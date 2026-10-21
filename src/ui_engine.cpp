#include "stremio/ui_engine.hpp"
#include "stremio/stremio_client.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
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
            m_fontSmall = TTF_OpenFontRW(SDL_RWFromMem(sharedFont.address, sharedFont.size), 0, 18);
            m_fontMedium = TTF_OpenFontRW(SDL_RWFromMem(sharedFont.address, sharedFont.size), 0, 24);
            m_fontLarge = TTF_OpenFontRW(SDL_RWFromMem(sharedFont.address, sharedFont.size), 0, 36);
        }
    }
#else
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf"
    };
    for (const char* p : fontPaths) {
        m_fontSmall = TTF_OpenFont(p, 18);
        if (m_fontSmall) {
            m_fontMedium = TTF_OpenFont(p, 24);
            m_fontLarge = TTF_OpenFont(p, 36);
            break;
        }
    }
#endif

    std::cout << "[Font] Font status: small=" << (m_fontSmall != nullptr)
              << " medium=" << (m_fontMedium != nullptr)
              << " large=" << (m_fontLarge != nullptr) << std::endl;

    // Load installed addons
    std::cout << "[Config] Loading addon configurations..." << std::endl;
    m_addonManager.loadConfig();
    std::cout << "[Config] Active addons: " << m_addonManager.getAddons().size() << std::endl;

    // Do NOT initialize player here: initialize lazily on playback to avoid GPU/audio conflict!

    // Fetch initial catalog
    loadCatalogData();

    m_running = true;
    return true;
}

void UiEngine::shutdown() {
    m_player.shutdown();

    if (m_fontSmall) TTF_CloseFont(m_fontSmall);
    if (m_fontMedium) TTF_CloseFont(m_fontMedium);
    if (m_fontLarge) TTF_CloseFont(m_fontLarge);
    m_fontSmall = m_fontMedium = m_fontLarge = nullptr;

#ifdef __SWITCH__
    plExit();
#endif

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
    else if (fontSize >= 32) font = m_fontLarge;

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

void UiEngine::drawHeader() {
    // Header Bar
    drawRect(0, 0, 1280, 70, {18, 20, 29, 255});
    drawRect(0, 68, 1280, 2, {50, 54, 75, 255});

    // Logo / Title
    drawText("JOYSTREAM", 40, 18, {109, 93, 252, 255}, 32);

    // Active Tab
    std::string tab = (m_contentType == "movie") ? "[ Movies ]   Series" : "  Movies   [ Series ]";
    drawText(tab, 300, 24, {220, 220, 240, 255}, 20);

    // Addons Status
    std::string addonInfo = std::to_string(m_addonManager.getAddons().size()) + " Addons Active";
    drawText(addonInfo, 1240, 24, {140, 145, 170, 255}, 18, true);
}

void UiEngine::drawFooter() {
    // Footer Bar
    drawRect(0, 660, 1280, 60, {18, 20, 29, 255});
    drawRect(0, 660, 1280, 2, {50, 54, 75, 255});

    std::string hints;
    if (m_state == UiState::CATALOG) {
        hints = "(A) Select   (X) Search   (Y) Switch Type   (-) Addons   (+) Exit";
    } else if (m_state == UiState::DETAILS) {
        hints = "(A) Find Streams   (B) Back to Catalog";
    } else if (m_state == UiState::STREAMS) {
        hints = "(A) Play Stream   (B) Cancel";
    } else if (m_state == UiState::ADDONS) {
        hints = "(A) Toggle Addon   (X) Add New Addon   (B) Back";
    } else if (m_state == UiState::PLAYER) {
        hints = "(A) Play/Pause   (B) Stop   (D-Pad) Seek   (+) OSD";
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
        drawText("Check Wi-Fi connection, or press (-) to view Addons.", 410, 350, {180, 185, 205, 255}, 20);
        return;
    }

    int startX = 40;
    int startY = 95;
    int cardW = 280;
    int cardH = 260;
    int gapX = 20;
    int gapY = 20;

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
        SDL_Color bg = isSelected ? SDL_Color{35, 40, 60, 255} : SDL_Color{24, 27, 38, 255};
        drawRect(x, y, cardW, cardH, bg);

        // Selection Border
        if (isSelected) {
            drawRect(x - 2, y - 2, cardW + 4, cardH + 4, {109, 93, 252, 255}, false);
            drawRect(x - 1, y - 1, cardW + 2, cardH + 2, {0, 212, 255, 255}, false);
        }

        // Poster Placeholder
        drawRect(x + 10, y + 10, cardW - 20, 150, {15, 17, 24, 255});
        drawText("POSTER", x + cardW / 2, y + 70, {70, 75, 95, 255}, 18, true);

        // Movie Title
        std::string title = item.name;
        if (title.length() > 22) title = title.substr(0, 20) + "..";
        drawText(title, x + 15, y + 172, {240, 240, 255, 255}, 20);

        // Year and IMDb Rating
        std::string metaStr = item.releaseInfo;
        if (!item.imdbRating.empty()) {
            metaStr += "  * " + item.imdbRating;
        }
        drawText(metaStr, x + 15, y + 205, {230, 180, 50, 255}, 18);

        // Type Badge
        drawText("[" + item.type + "]", x + 15, y + 230, {130, 135, 160, 255}, 14);
    }
}

