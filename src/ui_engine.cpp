#include "stremio/ui_engine.hpp"
#include "stremio/stremio_client.hpp"
#include "stremio/http_client.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Stremio {

static std::string requestTextInput(const std::string& guide, const std::string& initial = "") {
#ifdef __SWITCH__
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, guide.c_str());
        swkbdConfigSetInitialText(&kbd, initial.c_str());
        char buf[512] = {0};
        rc = swkbdShow(&kbd, buf, sizeof(buf));
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc)) {
            return std::string(buf);
        }
    }
#else
    std::cout << "\n[Input Prompt] " << guide << "\nEnter text: ";
    std::string line;
    std::getline(std::cin, line);
    return line;
#endif
    return "";
}

UiEngine::UiEngine() = default;

UiEngine::~UiEngine() {
    shutdown();
}

bool UiEngine::initialize() {
    std::cout << "[SDL] Initializing SDL2..." << std::endl;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        std::cerr << "[SDL] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    if (TTF_Init() < 0) {
        std::cerr << "[TTF] TTF_Init failed: " << TTF_GetError() << std::endl;
        return false;
    }

    int imgFlags = IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        std::cerr << "[IMG] Warning: IMG_Init: " << IMG_GetError() << std::endl;
    }

    // Open Joysticks on Switch
    int numJoysticks = SDL_NumJoysticks();
    std::cout << "[SDL] Found " << numJoysticks << " joystick(s)." << std::endl;
    for (int i = 0; i < numJoysticks && i < 2; ++i) {
        if (SDL_JoystickOpen(i) == nullptr) {
            std::cerr << "[SDL] Warning: Could not open joystick " << i << ": " << SDL_GetError() << std::endl;
        } else {
            std::cout << "[SDL] Opened joystick " << i << std::endl;
        }
    }

    // Window Creation
    std::cout << "[SDL] Creating 1280x720 window..." << std::endl;
#ifdef __SWITCH__
    m_window = SDL_CreateWindow("JoyStream", 0, 0, 1280, 720, 0);
#else
    m_window = SDL_CreateWindow("JoyStream", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN);
#endif

    if (!m_window) {
        std::cerr << "[SDL] Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        std::cout << "[SDL] Hardware renderer not available, falling back to software renderer..." << std::endl;
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!m_renderer) {
        std::cerr << "[SDL] Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create streaming texture for hardware/software video playback rendering
    m_videoTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 1280, 720);

    // Set 5 cols x 2 rows
    m_gridCols = 5;
    m_gridRows = 2;

    // Load Fonts
#ifdef __SWITCH__
    PlFontData sharedFont;
    Result rcPl = plInitialize(PlServiceType_User);
    std::cout << "[Font] plInitialize: 0x" << std::hex << rcPl << std::endl;
    if (R_SUCCEEDED(rcPl)) {
        Result rcFont = plGetSharedFontByType(&sharedFont, PlSharedFontType_Standard);
        std::cout << "[Font] plGetSharedFontByType: 0x" << std::hex << rcFont
                  << " addr: " << sharedFont.address
                  << " size: " << std::dec << sharedFont.size << std::endl;
        if (R_SUCCEEDED(rcFont) && sharedFont.address && sharedFont.size > 0) {
            m_fontSmall = TTF_OpenFontRW(SDL_RWFromMem(sharedFont.address, sharedFont.size), 0, 16);
            m_fontMedium = TTF_OpenFontRW(SDL_RWFromMem(sharedFont.address, sharedFont.size), 0, 22);
            m_fontLarge = TTF_OpenFontRW(SDL_RWFromMem(sharedFont.address, sharedFont.size), 0, 32);
        }
    }
#else
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf"
    };
    for (const char* p : fontPaths) {
        m_fontSmall = TTF_OpenFont(p, 16);
        if (m_fontSmall) {
            m_fontMedium = TTF_OpenFont(p, 22);
            m_fontLarge = TTF_OpenFont(p, 32);
            break;
        }
    }
#endif

    std::cout << "[Font] Font status: small=" << (m_fontSmall != nullptr)
              << " medium=" << (m_fontMedium != nullptr)
              << " large=" << (m_fontLarge != nullptr) << std::endl;

    // Load installed addons, filter settings & app settings
    std::cout << "[Config] Loading addon configurations & settings..." << std::endl;
    m_addonManager.loadConfig();
    m_filterConfig = m_addonManager.getFilterConfig();
    m_settings = m_addonManager.getSettings();
    std::cout << "[Config] Active addons: " << m_addonManager.getAddons().size()
              << " Filter: " << StreamFilter::getModeName(m_filterConfig.mode) << std::endl;

    applySettingsToPlayer();

    // Start background image loader thread
    m_imageThreadRunning = true;
    m_imageThread = std::thread(&UiEngine::posterDownloaderWorker, this);

    // Fetch initial catalog
    loadCatalogData();

    m_running = true;
    return true;
}

void UiEngine::shutdown() {
    m_imageThreadRunning = false;
    if (m_imageThread.joinable()) {
        m_imageThread.join();
    }

    m_player.shutdown();

    if (m_videoTexture) {
        SDL_DestroyTexture(m_videoTexture);
        m_videoTexture = nullptr;
    }

    // Destroy cached textures
    for (auto& [url, tex] : m_posterTextures) {
        if (tex) SDL_DestroyTexture(tex);
    }
    m_posterTextures.clear();

    if (m_fontSmall) TTF_CloseFont(m_fontSmall);
    if (m_fontMedium) TTF_CloseFont(m_fontMedium);
    if (m_fontLarge) TTF_CloseFont(m_fontLarge);
    m_fontSmall = m_fontMedium = m_fontLarge = nullptr;

#ifdef __SWITCH__
    plExit();
#endif

    TTF_Quit();
    IMG_Quit();

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

void UiEngine::applySettingsToPlayer() {
    m_player.setHwDec(m_settings.hwdecEnabled);
    m_player.setAudioLanguage(m_settings.audioLanguage);
    m_player.setSubtitlesEnabled(m_settings.subtitlesEnabled);
    m_player.setSubtitleLanguage(m_settings.subtitleLanguage);
    m_player.setDemuxerMaxBytes(m_settings.bufferCacheSize);
    m_player.setNetworkTimeout(m_settings.networkTimeoutSeconds);
}

void UiEngine::posterDownloaderWorker() {
    while (m_imageThreadRunning) {
        std::string nextUrl;
        {
            std::lock_guard<std::mutex> lock(m_imageMutex);
            if (!m_posterDownloadQueue.empty()) {
                nextUrl = m_posterDownloadQueue.front();
                m_posterDownloadQueue.pop();
            }
        }

        if (nextUrl.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }

        std::map<std::string, std::string> emptyHeaders;
        HttpResponse resp = HttpClient::get(nextUrl, emptyHeaders, 8);
        if (resp.success && !resp.body.empty()) {
            std::lock_guard<std::mutex> lock(m_imageMutex);
            m_downloadedImageData[nextUrl] = std::move(resp.body);
        }
    }
}

void UiEngine::queuePosterDownload(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::mutex> lock(m_imageMutex);
    if (m_posterTextures.find(url) == m_posterTextures.end() &&
        m_queuedUrls.find(url) == m_queuedUrls.end()) {
        m_queuedUrls.insert(url);
        m_posterDownloadQueue.push(url);
    }
}

void UiEngine::processDownloadedPosters() {
    std::unordered_map<std::string, std::string> readyImages;
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        if (!m_downloadedImageData.empty()) {
            readyImages = std::move(m_downloadedImageData);
            m_downloadedImageData.clear();
        }
    }

    for (auto& [url, data] : readyImages) {
        SDL_RWops* rw = SDL_RWFromMem(static_cast<void*>(data.data()), static_cast<int>(data.size()));
        if (!rw) continue;

        SDL_Surface* surface = IMG_Load_RW(rw, 1);
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surface);
            if (tex) {
                m_posterTextures[url] = tex;
            }
            SDL_FreeSurface(surface);
        }
    }
}

void UiEngine::loadCatalogData() {
    if (m_isSearchActive) return;

    m_isLoading = true;
    m_items.clear();

    try {
        auto catalogAddon = m_addonManager.getCatalogAddon(m_contentType);
        if (catalogAddon) {
            std::string catId = "top";
            std::map<std::string, std::string> extras;

            if (m_selectedCategoryIndex == 0) {
                catId = "top"; // Popular
            } else if (m_selectedCategoryIndex == 1) {
                catId = "imdbRating"; // Top Rated
            } else if (m_selectedCategoryIndex == 2) {
                catId = "year"; // New Releases
            } else if (m_selectedCategoryIndex < static_cast<int>(m_categories.size())) {
                catId = "top";
                extras["genre"] = m_categories[m_selectedCategoryIndex];
            }

            std::cout << "[Catalog] Requesting " << m_contentType
                      << " (Cat: " << m_categories[m_selectedCategoryIndex] << ") from " << catalogAddon->name << "..." << std::endl;
            m_items = StremioClient::fetchCatalog(*catalogAddon, m_contentType, catId, extras);
            std::cout << "[Catalog] Received " << m_items.size() << " items." << std::endl;
        } else {
            std::cerr << "[Catalog] No catalog addon active for type " << m_contentType << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Catalog] Error loading catalog: " << e.what() << std::endl;
    }

    int itemsPerPage = m_gridCols * m_gridRows;
    for (int i = 0; i < itemsPerPage && i < static_cast<int>(m_items.size()); ++i) {
        queuePosterDownload(m_items[i].poster);
    }

    m_selectedGridIndex = 0;
    m_isLoading = false;
}

