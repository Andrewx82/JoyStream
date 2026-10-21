#pragma once

#include <string>
#include <vector>
#include <functional>

struct mpv_handle;

namespace Stremio {

struct PlaybackState {
    bool isPlaying = false;
    bool isPaused = false;
    bool isBuffering = false;
    double currentTime = 0.0;
    double duration = 0.0;
    int cachePercent = 0;
    std::string currentTitle;
    std::string mediaUrl;
    std::string errorMessage;
};

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool initialize();
    void shutdown();

    // Start playback of a stream URL
    bool play(const std::string& url, const std::string& title = "", const std::vector<std::string>& headers = {});

    void togglePause();
    void pause();
    void resume();
    void stop();
    void seekRelative(double seconds);
    void seekAbsolute(double seconds);

    PlaybackState getState();
    void update(); // Poll events from MPV

    bool isInitialized() const { return m_mpv != nullptr; }

private:
    mpv_handle* m_mpv = nullptr;
    PlaybackState m_state;
};

} // namespace Stremio