void UiEngine::renderDetails() {
    // Backdrop Panel
    drawRect(40, 90, 1200, 550, {22, 25, 36, 255});

    // Poster Box
    drawRect(70, 120, 260, 390, {15, 17, 24, 255});
    drawText("POSTER PREVIEW", 130, 300, {90, 95, 120, 255}, 18);

    // Title & Info
    drawText(m_selectedMeta.name, 360, 130, {255, 255, 255, 255}, 32);

    std::string sub = m_selectedMeta.releaseInfo + "  |  IMDb: " + (m_selectedMeta.imdbRating.empty() ? "N/A" : m_selectedMeta.imdbRating);
    drawText(sub, 360, 180, {230, 180, 50, 255}, 20);

    // Genres
    std::string genreStr = "Genres: ";
    for (size_t i = 0; i < m_selectedMeta.genres.size(); ++i) {
        genreStr += m_selectedMeta.genres[i] + (i + 1 < m_selectedMeta.genres.size() ? ", " : "");
    }
    drawText(genreStr, 360, 215, {160, 170, 200, 255}, 18);

    // Synopsis
    drawText("Synopsis:", 360, 260, {200, 200, 220, 255}, 20);
    std::string desc = m_selectedMeta.description;
    size_t lineLen = 65;
    for (size_t i = 0; i < 4 && i * lineLen < desc.length(); ++i) {
        std::string chunk = desc.substr(i * lineLen, lineLen);
        drawText(chunk, 360, 295 + static_cast<int>(i) * 26, {180, 185, 205, 255}, 18);
    }

    if (!m_selectedMeta.videos.empty()) {
        std::string epCount = "Episodes Available: " + std::to_string(m_selectedMeta.videos.size());
        drawText(epCount, 360, 420, {0, 212, 255, 255}, 18);
    }

    // Action Button
    bool btnFocus = (m_detailsActionIndex == 0);
    SDL_Color btnBg = btnFocus ? SDL_Color{109, 93, 252, 255} : SDL_Color{50, 55, 75, 255};
    drawRect(360, 470, 260, 55, btnBg);
    if (btnFocus) {
        drawRect(358, 468, 264, 59, {0, 212, 255, 255}, false);
    }
    drawText("▶  WATCH / STREAMS", 390, 484, {255, 255, 255, 255}, 20);
}

void UiEngine::renderStreams() {
    drawRect(0, 0, 1280, 720, {10, 10, 15, 200});

    int mw = 840;
    int mh = 500;
    int mx = (1280 - mw) / 2;
    int my = (720 - mh) / 2;

    drawRect(mx, my, mw, mh, {24, 27, 38, 255});
    drawRect(mx, my, mw, 50, {35, 40, 58, 255});
    drawText("AVAILABLE STREAMS (" + std::to_string(m_availableStreams.size()) + ")", mx + 30, my + 14, {255, 255, 255, 255}, 22);

    if (m_availableStreams.empty()) {
        drawText("No streams found from active addons.", mx + 50, my + 120, {200, 150, 150, 255}, 20);
        drawText("Tip: Configure Torrentio or Debrid addons in the Addon Menu (-).", mx + 50, my + 160, {160, 165, 185, 255}, 18);
        return;
    }

    int startY = my + 65;
    int itemH = 65;
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
        drawText(streamName, mx + 40, y + 10, {0, 212, 255, 255}, 20);

        std::string title = stream.title;
        std::replace(title.begin(), title.end(), '\n', ' ');
        if (title.length() > 60) title = title.substr(0, 58) + "..";
        drawText(title, mx + 40, y + 33, {200, 205, 220, 255}, 16);
    }
}

