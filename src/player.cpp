#include "stremio/player.hpp"
#include <iostream>
#include <mpv/client.h>
#include <mpv/render.h>
#include <SDL2/SDL.h>

namespace Stremio {

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

    // Browser User-Agent prevents streaming hosts & Debrid CDNs from blocking connections
    mpv_set_option_string(m_mpv, "user-agent", "Mozilla/5.0 (Nintendo Switch; ShareApplet) JoyStream/0.1");

    // Video Output: Use libmpv rendering backend for mpv_render_context
    mpv_set_option_string(m_mpv, "vo", "libmpv");

#ifdef __SWITCH__
    // Hardware accelerated video decoding on Tegra X1 & Horizon native audio
    mpv_set_option_string(m_mpv, "hwdec", "auto");
    mpv_set_option_string(m_mpv, "ao", "hos,sdl");
#else
    mpv_set_option_string(m_mpv, "hwdec", "auto");
    mpv_set_option_string(m_mpv, "ao", "pulse,alsa,sdl");
#endif

    // Cache and network tuning for Switch Wi-Fi & streaming
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "128M");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "32M");
    mpv_set_option_string(m_mpv, "network-timeout", "20");
    mpv_set_option_string(m_mpv, "ytdl", "no");

    // Request MPV log messages to capture stream loading errors
    mpv_request_log_messages(m_mpv, "info");

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
    m_needsRedraw = true;

    // Set custom headers if provided
    if (!headers.empty()) {
        std::string headerList;
        for (const auto& h : headers) {
            if (!headerList.empty()) headerList += ",";
            headerList += h;
        }
        mpv_set_option_string(m_mpv, "http-header-fields", headerList.c_str());
    }

    const char* cmd[] = {"loadfile", url.c_str(), "replace", nullptr};
    int res = mpv_command(m_mpv, cmd);
    if (res < 0) {
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
                if (msg && (msg->log_level >= MPV_LOG_LEVEL_WARN || std::string(msg->prefix) == "cplayer" || std::string(msg->prefix) == "ffmpeg")) {
                    std::cout << "[mpv " << msg->prefix << "] " << msg->text;
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
                    m_state.errorMessage = mpv_error_string(endFile->error);
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