void UiEngine::drawRect(int x, int y, int w, int h, SDL_Color color, bool filled) {
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = {x, y, w, h};
    if (filled) {
        SDL_RenderFillRect(m_renderer, &r);
    } else {
        SDL_RenderDrawRect(m_renderer, &r);
    }
}

void UiEngine::drawText(const std::string& text, int x, int y, SDL_Color color, int fontSize, bool rightAlign) {
    if (!m_renderer || text.empty()) return;

    TTF_Font* font = m_fontMedium;
    if (fontSize <= 17 && m_fontSmall) font = m_fontSmall;
    else if (fontSize >= 26 && m_fontLarge) font = m_fontLarge;
    if (!font) return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        int finalX = rightAlign ? (x - surface->w) : x;
        SDL_Rect dst = {finalX, y, surface->w, surface->h};
        SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void UiEngine::drawCenteredText(const std::string& text, int x, int y, int w, int h, SDL_Color color, int fontSize) {
    if (!m_renderer || text.empty()) return;

    TTF_Font* font = m_fontMedium;
    if (fontSize <= 17 && m_fontSmall) font = m_fontSmall;
    else if (fontSize >= 26 && m_fontLarge) font = m_fontLarge;
    if (!font) return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        SDL_Rect dst = {x + (w - surface->w) / 2, y + (h - surface->h) / 2, surface->w, surface->h};
        SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void UiEngine::drawTextureFitted(SDL_Texture* tex, int x, int y, int w, int h) {
    if (!tex || !m_renderer) return;

    int texW = 0, texH = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH) < 0 || texW <= 0 || texH <= 0) {
        return;
    }

    // Preserve natural aspect ratio
    float scale = std::min(static_cast<float>(w) / texW, static_cast<float>(h) / texH);
    int finalW = static_cast<int>(texW * scale);
    int finalH = static_cast<int>(texH * scale);

    int finalX = x + (w - finalW) / 2;
    int finalY = y + (h - finalH) / 2;

    SDL_Rect dst = {finalX, finalY, finalW, finalH};
    SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
}

void UiEngine::drawHeader() {
    // Header Bar
    drawRect(0, 0, 1280, 70, {18, 20, 29, 255});
    drawRect(0, 68, 1280, 2, {50, 54, 75, 255});

    if (!m_isSearchActive) {
        // Logo
        drawText("JOYSTREAM", 30, 18, {109, 93, 252, 255}, 30);
        // Neon dots for Joy-Con flavor
        drawRect(205, 24, 6, 24, {0, 212, 255, 255});
        drawRect(214, 24, 6, 24, {255, 60, 40, 255});

        // Movies & Series Toggle Tabs
        bool isMovie = (m_contentType == "movie");
        SDL_Color movieColor = isMovie ? SDL_Color{0, 212, 255, 255} : SDL_Color{140, 145, 170, 255};
        SDL_Color seriesColor = !isMovie ? SDL_Color{0, 212, 255, 255} : SDL_Color{140, 145, 170, 255};

        drawRect(235, 16, 95, 38, isMovie ? SDL_Color{35, 42, 65, 255} : SDL_Color{22, 25, 36, 255});
        drawCenteredText("Movies", 235, 16, 95, 38, movieColor, 18);

        drawRect(340, 16, 95, 38, !isMovie ? SDL_Color{35, 42, 65, 255} : SDL_Color{22, 25, 36, 255});
        drawCenteredText("Series", 340, 16, 95, 38, seriesColor, 18);

        // Category Selector Pill
        int catX = 445;
        int catW = 200;
        drawRect(catX, 16, catW, 38, {30, 36, 54, 255});
        drawRect(catX, 16, catW, 38, {65, 75, 110, 255}, false);
        std::string catLabel = m_categories[m_selectedCategoryIndex] + "  ▼";
        if (catLabel.length() > 18) catLabel = catLabel.substr(0, 16) + "..";
        drawCenteredText(catLabel, catX, 16, catW, 38, {0, 212, 255, 255}, 16);

        // Page indicator
        int itemsPerPage = m_gridCols * m_gridRows;
        int totalPages = std::max(1, static_cast<int>(std::ceil(static_cast<double>(m_items.size()) / itemsPerPage)));
        std::string pageInfo = "◀ " + std::to_string(m_page + 1) + "/" + std::to_string(totalPages) + " ▶";
        drawCenteredText(pageInfo, 660, 16, 120, 38, {180, 185, 205, 255}, 16);

        // Settings Button
        bool isSettings = (m_state == UiState::SETTINGS);
        drawRect(790, 16, 140, 38, isSettings ? SDL_Color{50, 60, 90, 255} : SDL_Color{28, 33, 48, 255});
        drawCenteredText("Settings (-)", 790, 16, 140, 38, isSettings ? SDL_Color{0, 212, 255, 255} : SDL_Color{200, 205, 225, 255}, 16);

        // Addons Button
        bool isAddons = (m_state == UiState::ADDONS);
        drawRect(940, 16, 120, 38, isAddons ? SDL_Color{50, 60, 90, 255} : SDL_Color{28, 33, 48, 255});
        std::string addonInfo = std::to_string(m_addonManager.getAddons().size()) + " Addons";
        drawCenteredText(addonInfo, 940, 16, 120, 38, isAddons ? SDL_Color{0, 212, 255, 255} : SDL_Color{200, 205, 225, 255}, 16);

        // Search Button (X)
        drawRect(1070, 16, 180, 38, {109, 93, 252, 255});
        drawCenteredText("Search (X)", 1070, 16, 180, 38, {255, 255, 255, 255}, 18);
    } else {
        // Search Banner
        std::string searchDisplay = "Search: \"" + (m_searchQuery.length() > 14 ? m_searchQuery.substr(0, 12) + ".." : m_searchQuery) + "\"";
        drawText(searchDisplay, 30, 22, {255, 255, 255, 255}, 20);

        // Search Tabs: [ ALL ] [ MOVIES ] [ TV SHOWS ]
        auto drawSearchTab = [&](int tabIdx, int x, int w, const std::string& label, size_t count) {
            bool active = (m_searchTab == tabIdx);
            drawRect(x, 16, w, 38, active ? SDL_Color{40, 70, 120, 255} : SDL_Color{24, 28, 40, 255});
            if (active) drawRect(x, 16, w, 38, {0, 212, 255, 255}, false);
            std::string t = label + " (" + std::to_string(count) + ")";
            drawCenteredText(t, x, 16, w, 38, active ? SDL_Color{0, 212, 255, 255} : SDL_Color{160, 165, 185, 255}, 15);
        };

        drawSearchTab(0, 320, 120, "All", m_searchAllResults.size());
        drawSearchTab(1, 450, 140, "Movies", m_searchMovieResults.size());
        drawSearchTab(2, 600, 150, "TV Shows", m_searchSeriesResults.size());

        // Clear Search Button
        drawRect(760, 16, 110, 38, {60, 35, 45, 255});
        drawCenteredText("✕ Clear", 760, 16, 110, 38, {255, 140, 140, 255}, 16);

        // Page indicator
        int itemsPerPage = m_gridCols * m_gridRows;
        int totalPages = std::max(1, static_cast<int>(std::ceil(static_cast<double>(m_items.size()) / itemsPerPage)));
        std::string pageInfo = "◀ " + std::to_string(m_page + 1) + "/" + std::to_string(totalPages) + " ▶";
        drawCenteredText(pageInfo, 880, 16, 110, 38, {180, 185, 205, 255}, 15);

        // Settings Button
        drawRect(1000, 16, 120, 38, {28, 33, 48, 255});
        drawCenteredText("Settings", 1000, 16, 120, 38, {200, 205, 225, 255}, 16);

        // Addons Button
        drawRect(1130, 16, 120, 38, {28, 33, 48, 255});
        drawCenteredText("Addons", 1130, 16, 120, 38, {200, 205, 225, 255}, 16);
    }
}

