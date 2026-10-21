#include "stremio/ui_engine.hpp"
#include "stremio/stremio_client.hpp"
#include "stremio/http_client.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <chrono>
#include <algorithm>

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

    // Load installed addons & filter settings
    std::cout << "[Config] Loading addon configurations..." << std::endl;
    m_addonManager.loadConfig();
    m_filterConfig = m_addonManager.getFilterConfig();
    std::cout << "[Config] Active addons: " << m_addonManager.getAddons().size()
              << " Filter: " << StreamFilter::getModeName(m_filterConfig.mode) << std::endl;

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

    IMG_Quit();
    TTF_Quit();

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
    m_isLoading = true;
    m_items.clear();

    try {
        auto catalogAddon = m_addonManager.getCatalogAddon(m_contentType);
        if (catalogAddon) {
            std::cout << "[Catalog] Requesting " << m_contentType << " from " << catalogAddon->name << "..." << std::endl;
            m_items = StremioClient::fetchCatalog(*catalogAddon, m_contentType, "top");
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
    if (fontSize <= 18) font = m_fontSmall;
    else if (fontSize >= 28) font = m_fontLarge;

    if (!font) return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        int textX = rightAlign ? (x - surface->w) : x;
        SDL_Rect dst = {textX, y, surface->w, surface->h};
        SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

void UiEngine::drawCenteredText(const std::string& text, int x, int y, int w, int h, SDL_Color color, int fontSize) {
    if (!m_renderer || text.empty()) return;

    TTF_Font* font = m_fontMedium;
    if (fontSize <= 18) font = m_fontSmall;
    else if (fontSize >= 28) font = m_fontLarge;

    if (!font) return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        int textX = x + (w - surface->w) / 2;
        int textY = y + (h - surface->h) / 2;
        SDL_Rect dst = {textX, textY, surface->w, surface->h};
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

    // Logo
    drawText("JOYSTREAM", 40, 18, {109, 93, 252, 255}, 32);

    // Clickable Tabs
    bool isMovie = (m_contentType == "movie");
    SDL_Color movieColor = isMovie ? SDL_Color{0, 212, 255, 255} : SDL_Color{140, 145, 170, 255};
    SDL_Color seriesColor = !isMovie ? SDL_Color{0, 212, 255, 255} : SDL_Color{140, 145, 170, 255};

    drawRect(290, 16, 120, 38, isMovie ? SDL_Color{35, 42, 65, 255} : SDL_Color{22, 25, 36, 255});
    drawCenteredText("Movies", 290, 16, 120, 38, movieColor, 20);

    drawRect(420, 16, 120, 38, !isMovie ? SDL_Color{35, 42, 65, 255} : SDL_Color{22, 25, 36, 255});
    drawCenteredText("Series", 420, 16, 120, 38, seriesColor, 20);

    // Page indicator
    int itemsPerPage = m_gridCols * m_gridRows;
    int totalPages = std::max(1, static_cast<int>(std::ceil(static_cast<double>(m_items.size()) / itemsPerPage)));
    std::string pageInfo = "◀  Page " + std::to_string(m_page + 1) + " / " + std::to_string(totalPages) + "  ▶";
    drawText(pageInfo, 820, 24, {180, 185, 205, 255}, 18);

    // Addons Button
    drawRect(1080, 16, 160, 38, {30, 35, 52, 255});
    std::string addonInfo = std::to_string(m_addonManager.getAddons().size()) + " Addons (-)";
    drawCenteredText(addonInfo, 1080, 16, 160, 38, {200, 205, 230, 255}, 18);
}

void UiEngine::drawFooter() {
    drawRect(0, 660, 1280, 60, {18, 20, 29, 255});
    drawRect(0, 660, 1280, 2, {50, 54, 75, 255});

    std::string hints;
    if (m_state == UiState::CATALOG) {
        hints = "(Touch or D-Pad) Select   (A) Details   (L/R) Page   (X) Search   (Y) Type   (-) Addons";
    } else if (m_state == UiState::DETAILS) {
        hints = "(A / Touch) Watch Streams   (B) Back to Catalog";
    } else if (m_state == UiState::STREAMS) {
        hints = "(Touch or D-Pad) Select Stream   (A) Play   (B) Cancel";
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
        drawText("No catalog items loaded.", 500, 300, {230, 100, 100, 255}, 24);
        drawText("Check Wi-Fi connection, or tap (-) to view Addons.", 410, 350, {180, 185, 205, 255}, 20);
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

        // Poster backdrop box
        drawRect(pBoxX, pBoxY, pBoxW, pBoxH, {16, 18, 25, 255});

        bool posterRendered = false;
        if (!item.poster.empty()) {
            auto it = m_posterTextures.find(item.poster);
            if (it != m_posterTextures.end() && it->second) {
                // Aspect-ratio fitted rendering
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

    // Action Button 1: "▶ WATCH NOW" (wide enough with generous padding)
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

    // Action Button 2: "BACK" (Touch/Clickable)
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

void UiEngine::renderAddons() {
    drawRect(40, 90, 1200, 550, {22, 25, 36, 255});
    drawText("INSTALLED STREMIO ADDONS", 70, 115, {255, 255, 255, 255}, 28);
    drawText("(Tap or press X to add a custom addon URL, e.g. Torrentio / RealDebrid)", 70, 155, {160, 165, 185, 255}, 18);

    // "+ Add Addon" touch button
    int addBtnX = 960;
    int addBtnY = 110;
    int addBtnW = 220;
    int addBtnH = 42;
    drawRect(addBtnX, addBtnY, addBtnW, addBtnH, {109, 93, 252, 255});
    drawCenteredText("+ ADD ADDON (X)", addBtnX, addBtnY, addBtnW, addBtnH, {255, 255, 255, 255}, 18);

    const auto& addons = m_addonManager.getAddons();
    int startY = 195;
    int itemH = 60;

    for (size_t i = 0; i < addons.size(); ++i) {
        int y = startY + static_cast<int>(i) * itemH;
        bool isSelected = (static_cast<int>(i) == m_selectedAddonIndex);

        SDL_Color bg = isSelected ? SDL_Color{40, 46, 68, 255} : SDL_Color{28, 32, 45, 255};
        drawRect(70, y, 1140, itemH - 10, bg);

        if (isSelected) {
            drawRect(68, y - 2, 1144, itemH - 6, {109, 93, 252, 255}, false);
        }

        std::string status = addons[i].enabled ? "[ENABLED]" : "[DISABLED]";
        SDL_Color statusCol = addons[i].enabled ? SDL_Color{46, 204, 113, 255} : SDL_Color{231, 76, 60, 255};

        drawText(status, 90, y + 14, statusCol, 18);
        drawText(addons[i].manifest.name + " (" + addons[i].manifest.version + ")", 230, y + 14, {255, 255, 255, 255}, 20);
        drawText(addons[i].url, 700, y + 16, {140, 145, 170, 255}, 16);
    }
}

void UiEngine::renderPlayer() {
    PlaybackState st = m_player.getState();

    if (m_showOsd) {
        // Top banner with Back button
        drawRect(0, 0, 1280, 80, {10, 12, 18, 220});
        drawRect(30, 20, 50, 40, {40, 45, 60, 255});
        drawCenteredText("◀", 30, 20, 50, 40, {255, 255, 255, 255}, 22);

        drawText(st.currentTitle, 100, 26, {255, 255, 255, 255}, 26);

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
            drawText("PAUSED", 1200, 620, {230, 180, 50, 255}, 20, true);
        }

        // Timeline Scrubber
        int barX = 40;
        int barY = 655;
        int barW = 1200;
        int barH = 12;
        drawRect(barX, barY, barW, barH, {60, 65, 80, 255});

        if (st.duration > 0.0) {
            int filledW = static_cast<int>((st.currentTime / st.duration) * barW);
            drawRect(barX, barY, filledW, barH, {109, 93, 252, 255});
            drawRect(barX + filledW - 6, barY - 4, 12, 20, {0, 212, 255, 255});
        }
    }
}

void UiEngine::openSearch() {
    std::string query = requestTextInput("Search Movies & Series");
    if (!query.empty()) {
        m_isLoading = true;
        auto catalogAddon = m_addonManager.getCatalogAddon(m_contentType);
        if (catalogAddon) {
            m_items = StremioClient::searchCatalog(*catalogAddon, m_contentType, query);
            m_selectedGridIndex = 0;
            m_page = 0;
            m_state = UiState::CATALOG;

            int itemsPerPage = m_gridCols * m_gridRows;
            for (int i = 0; i < itemsPerPage && i < static_cast<int>(m_items.size()); ++i) {
                queuePosterDownload(m_items[i].poster);
            }
        }
        m_isLoading = false;
    }
}

void UiEngine::openAddAddon() {
    std::string url = requestTextInput("Enter Stremio Addon URL", "https://");
    if (!url.empty()) {
        m_addonManager.addAddon(url);
    }
}

void UiEngine::playStream(const StreamItem& stream) {
    if (!stream.url.empty()) {
        std::cout << "[Playback] Launching stream: " << stream.name << " URL: " << stream.url << std::endl;
        if (!m_player.isInitialized()) {
            std::cout << "[Playback] Initializing libmpv..." << std::endl;
            if (!m_player.initialize()) {
                std::cerr << "[Playback] Failed to initialize libmpv!" << std::endl;
                return;
            }
        }
        m_player.play(stream.url, m_selectedMeta.name + " (" + stream.name + ")");
        m_state = UiState::PLAYER;
        m_lastPlayerInputTime = SDL_GetTicks();
        m_showOsd = true;
    }
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
    if (y < 70) {
        if (x >= 290 && x <= 410) { // Movies tab
            if (m_contentType != "movie") {
                m_contentType = "movie";
                m_page = 0;
                loadCatalogData();
                m_state = UiState::CATALOG;
            }
            return;
        }
        if (x >= 420 && x <= 540) { // Series tab
            if (m_contentType != "series") {
                m_contentType = "series";
                m_page = 0;
                loadCatalogData();
                m_state = UiState::CATALOG;
            }
            return;
        }
        if (x >= 800 && x <= 950) { // Page indicator
            if (x < 875) onPagePrev();
            else onPageNext();
            return;
        }
        if (x >= 1080 && x <= 1260) { // Addons button
            onActionMenu();
            return;
        }
    }

    // State-specific touch handling
    if (m_state == UiState::CATALOG) {
        // Footer bar tap for Search
        if (y >= 660) {
            onActionSearch();
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
        if (x >= 960 && x <= 1180 && y >= 110 && y <= 155) {
            openAddAddon();
            return;
        }

        // Tapping an addon row to toggle
        const auto& addons = m_addonManager.getAddons();
        int startY = 195;
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
    if (m_state == UiState::CATALOG && m_page > 0) {
        m_page--;
        m_selectedGridIndex = 0;
        int itemsPerPage = m_gridCols * m_gridRows;
        int startIndex = m_page * itemsPerPage;
        for (int i = 0; i < itemsPerPage && (startIndex + i) < static_cast<int>(m_items.size()); ++i) {
            queuePosterDownload(m_items[startIndex + i].poster);
        }
    }
}

void UiEngine::onPageNext() {
    if (m_state == UiState::CATALOG) {
        int itemsPerPage = m_gridCols * m_gridRows;
        if ((m_page + 1) * itemsPerPage < static_cast<int>(m_items.size())) {
            m_page++;
            m_selectedGridIndex = 0;
            int startIndex = m_page * itemsPerPage;
            for (int i = 0; i < itemsPerPage && (startIndex + i) < static_cast<int>(m_items.size()); ++i) {
                queuePosterDownload(m_items[startIndex + i].poster);
            }
        }
    }
}

void UiEngine::onActionConfirm() {
    if (m_state == UiState::CATALOG && !m_items.empty()) {
        int idx = m_page * (m_gridCols * m_gridRows) + m_selectedGridIndex;
        if (idx < static_cast<int>(m_items.size())) {
            auto catAddon = m_addonManager.getCatalogAddon(m_contentType);
            if (catAddon) {
                auto fullMeta = StremioClient::fetchMeta(*catAddon, m_contentType, m_items[idx].id);
                m_selectedMeta = fullMeta ? *fullMeta : m_items[idx];
            } else {
                m_selectedMeta = m_items[idx];
            }
            queuePosterDownload(m_selectedMeta.poster);
            m_state = UiState::DETAILS;
        }
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
    if (m_state == UiState::STREAMS) {
        m_state = UiState::DETAILS;
    } else if (m_state == UiState::DETAILS || m_state == UiState::ADDONS) {
        m_state = UiState::CATALOG;
    } else if (m_state == UiState::PLAYER) {
        m_player.stop();
        m_player.shutdown();
        m_state = UiState::DETAILS;
    }
}

void UiEngine::onActionSearch() {
    if (m_state == UiState::STREAMS) {
        editCustomRegex();
    } else if (m_state == UiState::ADDONS) {
        openAddAddon();
    } else {
        openSearch();
    }
}

void UiEngine::onActionToggleType() {
    if (m_state == UiState::STREAMS) {
        cycleFilterMode();
    } else if (m_state == UiState::CATALOG) {
        m_contentType = (m_contentType == "movie") ? "series" : "movie";
        m_page = 0;
        loadCatalogData();
    }
}

void UiEngine::onActionMenu() {
    m_state = (m_state == UiState::ADDONS) ? UiState::CATALOG : UiState::ADDONS;
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

    SDL_SetRenderDrawColor(m_renderer, 15, 16, 22, 255);
    SDL_RenderClear(m_renderer);

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
