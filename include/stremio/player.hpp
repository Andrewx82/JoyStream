#pragma once

#include <string>
#include <vector>
#include <functional>

struct mpv_handle;
struct mpv_render_context;
struct SDL_Texture;

namespace Stremio {

struct PlaybackState {
    bool isPlaying = false;
    bool isPaused = false;
    bool isBuffering = false;
    double currentTime = 0.0;
    double duration = 0.0;
    int cachePercent = 100;
    int videoWidth = 0;
    int videoHeight = 0;
    std::string hwdecCurrent;
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

    // Render video frame into an SDL streaming texture
    bool renderFrame(SDL_Texture* targetTexture);

    PlaybackState getState();
    void update(); // Poll events from MPV

    bool isInitialized() const { return m_mpv != nullptr; }

    // Dynamic runtime settings
    void setHwDec(bool enabled);
    void setAudioLanguage(const std::string& lang);
    void setSubtitleLanguage(const std::string& lang);
    void setSubtitlesEnabled(bool enabled);
    void setDemuxerMaxBytes(const std::string& bytes);
    void setNetworkTimeout(int seconds);

private:
    mpv_handle* m_mpv = nullptr;
    mpv_render_context* m_mpvRenderCtx = nullptr;
    PlaybackState m_state;
    bool m_needsRedraw = false;
};

} // namespace Stremio
