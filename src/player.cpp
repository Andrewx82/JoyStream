#include "stremio/player.hpp"
#include "stremio/curl_stream.hpp"
#include "stremio/switch_sys.hpp"
#include <iostream>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/stream_cb.h>
#include <SDL2/SDL.h>

namespace Stremio {

int VideoPlayer::streamCbOpen(void* user_data, char* uri, mpv_stream_cb_info* info) {
    auto* player = static_cast<VideoPlayer*>(user_data);
    std::string urlStr = uri ? uri : "";

    std::string realUrl = urlStr;
    if (urlStr.rfind("jshttp://", 0) == 0) {
        realUrl = "http://" + urlStr.substr(9);
    } else if (urlStr.rfind("jshttps://", 0) == 0) {
        realUrl = "https://" + urlStr.substr(10);
    }

    std::cout << "[VideoPlayer] streamCbOpen opening URL via CurlStream: " << realUrl << std::endl;

    auto* stream = new CurlStream(realUrl, player ? player->m_currentHeaders : std::map<std::string, std::string>{});
    if (!stream->open()) {
        std::cerr << "[VideoPlayer] CurlStream failed: " << stream->getLastError() << std::endl;
        if (player) {
            player->m_lastMpvLogMessage = "CurlStream: " + stream->getLastError();
        }
        delete stream;
        return -13; // MPV_ERROR_LOADING_FAILED
    }

    info->cookie = stream;
    info->read_fn = &CurlStream::mpvRead;
    info->seek_fn = &CurlStream::mpvSeek;
    info->size_fn = &CurlStream::mpvSize;
    info->close_fn = &CurlStream::mpvClose;
    info->cancel_fn = &CurlStream::mpvCancel;

    std::cout << "[VideoPlayer] CurlStream initialized successfully for: " << realUrl << std::endl;
    return 0;
}

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    shutdown();
}

bool VideoPlayer::initialize() {
    if (m_mpv) return true;

    m_mpv = mpv_create();
    if (!m_mpv) {
        std::cerr << "[Playback] Failed to create mpv context" << std::endl;
        return false;
    }

    // Critical for Switch: Atmosphere CFW lacks OS root CA bundles.
    // Disable TLS verification so HTTPS streams (Real-Debrid, Torrentio, CDNs) can connect.
    mpv_set_option_string(m_mpv, "tls-verify", "no");

    // Pass FFmpeg lavf options for TLS and network streaming reconnects
    mpv_set_option_string(m_mpv, "stream-lavf-o", "tls_verify=0,reconnect=1,reconnect_streamed=1,reconnect_delay_max=5");

    // Browser User-Agent prevents streaming hosts & Debrid CDNs from blocking connections
    mpv_set_option_string(m_mpv, "user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 JoyStream/0.1");

    // Video Output: Use libmpv rendering backend for mpv_render_context
    mpv_set_option_string(m_mpv, "vo", "libmpv");

    // Software decoding optimizations:
    // Multi-threaded decoding on all 4 CPU cores
    mpv_set_option_string(m_mpv, "hwdec", "no");
    mpv_set_option_string(m_mpv, "vd-lavc-threads", "4");
    mpv_set_option_string(m_mpv, "vd-lavc-fast", "yes");
    mpv_set_option_string(m_mpv, "vd-lavc-skiploopfilter", "nonref");
    mpv_set_option_string(m_mpv, "vd-lavc-dr", "yes");

    // Frame dropping and sync tuning for smooth playback
    mpv_set_option_string(m_mpv, "framedrop", "vo");
    mpv_set_option_string(m_mpv, "video-sync", "audio");

    // Fast ARM NEON SIMD bilinear scaling in libswscale
    mpv_set_option_string(m_mpv, "sws-scaler", "bilinear");
    mpv_set_option_string(m_mpv, "sws-fast", "yes");

#ifdef __SWITCH__
    mpv_set_option_string(m_mpv, "ao", "hos,sdl");
#else
    mpv_set_option_string(m_mpv, "ao", "pulse,alsa,sdl");
#endif

    // Cache and network tuning for Switch Wi-Fi & streaming
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "64M");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "16M");
    mpv_set_option_string(m_mpv, "demuxer-readahead-secs", "20");
    mpv_set_option_string(m_mpv, "network-timeout", "20");
    mpv_set_option_string(m_mpv, "ytdl", "no");

    // Register CurlStream custom protocol handlers for rock-solid Switch HTTPS streaming
    mpv_stream_cb_add_ro(m_mpv, "jshttp", this, &VideoPlayer::streamCbOpen);
    mpv_stream_cb_add_ro(m_mpv, "jshttps", this, &VideoPlayer::streamCbOpen);

    // Request MPV log messages to capture stream loading errors
    mpv_request_log_messages(m_mpv, "warn");

    int res = mpv_initialize(m_mpv);
    if (res < 0) {
        std::cerr << "[Playback] Failed to initialize mpv: " << mpv_error_string(res) << std::endl;
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return false;
    }

    // Initialize mpv_render_context with Software renderer (renders directly to texture)
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    int rres = mpv_render_context_create(&m_mpvRenderCtx, m_mpv, params);
    if (rres < 0) {
        std::cerr << "[Playback] Warning: Failed to create mpv_render_context: " << mpv_error_string(rres) << std::endl;
        m_mpvRenderCtx = nullptr;
    } else {
        std::cout << "[Playback] mpv_render_context initialized successfully." << std::endl;
    }

    // Observe properties for UI OSD
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "cache-buffering-state", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "dheight", MPV_FORMAT_INT64);

    return true;
}