void UiEngine::renderAddons() {
    drawRect(40, 90, 1200, 550, {22, 25, 36, 255});
    drawText("INSTALLED STREMIO ADDONS", 70, 115, {255, 255, 255, 255}, 28);
    drawText("(Press X to add a custom addon URL, e.g. Torrentio / RealDebrid)", 70, 155, {160, 165, 185, 255}, 18);

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
        drawRect(0, 0, 1280, 80, {10, 12, 18, 220});
        drawText("▶  " + st.currentTitle, 40, 26, {255, 255, 255, 255}, 26);

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

        int barX = 40;
        int barY = 655;
        int barW = 1200;
        int barH = 10;
        drawRect(barX, barY, barW, barH, {60, 65, 80, 255});

        if (st.duration > 0.0) {
            int filledW = static_cast<int>((st.currentTime / st.duration) * barW);
            drawRect(barX, barY, filledW, barH, {109, 93, 252, 255});
            drawRect(barX + filledW - 4, barY - 4, 8, 18, {0, 212, 255, 255});
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
            m_state = UiState::CATALOG;
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

void UiEngine::handleInput() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            m_running = false;
            return;
        }

        // Handle Player Input
        if (m_state == UiState::PLAYER) {
            if (event.type == SDL_KEYDOWN) {
                m_lastPlayerInputTime = SDL_GetTicks();
                m_showOsd = true;

                switch (event.key.keysym.sym) {
                    case SDLK_SPACE:
                    case SDLK_RETURN:
                        m_player.togglePause();
                        break;
                    case SDLK_ESCAPE:
                    case SDLK_BACKSPACE:
                        m_player.stop();
                        m_player.shutdown(); // Release GPU surface back to SDL2
                        m_state = UiState::DETAILS;
                        break;
                    case SDLK_LEFT:
                        m_player.seekRelative(-10.0);
                        break;
                    case SDLK_RIGHT:
                        m_player.seekRelative(10.0);
                        break;
                    case SDLK_UP:
                        m_player.seekRelative(60.0);
                        break;
                    case SDLK_DOWN:
                        m_player.seekRelative(-60.0);
                        break;
                }
            } else if (event.type == SDL_JOYBUTTONDOWN) {
                m_lastPlayerInputTime = SDL_GetTicks();
                m_showOsd = true;

                // Joy-Con Buttons
                // 0: A, 1: B, 2: X, 3: Y, 10: Plus
                if (event.jbutton.button == 0) { // A
                    m_player.togglePause();
                } else if (event.jbutton.button == 1) { // B
                    m_player.stop();
                    m_player.shutdown(); // Release GPU surface back to SDL2
                    m_state = UiState::DETAILS;
                } else if (event.jbutton.button == 10) { // Plus
                    m_showOsd = !m_showOsd;
                }
            }
            continue;
        }

        // Keyboard & Controller events for UI Navigation
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (m_state == UiState::DETAILS || m_state == UiState::ADDONS) {
                        m_state = UiState::CATALOG;
                    } else if (m_state == UiState::STREAMS) {
                        m_state = UiState::DETAILS;
                    } else {
                        m_running = false;
                    }
                    break;

                case SDLK_LEFT:
                    if (m_state == UiState::CATALOG && m_selectedGridIndex % m_gridCols > 0) {
                        m_selectedGridIndex--;
                    }
                    break;
                case SDLK_RIGHT:
                    if (m_state == UiState::CATALOG && m_selectedGridIndex % m_gridCols < m_gridCols - 1) {
                        m_selectedGridIndex++;
                    }
                    break;
                case SDLK_UP:
                    if (m_state == UiState::CATALOG && m_selectedGridIndex >= m_gridCols) {
                        m_selectedGridIndex -= m_gridCols;
                    } else if (m_state == UiState::STREAMS && m_selectedStreamIndex > 0) {
                        m_selectedStreamIndex--;
                    } else if (m_state == UiState::ADDONS && m_selectedAddonIndex > 0) {
                        m_selectedAddonIndex--;
                    }
                    break;
                case SDLK_DOWN:
                    if (m_state == UiState::CATALOG && m_selectedGridIndex + m_gridCols < m_gridCols * m_gridRows) {
                        m_selectedGridIndex += m_gridCols;
                    } else if (m_state == UiState::STREAMS && m_selectedStreamIndex + 1 < static_cast<int>(m_availableStreams.size())) {
                        m_selectedStreamIndex++;
                    } else if (m_state == UiState::ADDONS && m_selectedAddonIndex + 1 < static_cast<int>(m_addonManager.getAddons().size())) {
                        m_selectedAddonIndex++;
                    }
                    break;

                case SDLK_RETURN:
                case SDLK_SPACE:
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
                            m_state = UiState::DETAILS;
                        }
                    } else if (m_state == UiState::DETAILS) {
                        m_availableStreams = m_addonManager.aggregateStreams(m_selectedMeta.type, m_selectedMeta.id);
                        m_selectedStreamIndex = 0;
                        m_state = UiState::STREAMS;
                    } else if (m_state == UiState::STREAMS && !m_availableStreams.empty()) {
                        playStream(m_availableStreams[m_selectedStreamIndex]);
                    }
                    break;

                case SDLK_x:
                    openSearch();
                    break;

                case SDLK_y:
                    m_contentType = (m_contentType == "movie") ? "series" : "movie";
                    loadCatalogData();
                    break;

                case SDLK_MINUS:
                    m_state = (m_state == UiState::ADDONS) ? UiState::CATALOG : UiState::ADDONS;
                    break;
            }
        } else if (event.type == SDL_JOYBUTTONDOWN) {
            // Joy-Con Buttons
            // 0: A, 1: B, 2: X, 3: Y, 10: Plus, 11: Minus
            switch (event.jbutton.button) {
                case 0: // A (Confirm)
                    if (m_state == UiState::CATALOG && !m_items.empty()) {
                        int idx = m_page * (m_gridCols * m_gridRows) + m_selectedGridIndex;
                        if (idx < static_cast<int>(m_items.size())) {
                            auto catAddon = m_addonManager.getCatalogAddon(m_contentType);
                            if (catAddon) {
                                auto fullMeta = StremioClient::fetchMeta(*catAddon, m_contentType, m_items[idx].id);
                                m_selectedMeta = fullMeta ? *fullMeta : m_items[idx];
                            }
                            m_state = UiState::DETAILS;
                        }
                    } else if (m_state == UiState::DETAILS) {
                        m_availableStreams = m_addonManager.aggregateStreams(m_selectedMeta.type, m_selectedMeta.id);
                        m_selectedStreamIndex = 0;
                        m_state = UiState::STREAMS;
                    } else if (m_state == UiState::STREAMS && !m_availableStreams.empty()) {
                        playStream(m_availableStreams[m_selectedStreamIndex]);
                    } else if (m_state == UiState::ADDONS) {
                        const auto& addons = m_addonManager.getAddons();
                        if (m_selectedAddonIndex < static_cast<int>(addons.size())) {
                            m_addonManager.setAddonEnabled(addons[m_selectedAddonIndex].url, !addons[m_selectedAddonIndex].enabled);
                        }
                    }
                    break;

                case 1: // B (Back)
                    if (m_state == UiState::DETAILS || m_state == UiState::ADDONS) {
                        m_state = UiState::CATALOG;
                    } else if (m_state == UiState::STREAMS) {
                        m_state = UiState::DETAILS;
                    }
                    break;

                case 2: // X (Search or Add Addon)
                    if (m_state == UiState::ADDONS) {
                        openAddAddon();
                    } else {
                        openSearch();
                    }
                    break;

                case 3: // Y (Toggle Movies / Series)
                    m_contentType = (m_contentType == "movie") ? "series" : "movie";
                    loadCatalogData();
                    break;

                case 10: // Plus (Exit)
                    m_running = false;
                    break;

                case 11: // Minus (Addons menu)
                    m_state = (m_state == UiState::ADDONS) ? UiState::CATALOG : UiState::ADDONS;
                    break;
            }
        }
    }
}

void UiEngine::update() {
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