void UiEngine::drawFooter() {
    drawRect(0, 660, 1280, 60, {18, 20, 29, 255});
    drawRect(0, 660, 1280, 2, {50, 54, 75, 255});

    std::string hints;
    if (m_state == UiState::CATALOG) {
        if (m_isSearchActive) {
            hints = "(Touch or D-Pad) Select   (A) Details   (L/R) Switch Tab   (X) New Search   (B) Clear Search";
        } else {
            hints = "(Touch or D-Pad) Select   (A) Details   (L/R) Page   (X) Search   (Y) Type   (ZL) Categories   (-) Settings";
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        hints = "(D-Pad or Touch) Choose Category   (A) Confirm   (B) Close";
    } else if (m_state == UiState::SETTINGS) {
        hints = "(D-Pad Up/Down) Select   (Left/Right or A) Change Value   (B) Back to Catalog";
    } else if (m_state == UiState::DETAILS) {
        hints = "(A / Touch) Watch Streams   (B) Back to Catalog";
    } else if (m_state == UiState::STREAMS) {
        hints = "(D-Pad / Touch) Select   (A) Play   (Y) Cycle Filter   (X) Edit Regex   (B) Back";
    } else if (m_state == UiState::ADDONS) {
        hints = "(Touch or D-Pad) Toggle   (X) Add New Addon   (B) Back";
    } else if (m_state == UiState::PLAYER) {
        hints = "(Touch / A) Play/Pause   (D-Pad Left/Right) Seek 10s   (Touch Bar) Scrub   (B) Stop";
    }

    drawText(hints, 40, 678, {160, 165, 185, 255}, 18);
}

void UiEngine::renderCatalog() {
    if (m_isLoading) {
        drawText("Connecting to Stremio Catalogs...", 460, 330, {200, 200, 220, 255}, 24);
        drawText("Please wait...", 560, 370, {150, 155, 175, 255}, 18);
        return;
    }

    if (m_items.empty()) {
        if (m_isSearchActive) {
            drawText("No search results found for: " + m_searchQuery, 420, 300, {230, 140, 140, 255}, 22);
            drawText("Try a different search query or tap ✕ Clear.", 430, 345, {180, 185, 205, 255}, 18);
        } else {
            drawText("No items loaded for category: " + m_categories[m_selectedCategoryIndex], 420, 300, {230, 100, 100, 255}, 22);
            drawText("Check Wi-Fi connection, or tap (-) to view Addons.", 410, 345, {180, 185, 205, 255}, 18);
        }
        return;
    }

    // 5 columns x 2 rows
    int startX = 40;
    int startY = 85;
    int cardW = 224;
    int cardH = 265;
    int gapX = 20;
    int gapY = 15;

    int itemsPerPage = m_gridCols * m_gridRows;
    int startIndex = m_page * itemsPerPage;

    for (int i = 0; i < itemsPerPage; ++i) {
        int itemIdx = startIndex + i;
        if (itemIdx >= static_cast<int>(m_items.size())) break;

        const auto& item = m_items[itemIdx];
        int col = i % m_gridCols;
        int row = i / m_gridCols;
        int x = startX + col * (cardW + gapX);
        int y = startY + row * (cardH + gapY);

        bool isSelected = (i == m_selectedGridIndex);

        // Card background
        SDL_Color bg = isSelected ? SDL_Color{35, 42, 65, 255} : SDL_Color{24, 27, 38, 255};
        drawRect(x, y, cardW, cardH, bg);

        // Selection Border
        if (isSelected) {
            drawRect(x - 2, y - 2, cardW + 4, cardH + 4, {109, 93, 252, 255}, false);
            drawRect(x - 1, y - 1, cardW + 2, cardH + 2, {0, 212, 255, 255}, false);
        }

        // Poster box: 204w x 180h
        int pBoxX = x + 10;
        int pBoxY = y + 8;
        int pBoxW = cardW - 20;
        int pBoxH = 180;

        drawRect(pBoxX, pBoxY, pBoxW, pBoxH, {16, 18, 25, 255});

        bool posterRendered = false;
        if (!item.poster.empty()) {
            auto it = m_posterTextures.find(item.poster);
            if (it != m_posterTextures.end() && it->second) {
                drawTextureFitted(it->second, pBoxX, pBoxY, pBoxW, pBoxH);
                posterRendered = true;
            } else {
                queuePosterDownload(item.poster);
            }
        }

        if (!posterRendered) {
            std::string shortTitle = item.name.substr(0, 14);
            drawCenteredText(shortTitle, pBoxX, pBoxY, pBoxW, pBoxH, {80, 85, 110, 255}, 16);
        }

        // Movie Title (truncate cleanly)
        std::string title = item.name;
        if (title.length() > 20) title = title.substr(0, 18) + "..";
        drawText(title, x + 12, y + 196, {245, 245, 255, 255}, 18);

        // Year and IMDb Rating
        std::string metaStr = item.releaseInfo;
        if (!item.imdbRating.empty()) {
            metaStr += "  * " + item.imdbRating;
        }
        drawText(metaStr, x + 12, y + 220, {230, 180, 50, 255}, 16);

        // Type Badge
        drawText("[" + item.type + "]", x + 12, y + 242, {130, 135, 160, 255}, 13);
    }
}

void UiEngine::renderDetails() {
    // Backdrop Panel
    drawRect(40, 90, 1200, 550, {22, 25, 36, 255});

    // Poster Box (260w x 390h)
    int pBoxX = 70;
    int pBoxY = 120;
    int pBoxW = 260;
    int pBoxH = 390;
    drawRect(pBoxX, pBoxY, pBoxW, pBoxH, {15, 17, 24, 255});

    bool posterRendered = false;
    if (!m_selectedMeta.poster.empty()) {
        auto it = m_posterTextures.find(m_selectedMeta.poster);
        if (it != m_posterTextures.end() && it->second) {
            drawTextureFitted(it->second, pBoxX, pBoxY, pBoxW, pBoxH);
            posterRendered = true;
        } else {
            queuePosterDownload(m_selectedMeta.poster);
        }
    }
    if (!posterRendered) {
        drawCenteredText("POSTER", pBoxX, pBoxY, pBoxW, pBoxH, {90, 95, 120, 255}, 20);
    }

    // Title & Info
    drawText(m_selectedMeta.name, 360, 125, {255, 255, 255, 255}, 32);

    std::string sub = m_selectedMeta.releaseInfo + "  |  IMDb: " + (m_selectedMeta.imdbRating.empty() ? "N/A" : m_selectedMeta.imdbRating);
    drawText(sub, 360, 175, {230, 180, 50, 255}, 20);

    // Genres
    std::string genreStr = "Genres: ";
    for (size_t i = 0; i < m_selectedMeta.genres.size(); ++i) {
        genreStr += m_selectedMeta.genres[i] + (i + 1 < m_selectedMeta.genres.size() ? ", " : "");
    }
    drawText(genreStr, 360, 210, {160, 170, 200, 255}, 18);

    // Synopsis
    drawText("Synopsis:", 360, 255, {200, 200, 220, 255}, 20);
    std::string desc = m_selectedMeta.description;
    size_t lineLen = 65;
    for (size_t i = 0; i < 4 && i * lineLen < desc.length(); ++i) {
        std::string chunk = desc.substr(i * lineLen, lineLen);
        drawText(chunk, 360, 290 + static_cast<int>(i) * 26, {180, 185, 205, 255}, 18);
    }

    if (!m_selectedMeta.videos.empty()) {
        std::string epCount = "Episodes Available: " + std::to_string(m_selectedMeta.videos.size());
        drawText(epCount, 360, 415, {0, 212, 255, 255}, 18);
    }

    // Action Button 1: "▶ WATCH NOW"
    int btnX = 360;
    int btnY = 475;
    int btnW = 280;
    int btnH = 55;

    bool btnFocus = (m_detailsActionIndex == 0);
    SDL_Color btnBg = btnFocus ? SDL_Color{109, 93, 252, 255} : SDL_Color{50, 55, 75, 255};
    drawRect(btnX, btnY, btnW, btnH, btnBg);
    if (btnFocus) {
        drawRect(btnX - 2, btnY - 2, btnW + 4, btnH + 4, {0, 212, 255, 255}, false);
    }
    drawCenteredText("▶  WATCH NOW", btnX, btnY, btnW, btnH, {255, 255, 255, 255}, 22);

    // Action Button 2: "BACK"
    int backBtnX = 660;
    int backBtnY = 475;
    int backBtnW = 150;
    int backBtnH = 55;
    drawRect(backBtnX, backBtnY, backBtnW, backBtnH, {38, 42, 58, 255});
    drawCenteredText("BACK", backBtnX, backBtnY, backBtnW, backBtnH, {200, 205, 225, 255}, 20);
}

void UiEngine::renderStreams() {
    drawRect(0, 0, 1280, 720, {10, 10, 15, 215});

    int mw = 900;
    int mh = 530;
    int mx = (1280 - mw) / 2;
    int my = (720 - mh) / 2;

    drawRect(mx, my, mw, mh, {24, 27, 38, 255});
    drawRect(mx, my, mw, 52, {35, 40, 58, 255});

    std::string countStr = "STREAMS (" + std::to_string(m_availableStreams.size()) + "/" + std::to_string(m_rawStreams.size()) + ")";
    drawText(countStr, mx + 25, my + 14, {255, 255, 255, 255}, 22);

    // Filter Mode Toggle Button (Touch / Y)
    int fBtnX = mx + 350;
    int fBtnY = my + 9;
    int fBtnW = 270;
    int fBtnH = 34;
    drawRect(fBtnX, fBtnY, fBtnW, fBtnH, {48, 56, 82, 255});
    drawRect(fBtnX, fBtnY, fBtnW, fBtnH, {0, 212, 255, 255}, false);
    drawCenteredText("Filter: " + StreamFilter::getModeName(m_filterConfig.mode) + " (Y)", fBtnX, fBtnY, fBtnW, fBtnH, {0, 212, 255, 255}, 16);

    // Edit Custom Regex Button (Touch / X)
    if (m_filterConfig.mode == StreamFilterMode::CUSTOM_REGEX) {
        int eBtnX = fBtnX + fBtnW + 10;
        int eBtnY = fBtnY;
        int eBtnW = 150;
        int eBtnH = fBtnH;
        drawRect(eBtnX, eBtnY, eBtnW, eBtnH, {109, 93, 252, 255});
        drawCenteredText("Edit Regex (X)", eBtnX, eBtnY, eBtnW, eBtnH, {255, 255, 255, 255}, 16);
    }

    // Close button (X) on top-right of modal
    drawRect(mx + mw - 45, my + 10, 32, 32, {60, 65, 85, 255});
    drawCenteredText("X", mx + mw - 45, my + 10, 32, 32, {255, 255, 255, 255}, 18);

    // Filter mode description subtitle
    std::string modeDesc = StreamFilter::getModeDescription(m_filterConfig.mode);
    drawText(modeDesc, mx + 25, my + 56, {150, 155, 180, 255}, 14);

    if (!m_streamErrorMessage.empty()) {
        drawRect(mx + 20, my + 54, mw - 40, 22, {80, 30, 30, 255});
        drawText("Notice: " + m_streamErrorMessage, mx + 25, my + 57, {255, 140, 140, 255}, 13);
    }

    if (m_availableStreams.empty()) {
        drawText("No streams match current filter: " + StreamFilter::getModeName(m_filterConfig.mode), mx + 50, my + 130, {230, 130, 130, 255}, 20);
        drawText("Tap or press (Y) to cycle filters, or switch to 'Show All'.", mx + 50, my + 170, {180, 185, 205, 255}, 18);
        return;
    }

    int startY = my + 78;
    int itemH = 68;
    int maxVisible = 6;

    for (int i = 0; i < maxVisible && i < static_cast<int>(m_availableStreams.size()); ++i) {
        const auto& stream = m_availableStreams[i];
        int y = startY + i * itemH;
        bool isSelected = (i == m_selectedStreamIndex);

        SDL_Color bg = isSelected ? SDL_Color{45, 52, 78, 255} : SDL_Color{28, 32, 45, 255};
        drawRect(mx + 20, y, mw - 40, itemH - 8, bg);

        if (isSelected) {
            drawRect(mx + 18, y - 2, mw - 36, itemH - 4, {109, 93, 252, 255}, false);
        }

        std::string streamName = stream.name;
        std::replace(streamName.begin(), streamName.end(), '\n', ' ');
        drawText(streamName, mx + 35, y + 8, {0, 212, 255, 255}, 20);

        // Badge: Direct HTTPS / Debrid vs P2P
        if (!stream.url.empty()) {
            drawRect(mx + mw - 190, y + 10, 150, 22, {35, 75, 45, 255});
            drawCenteredText("DIRECT / DEBRID", mx + mw - 190, y + 10, 150, 22, {80, 240, 120, 255}, 13);
        } else {
            drawRect(mx + mw - 190, y + 10, 150, 22, {70, 50, 30, 255});
            drawCenteredText("P2P TORRENT", mx + mw - 190, y + 10, 150, 22, {240, 180, 80, 255}, 13);
        }

        std::string title = stream.title;
        std::replace(title.begin(), title.end(), '\n', ' ');
        if (title.length() > 68) title = title.substr(0, 66) + "..";
        drawText(title, mx + 35, y + 33, {200, 205, 220, 255}, 15);
    }
}

void UiEngine::renderCategoriesModal() {
    drawRect(0, 0, 1280, 720, {10, 10, 15, 220});

    int mw = 840;
    int mh = 490;
    int mx = (1280 - mw) / 2;
    int my = (720 - mh) / 2;

    drawRect(mx, my, mw, mh, {24, 27, 38, 255});
    drawRect(mx, my, mw, 52, {35, 40, 58, 255});

    drawText("SELECT CATEGORY / GENRE", mx + 25, my + 14, {255, 255, 255, 255}, 22);

    // Close button
    drawRect(mx + mw - 45, my + 10, 32, 32, {60, 65, 85, 255});
    drawCenteredText("X", mx + mw - 45, my + 10, 32, 32, {255, 255, 255, 255}, 18);

    int cols = 4;
    int btnW = 180;
    int btnH = 68;
    int gapX = 18;
    int gapY = 18;
    int startX = mx + 30;
    int startY = my + 80;

    for (size_t i = 0; i < m_categories.size() && i < 16; ++i) {
        int c = static_cast<int>(i) % cols;
        int r = static_cast<int>(i) / cols;
        int bx = startX + c * (btnW + gapX);
        int by = startY + r * (btnH + gapY);

        bool isCurrent = (static_cast<int>(i) == m_selectedCategoryIndex);
        bool isFocused = (static_cast<int>(i) == m_modalCategoryIndex);

        SDL_Color bg = isCurrent ? SDL_Color{35, 60, 95, 255} : (isFocused ? SDL_Color{48, 54, 78, 255} : SDL_Color{30, 34, 48, 255});
        drawRect(bx, by, btnW, btnH, bg);

        if (isFocused || isCurrent) {
            drawRect(bx, by, btnW, btnH, isCurrent ? SDL_Color{0, 212, 255, 255} : SDL_Color{109, 93, 252, 255}, false);
        }

        drawCenteredText(m_categories[i], bx, by, btnW, btnH, isCurrent ? SDL_Color{0, 212, 255, 255} : SDL_Color{220, 225, 240, 255}, 17);
    }
}

void UiEngine::renderSettings() {
    drawHeader();

    int panelW = 1200;
    int panelH = 550;
    int panelX = 40;
    int panelY = 90;

    drawRect(panelX, panelY, panelW, panelH, {22, 25, 36, 255});
    drawRect(panelX, panelY, panelW, 50, {32, 38, 56, 255});
    drawText("SETTINGS", panelX + 30, panelY + 12, {255, 255, 255, 255}, 24);

    // Back button in settings
    drawRect(panelX + panelW - 130, panelY + 9, 110, 32, {48, 54, 76, 255});
    drawCenteredText("◀ Back (B)", panelX + panelW - 130, panelY + 9, 110, 32, {200, 205, 225, 255}, 16);

    struct SettingItem {
        std::string label;
        std::string value;
        std::string desc;
    };

    std::vector<SettingItem> items = {
        {"Hardware Video Decoding", m_settings.hwdecEnabled ? "Enabled (NVDEC)" : "Disabled (Software)", "Uses Nintendo Switch Tegra X1 NVDEC hardware accelerator for 60fps video."},
        {"Audio Language Preference", m_settings.audioLanguage, "Preferred audio stream language track (e.g. auto, eng, spa, jpn)."},
        {"Subtitles", m_settings.subtitlesEnabled ? "Enabled" : "Disabled", "Automatically loads and displays subtitles during stream playback."},
        {"Subtitle Language", m_settings.subtitleLanguage, "Preferred subtitle language track (e.g. eng, spa, fre, ger)."},
        {"Demuxer Cache Buffer", m_settings.bufferCacheSize, "RAM memory cache dedicated to buffering streams against Wi-Fi drops."},
        {"Network Stream Timeout", std::to_string(m_settings.networkTimeoutSeconds) + " Seconds", "Max time before retrying a lagging video stream or slow CDN."},
        {"Default Stream Filter", StreamFilter::getModeName(m_filterConfig.mode), "Preset used to filter streams (Switch Optimized removes unplayable 4K/AV1/CAM)."},
        {"Max Stream File Size", m_filterConfig.maxFileSizeGB > 0 ? std::to_string(static_cast<int>(m_filterConfig.maxFileSizeGB)) + " GB" : "Unlimited", "Excludes huge files that cause Wi-Fi buffer under-runs on the Switch."},
        {"Custom Exclude Regex", m_filterConfig.customExcludeRegex.substr(0, 36) + (m_filterConfig.customExcludeRegex.length() > 36 ? ".." : ""), "Regular expression pattern excluded from stream results (tap to edit)."},
        {"Clear Poster Cache", "Tap or Press (A)", "Cleans in-memory poster textures to free RAM."},
        {"Reset All to Defaults", "Tap or Press (A)", "Restores factory default player and stream settings."}
    };

    int listY = panelY + 58;
    int rowH = 41;
    for (size_t i = 0; i < items.size(); ++i) {
        int ry = listY + static_cast<int>(i) * rowH;
        bool isSel = (static_cast<int>(i) == m_selectedSettingIndex);

        if (isSel) {
            drawRect(panelX + 15, ry, panelW - 30, rowH - 4, {45, 52, 78, 255});
            drawRect(panelX + 15, ry, panelW - 30, rowH - 4, {109, 93, 252, 255}, false);
        }

        drawText(items[i].label, panelX + 35, ry + 8, isSel ? SDL_Color{0, 212, 255, 255} : SDL_Color{220, 225, 240, 255}, 18);

        // Value button pill
        int valW = 280;
        int valH = 30;
        int valX = panelX + panelW - valW - 35;
        int valY = ry + 4;
        drawRect(valX, valY, valW, valH, isSel ? SDL_Color{60, 68, 98, 255} : SDL_Color{35, 40, 56, 255});
        drawCenteredText(items[i].value, valX, valY, valW, valH, isSel ? SDL_Color{255, 255, 255, 255} : SDL_Color{180, 185, 205, 255}, 16);
    }

    // Bottom description banner
    if (m_selectedSettingIndex >= 0 && m_selectedSettingIndex < static_cast<int>(items.size())) {
        int descY = panelY + panelH - 34;
        drawRect(panelX + 20, descY, panelW - 40, 26, {18, 20, 28, 200});
        drawText("ℹ  " + items[m_selectedSettingIndex].desc, panelX + 35, descY + 4, {160, 165, 190, 255}, 15);
    }
}

void UiEngine::changeSetting(int delta) {
    switch (m_selectedSettingIndex) {
        case 0: // HW Decoding
            m_settings.hwdecEnabled = !m_settings.hwdecEnabled;
            break;
        case 1: { // Audio Language
            std::vector<std::string> langs = {"auto", "eng", "spa", "fre", "ger", "jpn", "por", "ita"};
            auto it = std::find(langs.begin(), langs.end(), m_settings.audioLanguage);
            int idx = (it != langs.end()) ? static_cast<int>(std::distance(langs.begin(), it)) : 0;
            idx = (idx + delta + static_cast<int>(langs.size())) % static_cast<int>(langs.size());
            m_settings.audioLanguage = langs[idx];
            break;
        }
        case 2: // Subtitles
            m_settings.subtitlesEnabled = !m_settings.subtitlesEnabled;
            break;
        case 3: { // Subtitle Language
            std::vector<std::string> langs = {"eng", "spa", "fre", "ger", "jpn", "por", "ita"};
            auto it = std::find(langs.begin(), langs.end(), m_settings.subtitleLanguage);
            int idx = (it != langs.end()) ? static_cast<int>(std::distance(langs.begin(), it)) : 0;
            idx = (idx + delta + static_cast<int>(langs.size())) % static_cast<int>(langs.size());
            m_settings.subtitleLanguage = langs[idx];
            break;
        }
        case 4: { // Demuxer Buffer Cache
            std::vector<std::string> sizes = {"64M", "128M", "256M"};
            auto it = std::find(sizes.begin(), sizes.end(), m_settings.bufferCacheSize);
            int idx = (it != sizes.end()) ? static_cast<int>(std::distance(sizes.begin(), it)) : 1;
            idx = (idx + delta + static_cast<int>(sizes.size())) % static_cast<int>(sizes.size());
            m_settings.bufferCacheSize = sizes[idx];
            break;
        }
        case 5: { // Network Timeout
            std::vector<int> timeouts = {10, 15, 20, 30};
            auto it = std::find(timeouts.begin(), timeouts.end(), m_settings.networkTimeoutSeconds);
            int idx = (it != timeouts.end()) ? static_cast<int>(std::distance(timeouts.begin(), it)) : 2;
            idx = (idx + delta + static_cast<int>(timeouts.size())) % static_cast<int>(timeouts.size());
            m_settings.networkTimeoutSeconds = timeouts[idx];
            break;
        }
        case 6: // Filter Mode
            cycleFilterMode();
            return;
        case 7: { // Max File Size
            std::vector<float> sizes = {10.0f, 15.0f, 25.0f, 0.0f};
            auto it = std::find(sizes.begin(), sizes.end(), m_filterConfig.maxFileSizeGB);
            int idx = (it != sizes.end()) ? static_cast<int>(std::distance(sizes.begin(), it)) : 2;
            idx = (idx + delta + static_cast<int>(sizes.size())) % static_cast<int>(sizes.size());
            m_filterConfig.maxFileSizeGB = sizes[idx];
            m_addonManager.setFilterConfig(m_filterConfig);
            return;
        }
        case 8: // Custom Exclude Regex
            editCustomRegex();
            return;
        case 9: { // Clear poster cache
            for (auto& [url, tex] : m_posterTextures) {
                if (tex) SDL_DestroyTexture(tex);
            }
            m_posterTextures.clear();
            m_queuedUrls.clear();
            int itemsPerPage = m_gridCols * m_gridRows;
            for (int i = 0; i < itemsPerPage && i < static_cast<int>(m_items.size()); ++i) {
                queuePosterDownload(m_items[i].poster);
            }
            break;
        }
        case 10: { // Reset All to Defaults
            m_settings = AppSettings();
            m_filterConfig = FilterConfig();
            m_addonManager.setFilterConfig(m_filterConfig);
            break;
        }
    }

    m_addonManager.setSettings(m_settings);
    applySettingsToPlayer();
}

void UiEngine::renderAddons() {
    drawRect(40, 90, 1200, 550, {22, 25, 36, 255});
    drawRect(40, 90, 1200, 50, {32, 38, 56, 255});
    drawText("INSTALLED ADDONS", 70, 102, {255, 255, 255, 255}, 24);

    // "+ ADD ADDON" Button
    drawRect(960, 98, 220, 36, {109, 93, 252, 255});
    drawCenteredText("+ ADD ADDON (X)", 960, 98, 220, 36, {255, 255, 255, 255}, 16);

    const auto& addons = m_addonManager.getAddons();
    if (addons.empty()) {
        drawText("No addons installed.", 70, 160, {200, 200, 220, 255}, 20);
        return;
    }

    int startY = 160;
    int itemH = 60;
    for (size_t i = 0; i < addons.size(); ++i) {
        int y = startY + static_cast<int>(i) * itemH;
        bool isSelected = (static_cast<int>(i) == m_selectedAddonIndex);

        drawRect(70, y, 1140, itemH - 10, isSelected ? SDL_Color{35, 42, 65, 255} : SDL_Color{28, 32, 45, 255});

        if (isSelected) {
            drawRect(68, y - 2, 1144, itemH - 6, {0, 212, 255, 255}, false);
        }

        drawText(addons[i].manifest.name, 90, y + 8, {255, 255, 255, 255}, 20);
        drawText(addons[i].manifest.description, 90, y + 30, {150, 155, 175, 255}, 14);

        // Status badge
        if (addons[i].enabled) {
            drawRect(1070, y + 10, 110, 30, {40, 120, 60, 255});
            drawCenteredText("ENABLED", 1070, y + 10, 110, 30, {255, 255, 255, 255}, 14);
        } else {
            drawRect(1070, y + 10, 110, 30, {90, 40, 40, 255});
            drawCenteredText("DISABLED", 1070, y + 10, 110, 30, {255, 255, 255, 255}, 14);
        }
    }
}

void UiEngine::renderPlayer() {
    // 1. Render decoded video frame
    if (m_videoTexture) {
        m_player.renderFrame(m_videoTexture);
        SDL_RenderCopy(m_renderer, m_videoTexture, nullptr, nullptr);
    }

    PlaybackState st = m_player.getState();

    // 2. Error message card if error occurred
    if (!st.errorMessage.empty()) {
        drawRect(240, 260, 800, 140, {25, 20, 28, 240});
        drawRect(240, 260, 800, 140, {255, 60, 60, 255}, false);
        drawCenteredText("Playback Error", 240, 275, 800, 30, {255, 80, 80, 255}, 22);
        drawCenteredText(st.errorMessage, 240, 310, 800, 30, {230, 230, 240, 255}, 18);
        drawCenteredText("Press (B) or tap ◀ to return to streams", 240, 350, 800, 30, {180, 185, 200, 255}, 16);
    }
    // 3. Buffering Indicator
    else if (st.isBuffering || (st.isPlaying && st.currentTime == 0.0 && st.duration == 0.0)) {
        int bufW = 260;
        int bufH = 46;
        int bufX = (1280 - bufW) / 2;
        int bufY = 320;
        drawRect(bufX, bufY, bufW, bufH, {20, 24, 36, 220});
        drawRect(bufX, bufY, bufW, bufH, {0, 212, 255, 255}, false);
        std::string bufText = "BUFFERING (" + std::to_string(st.cachePercent) + "%)...";
        drawCenteredText(bufText, bufX, bufY, bufW, bufH, {0, 212, 255, 255}, 18);
    }

    // 4. On-Screen Display (OSD) HUD
    if (m_showOsd) {
        // Top banner with Back button
        drawRect(0, 0, 1280, 80, {10, 12, 18, 220});
        drawRect(30, 20, 50, 40, {40, 45, 60, 255});
        drawCenteredText("◀", 30, 20, 50, 40, {255, 255, 255, 255}, 22);

        drawText(st.currentTitle, 100, 26, {255, 255, 255, 255}, 24);

        if (!st.hwdecCurrent.empty() && st.hwdecCurrent != "no") {
            drawRect(1110, 24, 130, 30, {35, 75, 45, 255});
            drawCenteredText("NVDEC HW", 1110, 24, 130, 30, {80, 240, 120, 255}, 14);
        }

        // Bottom HUD
        drawRect(0, 600, 1280, 120, {10, 12, 18, 220});

        auto formatTime = [](double sec) {
            int s = static_cast<int>(sec);
            int m = s / 60;
            int h = m / 60;
            std::ostringstream ss;
            if (h > 0) ss << h << ":" << std::setfill('0') << std::setw(2);
            ss << (m % 60) << ":" << std::setfill('0') << std::setw(2) << (s % 60);
            return ss.str();
        };

        std::string timeStr = formatTime(st.currentTime) + " / " + formatTime(st.duration);
        drawText(timeStr, 40, 620, {220, 220, 240, 255}, 20);

        if (st.isPaused) {
            drawText("PAUSED", 1190, 620, {230, 180, 50, 255}, 20, true);
        }

        // Timeline Scrubber
        int barX = 40;
        int barY = 655;
        int barW = 1200;
        int barH = 12;
        drawRect(barX, barY, barW, barH, {60, 65, 80, 255});

        if (st.duration > 0.0) {
            int filledW = static_cast<int>((st.currentTime / st.duration) * barW);
            filledW = std::clamp(filledW, 0, barW);
            drawRect(barX, barY, filledW, barH, {109, 93, 252, 255});
            drawRect(barX + filledW - 6, barY - 4, 12, 20, {0, 212, 255, 255});
        }
    }
}

void UiEngine::openSearch() {
    std::string query = requestTextInput("Search Movies & TV Shows");
    if (!query.empty()) {
        m_isLoading = true;
        auto movieAddon = m_addonManager.getCatalogAddon("movie");
        auto seriesAddon = m_addonManager.getCatalogAddon("series");

        if (movieAddon || seriesAddon) {
            m_searchQuery = query;
            m_isSearchActive = true;

            m_searchMovieResults = movieAddon ? StremioClient::searchCatalog(*movieAddon, "movie", query) : std::vector<MetaItem>{};
            m_searchSeriesResults = seriesAddon ? StremioClient::searchCatalog(*seriesAddon, "series", query) : std::vector<MetaItem>{};

            m_searchAllResults = m_searchMovieResults;
            m_searchAllResults.insert(m_searchAllResults.end(), m_searchSeriesResults.begin(), m_searchSeriesResults.end());

            setSearchTab(0); // Default to ALL tab
            m_state = UiState::CATALOG;
        }
        m_isLoading = false;
    }
}

void UiEngine::clearSearch() {
    m_isSearchActive = false;
    m_searchQuery = "";
    m_searchAllResults.clear();
    m_searchMovieResults.clear();
    m_searchSeriesResults.clear();
    m_page = 0;
    m_selectedGridIndex = 0;
    loadCatalogData();
}

void UiEngine::setSearchTab(int tab) {
    m_searchTab = tab;
    if (tab == 0) {
        m_items = m_searchAllResults;
    } else if (tab == 1) {
        m_items = m_searchMovieResults;
    } else if (tab == 2) {
        m_items = m_searchSeriesResults;
    }
    m_page = 0;
    m_selectedGridIndex = 0;

    int itemsPerPage = m_gridCols * m_gridRows;
    for (int i = 0; i < itemsPerPage && i < static_cast<int>(m_items.size()); ++i) {
        queuePosterDownload(m_items[i].poster);
    }
}

void UiEngine::openAddAddon() {
    std::string url = requestTextInput("Enter Stremio Addon URL", "https://");
    if (!url.empty()) {
        m_addonManager.addAddon(url);
    }
}

void UiEngine::playStream(const StreamItem& stream) {
    if (stream.url.empty()) {
        std::cerr << "[Playback] Stream has no direct URL (Torrent infoHash: " << stream.infoHash << ")" << std::endl;
        m_streamErrorMessage = "Direct playback requires an HTTPS stream or Debrid addon (e.g. Torrentio + Real-Debrid). Raw P2P torrents are not supported directly.";
        return;
    }

    m_streamErrorMessage = "";
    std::cout << "[Playback] Launching stream: " << stream.name << " URL: " << stream.url << std::endl;
    if (!m_player.isInitialized()) {
        std::cout << "[Playback] Initializing libmpv..." << std::endl;
        if (!m_player.initialize()) {
            std::cerr << "[Playback] Failed to initialize libmpv!" << std::endl;
            m_streamErrorMessage = "Failed to initialize libmpv video engine!";
            return;
        }
        applySettingsToPlayer();
    }

    m_player.play(stream.url, m_selectedMeta.name + " (" + stream.name + ")");
    m_state = UiState::PLAYER;
    m_lastPlayerInputTime = SDL_GetTicks();
    m_showOsd = true;
}

void UiEngine::applyStreamFilter() {
    m_availableStreams = StreamFilter::filterAndRank(m_rawStreams, m_filterConfig);
    if (m_selectedStreamIndex >= static_cast<int>(m_availableStreams.size())) {
        m_selectedStreamIndex = 0;
    }
}

void UiEngine::cycleFilterMode() {
    m_filterConfig.mode = StreamFilter::cycleMode(m_filterConfig.mode);
    m_addonManager.setFilterConfig(m_filterConfig);
    applyStreamFilter();
}

void UiEngine::editCustomRegex() {
    std::string newRegex = requestTextInput("Edit Exclude Regex", m_filterConfig.customExcludeRegex);
    if (!newRegex.empty()) {
        m_filterConfig.customExcludeRegex = newRegex;
        m_filterConfig.mode = StreamFilterMode::CUSTOM_REGEX;
        m_addonManager.setFilterConfig(m_filterConfig);
        applyStreamFilter();
    }
}

// ----------------- Touchscreen Tap Handler -----------------

void UiEngine::handleTouchTap(int x, int y) {
    std::cout << "[Touch] Tap at (" << x << ", " << y << ") in state " << static_cast<int>(m_state) << std::endl;

    // Header Taps (y: 0..70)
    if (y < 70 && m_state != UiState::PLAYER && m_state != UiState::STREAMS && m_state != UiState::CATEGORIES_MODAL) {
        if (!m_isSearchActive) {
            if (x >= 235 && x <= 330) { // Movies tab
                if (m_contentType != "movie" || m_state != UiState::CATALOG) {
                    m_contentType = "movie";
                    m_page = 0;
                    m_state = UiState::CATALOG;
                    loadCatalogData();
                }
                return;
            }
            if (x >= 340 && x <= 435) { // Series tab
                if (m_contentType != "series" || m_state != UiState::CATALOG) {
                    m_contentType = "series";
                    m_page = 0;
                    m_state = UiState::CATALOG;
                    loadCatalogData();
                }
                return;
            }
            if (x >= 445 && x <= 645) { // Categories pill
                m_modalCategoryIndex = m_selectedCategoryIndex;
                m_state = UiState::CATEGORIES_MODAL;
                return;
            }
            if (x >= 655 && x <= 780) { // Page indicator
                if (x < 715) onPagePrev();
                else onPageNext();
                return;
            }
            if (x >= 790 && x <= 930) { // Settings button
                m_state = (m_state == UiState::SETTINGS) ? UiState::CATALOG : UiState::SETTINGS;
                return;
            }
            if (x >= 940 && x <= 1060) { // Addons button
                m_state = (m_state == UiState::ADDONS) ? UiState::CATALOG : UiState::ADDONS;
                return;
            }
            if (x >= 1070 && x <= 1260) { // Search button
                openSearch();
                return;
            }
        } else {
            // Search mode tabs
            if (x >= 320 && x <= 440) { setSearchTab(0); return; }
            if (x >= 450 && x <= 590) { setSearchTab(1); return; }
            if (x >= 600 && x <= 750) { setSearchTab(2); return; }
            if (x >= 760 && x <= 870) { clearSearch(); return; }
            if (x >= 880 && x <= 990) {
                if (x < 935) onPagePrev(); else onPageNext();
                return;
            }
            if (x >= 1000 && x <= 1120) { m_state = UiState::SETTINGS; return; }
            if (x >= 1130 && x <= 1260) { m_state = UiState::ADDONS; return; }
        }
    }

    // State-specific touch handling
    if (m_state == UiState::CATALOG) {
        // Footer bar tap for Search
        if (y >= 660) {
            openSearch();
            return;
        }

        // Check if a card was tapped
        int startX = 40;
        int startY = 85;
        int cardW = 224;
        int cardH = 265;
        int gapX = 20;
        int gapY = 15;

        int itemsPerPage = m_gridCols * m_gridRows;
        int maxItemsOnPage = std::min(itemsPerPage, static_cast<int>(m_items.size()) - m_page * itemsPerPage);

        for (int i = 0; i < maxItemsOnPage; ++i) {
            int col = i % m_gridCols;
            int row = i / m_gridCols;
            int cx = startX + col * (cardW + gapX);
            int cy = startY + row * (cardH + gapY);

            if (x >= cx && x <= cx + cardW && y >= cy && y <= cy + cardH) {
                if (m_selectedGridIndex == i) {
                    onActionConfirm(); // Second tap opens details!
                } else {
                    m_selectedGridIndex = i; // First tap highlights card
                }
                return;
            }
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        int mw = 840;
        int mh = 490;
        int mx = (1280 - mw) / 2;
        int my = (720 - mh) / 2;

        // Close button (X) or outside modal
        if (x >= mx + mw - 50 && x <= mx + mw && y >= my && y <= my + 50) {
            m_state = UiState::CATALOG;
            return;
        }
        if (x < mx || x > mx + mw || y < my || y > my + mh) {
            m_state = UiState::CATALOG;
            return;
        }

        int cols = 4;
        int btnW = 180;
        int btnH = 68;
        int gapX = 18;
        int gapY = 18;
        int startX = mx + 30;
        int startY = my + 80;

        for (size_t i = 0; i < m_categories.size() && i < 16; ++i) {
            int c = static_cast<int>(i) % cols;
            int r = static_cast<int>(i) / cols;
            int bx = startX + c * (btnW + gapX);
            int by = startY + r * (btnH + gapY);

            if (x >= bx && x <= bx + btnW && y >= by && y <= by + btnH) {
                m_selectedCategoryIndex = static_cast<int>(i);
                m_page = 0;
                m_state = UiState::CATALOG;
                loadCatalogData();
                return;
            }
        }
    } else if (m_state == UiState::SETTINGS) {
        int panelW = 1200;
        int panelX = 40;
        int panelY = 90;

        // Back button
        if (x >= panelX + panelW - 130 && x <= panelX + panelW - 20 && y >= panelY + 9 && y <= panelY + 41) {
            m_state = UiState::CATALOG;
            return;
        }

        int listY = panelY + 58;
        int rowH = 41;
        for (int i = 0; i < 11; ++i) {
            int ry = listY + i * rowH;
            if (x >= panelX + 15 && x <= panelX + panelW - 15 && y >= ry && y <= ry + rowH) {
                m_selectedSettingIndex = i;
                changeSetting(1);
                return;
            }
        }
    } else if (m_state == UiState::DETAILS) {
        // "WATCH NOW" button
        if (x >= 360 && x <= 640 && y >= 475 && y <= 530) {
            onActionConfirm();
            return;
        }
        // "BACK" button
        if (x >= 660 && x <= 810 && y >= 475 && y <= 530) {
            onActionBack();
            return;
        }
        // Tapping outside panel
        if (x < 40 || x > 1240 || y < 90 || y > 640) {
            onActionBack();
            return;
        }
    } else if (m_state == UiState::STREAMS) {
        int mw = 900;
        int mh = 530;
        int mx = (1280 - mw) / 2;
        int my = (720 - mh) / 2;

        // Close button (X)
        if (x >= mx + mw - 50 && x <= mx + mw && y >= my && y <= my + 50) {
            onActionBack();
            return;
        }

        // Tap on Filter Button
        int fBtnX = mx + 350;
        int fBtnY = my + 9;
        int fBtnW = 270;
        int fBtnH = 34;
        if (x >= fBtnX && x <= fBtnX + fBtnW && y >= fBtnY && y <= fBtnY + fBtnH) {
            cycleFilterMode();
            return;
        }

        // Tap on Edit Regex Button
        if (m_filterConfig.mode == StreamFilterMode::CUSTOM_REGEX) {
            int eBtnX = fBtnX + fBtnW + 10;
            int eBtnY = fBtnY;
            int eBtnW = 150;
            int eBtnH = fBtnH;
            if (x >= eBtnX && x <= eBtnX + eBtnW && y >= eBtnY && y <= eBtnY + eBtnH) {
                editCustomRegex();
                return;
            }
        }

        // Tapping a stream item
        int startY = my + 78;
        int itemH = 68;
        for (size_t i = 0; i < 6 && i < m_availableStreams.size(); ++i) {
            int itemY = startY + static_cast<int>(i) * itemH;
            if (x >= mx + 20 && x <= mx + mw - 20 && y >= itemY && y <= itemY + itemH - 8) {
                m_selectedStreamIndex = static_cast<int>(i);
                playStream(m_availableStreams[i]);
                return;
            }
        }

        // Tapping outside modal closes it
        if (x < mx || x > mx + mw || y < my || y > my + mh) {
            onActionBack();
            return;
        }
    } else if (m_state == UiState::ADDONS) {
        // "+ ADD ADDON" button
        if (x >= 960 && x <= 1180 && y >= 98 && y <= 134) {
            openAddAddon();
            return;
        }

        // Tapping an addon row to toggle
        const auto& addons = m_addonManager.getAddons();
        int startY = 160;
        int itemH = 60;
        for (size_t i = 0; i < addons.size(); ++i) {
            int itemY = startY + static_cast<int>(i) * itemH;
            if (x >= 70 && x <= 1210 && y >= itemY && y <= itemY + itemH - 10) {
                m_selectedAddonIndex = static_cast<int>(i);
                m_addonManager.setAddonEnabled(addons[i].url, !addons[i].enabled);
                return;
            }
        }

        // Footer or outside
        if (y >= 660 || x < 40 || x > 1240) {
            onActionBack();
            return;
        }
    } else if (m_state == UiState::PLAYER) {
        PlaybackState st = m_player.getState();

        // Top-left Back button
        if (x >= 20 && x <= 90 && y >= 15 && y <= 65) {
            onActionBack();
            return;
        }

        // Timeline scrubber tap (x: 40..1240, y: 640..680)
        if (x >= 40 && x <= 1240 && y >= 640 && y <= 680 && st.duration > 0.0) {
            float pct = std::clamp(static_cast<float>(x - 40) / 1200.0f, 0.0f, 1.0f);
            m_player.seekAbsolute(pct * st.duration);
            m_lastPlayerInputTime = SDL_GetTicks();
            m_showOsd = true;
            return;
        }

        // Tap screen toggles play/pause & shows HUD
        m_player.togglePause();
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
}

// ----------------- Navigation Actions -----------------

void UiEngine::onNavigateLeft() {
    if (m_state == UiState::CATALOG) {
        if (m_selectedGridIndex % m_gridCols > 0) {
            m_selectedGridIndex--;
        } else if (m_page > 0) {
            onPagePrev();
            m_selectedGridIndex += (m_gridCols - 1);
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        m_modalCategoryIndex = (m_modalCategoryIndex - 1 + static_cast<int>(m_categories.size())) % static_cast<int>(m_categories.size());
    } else if (m_state == UiState::SETTINGS) {
        changeSetting(-1);
    } else if (m_state == UiState::PLAYER) {
        m_player.seekRelative(-10.0);
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
}

void UiEngine::onNavigateRight() {
    if (m_state == UiState::CATALOG) {
        int itemsPerPage = m_gridCols * m_gridRows;
        int maxItemsOnPage = std::min(itemsPerPage, static_cast<int>(m_items.size()) - m_page * itemsPerPage);

        if (m_selectedGridIndex % m_gridCols < m_gridCols - 1 && m_selectedGridIndex + 1 < maxItemsOnPage) {
            m_selectedGridIndex++;
        } else if ((m_page + 1) * itemsPerPage < static_cast<int>(m_items.size())) {
            onPageNext();
            m_selectedGridIndex -= (m_gridCols - 1);
            if (m_selectedGridIndex < 0) m_selectedGridIndex = 0;
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        m_modalCategoryIndex = (m_modalCategoryIndex + 1) % static_cast<int>(m_categories.size());
    } else if (m_state == UiState::SETTINGS) {
        changeSetting(1);
    } else if (m_state == UiState::PLAYER) {
        m_player.seekRelative(10.0);
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
}

void UiEngine::onNavigateUp() {
    if (m_state == UiState::CATALOG) {
        if (m_selectedGridIndex >= m_gridCols) {
            m_selectedGridIndex -= m_gridCols;
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        m_modalCategoryIndex = (m_modalCategoryIndex - 4 + static_cast<int>(m_categories.size())) % static_cast<int>(m_categories.size());
    } else if (m_state == UiState::SETTINGS) {
        if (m_selectedSettingIndex > 0) m_selectedSettingIndex--;
    } else if (m_state == UiState::STREAMS) {
        if (m_selectedStreamIndex > 0) {
            m_selectedStreamIndex--;
        }
    } else if (m_state == UiState::ADDONS) {
        if (m_selectedAddonIndex > 0) {
            m_selectedAddonIndex--;
        }
    } else if (m_state == UiState::PLAYER) {
        m_player.seekRelative(60.0);
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
}

void UiEngine::onNavigateDown() {
    if (m_state == UiState::CATALOG) {
        int itemsPerPage = m_gridCols * m_gridRows;
        int maxItemsOnPage = std::min(itemsPerPage, static_cast<int>(m_items.size()) - m_page * itemsPerPage);
        if (m_selectedGridIndex + m_gridCols < maxItemsOnPage) {
            m_selectedGridIndex += m_gridCols;
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        m_modalCategoryIndex = (m_modalCategoryIndex + 4) % static_cast<int>(m_categories.size());
    } else if (m_state == UiState::SETTINGS) {
        if (m_selectedSettingIndex < 10) m_selectedSettingIndex++;
    } else if (m_state == UiState::STREAMS) {
        if (m_selectedStreamIndex + 1 < static_cast<int>(m_availableStreams.size())) {
            m_selectedStreamIndex++;
        }
    } else if (m_state == UiState::ADDONS) {
        if (m_selectedAddonIndex + 1 < static_cast<int>(m_addonManager.getAddons().size())) {
            m_selectedAddonIndex++;
        }
    } else if (m_state == UiState::PLAYER) {
        m_player.seekRelative(-60.0);
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
}

void UiEngine::onPagePrev() {
    if (m_state == UiState::CATALOG) {
        if (m_isSearchActive) {
            setSearchTab((m_searchTab + 2) % 3);
            return;
        }
        if (m_page > 0) {
            m_page--;
            m_selectedGridIndex = 0;
            int itemsPerPage = m_gridCols * m_gridRows;
            int startIndex = m_page * itemsPerPage;
            for (int i = 0; i < itemsPerPage && (startIndex + i) < static_cast<int>(m_items.size()); ++i) {
                queuePosterDownload(m_items[startIndex + i].poster);
            }
        }
    } else if (m_state == UiState::SETTINGS) {
        changeSetting(-1);
    }
}

void UiEngine::onPageNext() {
    if (m_state == UiState::CATALOG) {
        if (m_isSearchActive) {
            setSearchTab((m_searchTab + 1) % 3);
            return;
        }
        int itemsPerPage = m_gridCols * m_gridRows;
        if ((m_page + 1) * itemsPerPage < static_cast<int>(m_items.size())) {
            m_page++;
            m_selectedGridIndex = 0;
            int startIndex = m_page * itemsPerPage;
            for (int i = 0; i < itemsPerPage && (startIndex + i) < static_cast<int>(m_items.size()); ++i) {
                queuePosterDownload(m_items[startIndex + i].poster);
            }
        }
    } else if (m_state == UiState::SETTINGS) {
        changeSetting(1);
    }
}

void UiEngine::onActionConfirm() {
    if (m_state == UiState::CATALOG && !m_items.empty()) {
        int idx = m_page * (m_gridCols * m_gridRows) + m_selectedGridIndex;
        if (idx < static_cast<int>(m_items.size())) {
            auto catAddon = m_addonManager.getCatalogAddon(m_items[idx].type);
            if (!catAddon) catAddon = m_addonManager.getCatalogAddon("movie");

            if (catAddon) {
                auto fullMeta = StremioClient::fetchMeta(*catAddon, m_items[idx].type, m_items[idx].id);
                m_selectedMeta = fullMeta ? *fullMeta : m_items[idx];
            } else {
                m_selectedMeta = m_items[idx];
            }
            queuePosterDownload(m_selectedMeta.poster);
            m_state = UiState::DETAILS;
        }
    } else if (m_state == UiState::CATEGORIES_MODAL) {
        m_selectedCategoryIndex = m_modalCategoryIndex;
        m_page = 0;
        m_state = UiState::CATALOG;
        loadCatalogData();
    } else if (m_state == UiState::SETTINGS) {
        changeSetting(1);
    } else if (m_state == UiState::DETAILS) {
        m_rawStreams = m_addonManager.aggregateStreams(m_selectedMeta.type, m_selectedMeta.id);
        applyStreamFilter();
        m_selectedStreamIndex = 0;
        m_state = UiState::STREAMS;
    } else if (m_state == UiState::STREAMS && !m_availableStreams.empty()) {
        playStream(m_availableStreams[m_selectedStreamIndex]);
    } else if (m_state == UiState::ADDONS) {
        const auto& addons = m_addonManager.getAddons();
        if (m_selectedAddonIndex < static_cast<int>(addons.size())) {
            m_addonManager.setAddonEnabled(addons[m_selectedAddonIndex].url, !addons[m_selectedAddonIndex].enabled);
        }
    } else if (m_state == UiState::PLAYER) {
        m_player.togglePause();
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
}

void UiEngine::onActionBack() {
    if (m_state == UiState::CATEGORIES_MODAL) {
        m_state = UiState::CATALOG;
    } else if (m_state == UiState::SETTINGS) {
        m_state = UiState::CATALOG;
    } else if (m_state == UiState::STREAMS) {
        m_state = UiState::DETAILS;
    } else if (m_state == UiState::DETAILS || m_state == UiState::ADDONS) {
        m_state = UiState::CATALOG;
    } else if (m_state == UiState::CATALOG && m_isSearchActive) {
        clearSearch();
    } else if (m_state == UiState::PLAYER) {
        m_player.stop();
        m_state = UiState::DETAILS;
    }
}

void UiEngine::onActionSearch() {
    if (m_state == UiState::STREAMS) {
        editCustomRegex();
    } else if (m_state == UiState::ADDONS) {
        openAddAddon();
    } else if (m_state == UiState::SETTINGS) {
        if (m_selectedSettingIndex == 8) editCustomRegex();
    } else {
        openSearch();
    }
}

void UiEngine::onActionToggleType() {
    if (m_state == UiState::STREAMS) {
        cycleFilterMode();
    } else if (m_state == UiState::CATALOG) {
        if (m_isSearchActive) {
            setSearchTab((m_searchTab + 1) % 3);
        } else {
            m_contentType = (m_contentType == "movie") ? "series" : "movie";
            m_page = 0;
            loadCatalogData();
        }
    }
}

void UiEngine::onActionMenu() {
    if (m_state == UiState::CATALOG) {
        m_state = UiState::SETTINGS;
    } else if (m_state == UiState::SETTINGS) {
        m_state = UiState::ADDONS;
    } else {
        m_state = UiState::CATALOG;
    }
}

void UiEngine::onActionExit() {
    if (m_state == UiState::PLAYER) {
        m_showOsd = !m_showOsd;
    } else {
        m_running = false;
    }
}

void UiEngine::handleInput() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            m_running = false;
            return;
        }

        // Touch Screen input (Switch capacitive touch)
        if (event.type == SDL_FINGERDOWN) {
            uint32_t now = SDL_GetTicks();
            if (now - m_lastTouchTime > 120) {
                m_lastTouchTime = now;
                int touchX = static_cast<int>(event.tfinger.x * 1280.0f);
                int touchY = static_cast<int>(event.tfinger.y * 720.0f);
                handleTouchTap(touchX, touchY);
            }
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                uint32_t now = SDL_GetTicks();
                if (now - m_lastTouchTime > 120) {
                    m_lastTouchTime = now;
                    handleTouchTap(event.button.x, event.button.y);
                }
            }
        }
        // Keyboard Navigation (Desktop & USB Keyboard)
        else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_LEFT:      onNavigateLeft(); break;
                case SDLK_RIGHT:     onNavigateRight(); break;
                case SDLK_UP:        onNavigateUp(); break;
                case SDLK_DOWN:      onNavigateDown(); break;
                case SDLK_PAGEUP:    onPagePrev(); break;
                case SDLK_PAGEDOWN:  onPageNext(); break;
                case SDLK_RETURN:
                case SDLK_SPACE:     onActionConfirm(); break;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE: onActionBack(); break;
                case SDLK_x:         onActionSearch(); break;
                case SDLK_y:         onActionToggleType(); break;
                case SDLK_c:
                    if (m_state == UiState::CATALOG) {
                        m_modalCategoryIndex = m_selectedCategoryIndex;
                        m_state = UiState::CATEGORIES_MODAL;
                    }
                    break;
                case SDLK_s:
                    m_state = (m_state == UiState::SETTINGS) ? UiState::CATALOG : UiState::SETTINGS;
                    break;
                case SDLK_MINUS:     onActionMenu(); break;
                case SDLK_PLUS:
                case SDLK_EQUALS:    onActionExit(); break;
            }
        }
        // Nintendo Switch Joy-Con & Pro Controller mapping
        else if (event.type == SDL_JOYBUTTONDOWN) {
            std::cout << "[Input] JoyButtonDown: " << static_cast<int>(event.jbutton.button) << std::endl;
            switch (event.jbutton.button) {
                case 0:  // A
                    onActionConfirm(); break;
                case 1:  // B
                    onActionBack(); break;
                case 2:  // X
                    onActionSearch(); break;
                case 3:  // Y
                    onActionToggleType(); break;
                case 6:  // L
                    onPagePrev(); break;
                case 7:  // R
                    onPageNext(); break;
                case 8:  // ZL
                    if (m_state == UiState::CATALOG) {
                        m_modalCategoryIndex = m_selectedCategoryIndex;
                        m_state = UiState::CATEGORIES_MODAL;
                    }
                    break;
                case 10: // Plus
                    onActionExit(); break;
                case 11: // Minus
                    onActionMenu(); break;
                case 12: // D-Pad Left
                case 16: // Left Stick Left
                    onNavigateLeft(); break;
                case 13: // D-Pad Up
                case 17: // Left Stick Up
                    onNavigateUp(); break;
                case 14: // D-Pad Right
                case 18: // Left Stick Right
                    onNavigateRight(); break;
                case 15: // D-Pad Down
                case 19: // Left Stick Down
                    onNavigateDown(); break;
            }
        }
        // D-Pad Hat Motion (Third-party controllers)
        else if (event.type == SDL_JOYHATMOTION) {
            if (event.jhat.value & SDL_HAT_LEFT)  onNavigateLeft();
            if (event.jhat.value & SDL_HAT_RIGHT) onNavigateRight();
            if (event.jhat.value & SDL_HAT_UP)    onNavigateUp();
            if (event.jhat.value & SDL_HAT_DOWN)  onNavigateDown();
        }
        // Analog Stick Axes Motion
        else if (event.type == SDL_JOYAXISMOTION) {
            uint32_t now = SDL_GetTicks();
            if (now - m_lastStickNavTime > 220) {
                if (event.jaxis.axis == 0) {
                    if (event.jaxis.value < -16000) {
                        onNavigateLeft();
                        m_lastStickNavTime = now;
                    } else if (event.jaxis.value > 16000) {
                        onNavigateRight();
                        m_lastStickNavTime = now;
                    }
                } else if (event.jaxis.axis == 1) {
                    if (event.jaxis.value < -16000) {
                        onNavigateUp();
                        m_lastStickNavTime = now;
                    } else if (event.jaxis.value > 16000) {
                        onNavigateDown();
                        m_lastStickNavTime = now;
                    }
                }
            }
        }
    }
}

void UiEngine::update() {
    processDownloadedPosters();

    if (m_state == UiState::PLAYER && m_showOsd) {
        if (SDL_GetTicks() - m_lastPlayerInputTime > 4000) {
            m_showOsd = false;
        }
    }
}

void UiEngine::render() {
    if (!m_renderer) return;

    if (m_state != UiState::PLAYER) {
        SDL_SetRenderDrawColor(m_renderer, 15, 16, 22, 255);
        SDL_RenderClear(m_renderer);
    }

    switch (m_state) {
        case UiState::CATALOG:
            drawHeader();
            renderCatalog();
            drawFooter();
            break;
        case UiState::DETAILS:
            drawHeader();
            renderDetails();
            drawFooter();
            break;
        case UiState::STREAMS:
            drawHeader();
            renderDetails();
            renderStreams();
            drawFooter();
            break;
        case UiState::CATEGORIES_MODAL:
            drawHeader();
            renderCatalog();
            renderCategoriesModal();
            drawFooter();
            break;
        case UiState::SETTINGS:
            renderSettings();
            drawFooter();
            break;
        case UiState::ADDONS:
            drawHeader();
            renderAddons();
            drawFooter();
            break;
        case UiState::PLAYER:
            renderPlayer();
            break;
    }

    SDL_RenderPresent(m_renderer);
}

void UiEngine::run() {
    while (m_running) {
        handleInput();
        update();
        render();
        SDL_Delay(16);
    }
}

} // namespace Stremio