void VideoPlayer::shutdown() {
    SwitchSys::restoreStock();
    if (m_mpvRenderCtx) {
        mpv_render_context_free(m_mpvRenderCtx);
        m_mpvRenderCtx = nullptr;
    }
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
    m_state = PlaybackState();
}

bool VideoPlayer::play(const std::string& url, const std::string& title, const std::vector<std::string>& headers) {
    if (!m_mpv && !initialize()) {
        return false;
    }

    std::cout << "[Playback] Loading URL: " << url << std::endl;
    m_state.mediaUrl = url;
    m_state.currentTitle = title;
    m_state.isPlaying = true;
    m_state.isPaused = false;
    m_state.isBuffering = true;
    m_state.cachePercent = 0;
    m_state.currentTime = 0.0;
    m_state.duration = 0.0;
    m_state.errorMessage = "";
    m_lastMpvLogMessage.clear();
    m_needsRedraw = true;

    // Save headers for CurlStream and MPV HTTP
    m_currentHeaders.clear();
    for (const auto& h : headers) {
        auto colon = h.find(':');
        if (colon != std::string::npos) {
            std::string k = h.substr(0, colon);
            std::string v = h.substr(colon + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.pop_back();
            m_currentHeaders[k] = v;
        }
    }

    if (!headers.empty()) {
        std::string headerList;
        for (const auto& h : headers) {
            headerList += h + "\r\n";
        }
        mpv_set_option_string(m_mpv, "http-header-fields", headerList.c_str());
    }

    // For HTTP/HTTPS streams, route through our CurlStream protocol handlers (jshttp / jshttps)
    // which bypasses Horizon SSL certstore issues and follows all redirects natively via libcurl.
    std::string playUrl = url;
    if (url.rfind("http://", 0) == 0) {
        playUrl = "jshttp://" + url.substr(7);
    } else if (url.rfind("https://", 0) == 0) {
        playUrl = "jshttps://" + url.substr(8);
    }

    std::cout << "[Playback] Dispatching to MPV: " << playUrl << std::endl;
    SwitchSys::boostPlayback();
    const char* cmd[] = {"loadfile", playUrl.c_str(), "replace", nullptr};
    int res = mpv_command(m_mpv, cmd);
    if (res < 0) {
        SwitchSys::restoreStock();
        m_state.errorMessage = mpv_error_string(res);
        std::cerr << "[Playback] Error issuing loadfile command: " << m_state.errorMessage << std::endl;
        return false;
    }

    return true;
}

void VideoPlayer::togglePause() {
    if (!m_mpv) return;
    int pauseVal = m_state.isPaused ? 0 : 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pauseVal);
    m_state.isPaused = !m_state.isPaused;
}

void VideoPlayer::pause() {
    if (!m_mpv) return;
    int pauseVal = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pauseVal);
    m_state.isPaused = true;
}

void VideoPlayer::resume() {
    if (!m_mpv) return;
    int pauseVal = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pauseVal);
    m_state.isPaused = false;
}

void VideoPlayer::stop() {
    SwitchSys::restoreStock();
    if (!m_mpv) return;
    const char* cmd[] = {"stop", nullptr};
    mpv_command(m_mpv, cmd);
    m_state.isPlaying = false;
    m_state.isPaused = false;
    m_state.currentTime = 0.0;
}

void VideoPlayer::seekRelative(double seconds) {
    if (!m_mpv) return;
    std::string secStr = std::to_string(seconds);
    const char* cmd[] = {"seek", secStr.c_str(), "relative", nullptr};
    mpv_command(m_mpv, cmd);
}

void VideoPlayer::seekAbsolute(double seconds) {
    if (!m_mpv) return;
    std::string secStr = std::to_string(seconds);
    const char* cmd[] = {"seek", secStr.c_str(), "absolute", nullptr};
    mpv_command(m_mpv, cmd);
}

