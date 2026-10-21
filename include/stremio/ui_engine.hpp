#pragma once

#include "stremio/models.hpp"
#include "stremio/addon_manager.hpp"
#include "stremio/player.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

namespace Stremio {

enum class UiState {
    CATALOG,
    DETAILS,
    STREAMS,
    ADDONS,
    PLAYER
};

class UiEngine {
public:
    UiEngine();
    ~UiEngine();

    bool initialize();
    void run();
    void shutdown();

private:
    void handleInput();
    void update();
    void render();

    // Input actions
    void onNavigateLeft();
    void onNavigateRight();
    void onNavigateUp();
    void onNavigateDown();
    void onPagePrev();
    void onPageNext();
    void onActionConfirm();
    void onActionBack();
    void onActionSearch();
    void onActionToggleType();
    void onActionMenu();
    void onActionExit();

    // Poster Image Pipeline
    void queuePosterDownload(const std::string& url);
    void processDownloadedPosters();
    void posterDownloaderWorker();

    // Render helpers
    void drawText(const std::string& text, int x, int y, SDL_Color color, int fontSize = 20, bool rightAlign = false);
    void drawRect(int x, int y, int w, int h, SDL_Color color, bool filled = true);
    void drawHeader();
    void drawFooter();

    // State renderers
    void renderCatalog();
    void renderDetails();
    void renderStreams();
    void renderAddons();
    void renderPlayer();

    // Actions
    void loadCatalogData();
    void openSearch();
    void openAddAddon();
    void playStream(const StreamItem& stream);

    // SDL Resources
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_fontSmall = nullptr;
    TTF_Font* m_fontMedium = nullptr;
    TTF_Font* m_fontLarge = nullptr;
    bool m_running = false;

    // App State
    UiState m_state = UiState::CATALOG;
    AddonManager m_addonManager;
    VideoPlayer m_player;

    // Catalog & Content Data
    std::string m_contentType = "movie"; // "movie" or "series"
    std::vector<MetaItem> m_items;
    int m_selectedGridIndex = 0;
    int m_gridCols = 4;
    int m_gridRows = 2;
    int m_page = 0;
    bool m_isLoading = false;

    // Details View
    MetaItem m_selectedMeta;
    int m_detailsActionIndex = 0;

    // Streams View
    std::vector<StreamItem> m_availableStreams;
    int m_selectedStreamIndex = 0;

    // Addons View
    int m_selectedAddonIndex = 0;

    // OSD timer
    uint32_t m_lastPlayerInputTime = 0;
    bool m_showOsd = true;

    // Stick debounce
    uint32_t m_lastStickNavTime = 0;

    // Poster Textures & Async Downloader
    std::unordered_map<std::string, SDL_Texture*> m_posterTextures;
    std::unordered_map<std::string, std::string> m_downloadedImageData;
    std::queue<std::string> m_posterDownloadQueue;
    std::unordered_set<std::string> m_queuedUrls;
    std::mutex m_imageMutex;
    std::thread m_imageThread;
    std::atomic<bool> m_imageThreadRunning{false};
};

} // namespace Stremio
