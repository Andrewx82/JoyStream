#include "stremio/player.hpp"
#include <iostream>
#include <mpv/client.h>

namespace Stremio {

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    shutdown();
}

bool VideoPlayer::initialize() {
    if (m_mpv) return true;

    m_mpv = mpv_create();
    if (!m_mpv) {
        std::cerr << "Failed to create mpv context" << std::endl;
        return false;
    }

    // Configure video player options
#ifdef __SWITCH__
    // Hardware acceleration on Tegra X1 (NVDEC)
    mpv_set_option_string(m_mpv, "hwdec", "auto");
    mpv_set_option_string(m_mpv, "vo", "gpu");
    mpv_set_option_string(m_mpv, "gpu-context", "auto");
#else
    mpv_set_option_string(m_mpv, "hwdec", "auto");
#endif

    // Cache and network tuning for Switch Wi-Fi & streaming
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "128M");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "32M");
    mpv_set_option_string(m_mpv, "network-timeout", "15");
    mpv_set_option_string(m_mpv, "ytdl", "no");

    int res = mpv_initialize(m_mpv);
    if (res < 0) {
        std::cerr << "Failed to initialize mpv: " << mpv_error_string(res) << std::endl;
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return false;
    }

    // Observe properties for UI OSD
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "cache-buffering-state", MPV_FORMAT_INT64);

    return true;
}

void VideoPlayer::shutdown() {
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

    m_state.mediaUrl = url;
    m_state.currentTitle = title;
    m_state.isPlaying = true;
    m_state.isPaused = false;
    m_state.currentTime = 0.0;
    m_state.duration = 0.0;
    m_state.errorMessage = "";

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
        std::cerr << "Error loading stream: " << m_state.errorMessage << std::endl;
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

void VideoPlayer::update() {
    if (!m_mpv) return;

    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(event->data);
                if (prop && prop->format == MPV_FORMAT_DOUBLE) {
                    if (std::string(prop->name) == "time-pos") {
                        m_state.currentTime = *static_cast<double*>(prop->data);
                    } else if (std::string(prop->name) == "duration") {
                        m_state.duration = *static_cast<double*>(prop->data);
                    }
                } else if (prop && prop->format == MPV_FORMAT_FLAG) {
                    if (std::string(prop->name) == "pause") {
                        m_state.isPaused = (*static_cast<int*>(prop->data) != 0);
                    }
                } else if (prop && prop->format == MPV_FORMAT_INT64) {
                    if (std::string(prop->name) == "cache-buffering-state") {
                        m_state.cachePercent = static_cast<int>(*static_cast<int64_t*>(prop->data));
                    }
                }
                break;
            }
            case MPV_EVENT_END_FILE: {
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