bool VideoPlayer::renderFrame(SDL_Texture* targetTexture) {
    if (!m_mpvRenderCtx || !targetTexture) return false;

    uint64_t flags = mpv_render_context_update(m_mpvRenderCtx);
    if ((flags & MPV_RENDER_UPDATE_FRAME) == 0 && !m_needsRedraw) {
        return false;
    }
    m_needsRedraw = false;

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(targetTexture, nullptr, &pixels, &pitch) != 0) {
        return false;
    }

    int size[2] = {1280, 720};
    char format[] = "rgba";
    size_t stride = static_cast<size_t>(pitch);

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_SW_SIZE, size},
        {MPV_RENDER_PARAM_SW_FORMAT, format},
        {MPV_RENDER_PARAM_SW_STRIDE, &stride},
        {MPV_RENDER_PARAM_SW_POINTER, pixels},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context_render(m_mpvRenderCtx, params);
    SDL_UnlockTexture(targetTexture);
    return true;
}

void VideoPlayer::setHwDec(bool enabled) {
    if (!m_mpv) return;
    mpv_set_option_string(m_mpv, "hwdec", enabled ? "auto" : "no");
}

void VideoPlayer::setAudioLanguage(const std::string& lang) {
    if (!m_mpv) return;
    mpv_set_option_string(m_mpv, "alang", lang.c_str());
}

void VideoPlayer::setSubtitleLanguage(const std::string& lang) {
    if (!m_mpv) return;
    mpv_set_option_string(m_mpv, "slang", lang.c_str());
}

void VideoPlayer::setSubtitlesEnabled(bool enabled) {
    if (!m_mpv) return;
    mpv_set_option_string(m_mpv, "sid", enabled ? "auto" : "no");
}

void VideoPlayer::setDemuxerMaxBytes(const std::string& bytes) {
    if (!m_mpv) return;
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", bytes.c_str());
}

void VideoPlayer::setNetworkTimeout(int seconds) {
    if (!m_mpv) return;
    std::string s = std::to_string(seconds);
    mpv_set_option_string(m_mpv, "network-timeout", s.c_str());
}

void VideoPlayer::update() {
    if (!m_mpv) return;

    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;

        switch (event->event_id) {
            case MPV_EVENT_LOG_MESSAGE: {
                auto* msg = static_cast<mpv_event_log_message*>(event->data);
                if (msg) {
                    // Lower numeric log level in mpv means higher severity:
                    // MPV_LOG_LEVEL_FATAL = 10, ERROR = 20, WARN = 30
                    if (msg->log_level <= MPV_LOG_LEVEL_WARN) {
                        std::cout << "[mpv " << msg->prefix << "] " << msg->text;
                        std::string t = msg->text;
                        while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
                        if (!t.empty() && t.find("Exiting") == std::string::npos) {
                            m_lastMpvLogMessage = std::string(msg->prefix) + ": " + t;
                        }
                    }
                }
                break;
            }
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(event->data);
                if (!prop) break;

                std::string name = prop->name;
                if (prop->format == MPV_FORMAT_DOUBLE) {
                    if (name == "time-pos") {
                        m_state.currentTime = *static_cast<double*>(prop->data);
                        m_needsRedraw = true;
                    } else if (name == "duration") {
                        m_state.duration = *static_cast<double*>(prop->data);
                    }
                } else if (prop->format == MPV_FORMAT_FLAG) {
                    if (name == "pause") {
                        m_state.isPaused = (*static_cast<int*>(prop->data) != 0);
                    } else if (name == "paused-for-cache") {
                        m_state.isBuffering = (*static_cast<int*>(prop->data) != 0);
                    }
                } else if (prop->format == MPV_FORMAT_INT64) {
                    if (name == "cache-buffering-state") {
                        m_state.cachePercent = static_cast<int>(*static_cast<int64_t*>(prop->data));
                    } else if (name == "dwidth") {
                        m_state.videoWidth = static_cast<int>(*static_cast<int64_t*>(prop->data));
                    } else if (name == "dheight") {
                        m_state.videoHeight = static_cast<int>(*static_cast<int64_t*>(prop->data));
                    }
                } else if (prop->format == MPV_FORMAT_STRING) {
                    if (name == "hwdec-current") {
                        const char* val = *static_cast<const char**>(prop->data);
                        m_state.hwdecCurrent = val ? val : "";
                    }
                }
                break;
            }
            case MPV_EVENT_END_FILE: {
                auto* endFile = static_cast<mpv_event_end_file*>(event->data);
                if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR) {
                    std::string errStr = mpv_error_string(endFile->error);
                    if (!m_lastMpvLogMessage.empty()) {
                        m_state.errorMessage = errStr + " (" + m_lastMpvLogMessage + ")";
                    } else {
                        m_state.errorMessage = errStr;
                    }
                    std::cerr << "[Playback] Playback failed: " << m_state.errorMessage << std::endl;
                }
                m_state.isPlaying = false;
                break;
            }
            default:
                break;
        }
    }
}

PlaybackState VideoPlayer::getState() {
    update();
    return m_state;
}

} // namespace Stremio
