#include "stremio/stremio_client.hpp"
#include "stremio/addon_manager.hpp"
#include "stremio/http_client.hpp"
#include "stremio/stream_filter.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace Stremio;

void runTests() {
    std::cout << "========================================" << std::endl;
    std::cout << "  STREMIO-NX CORE ENGINE TEST SUITE    " << std::endl;
    std::cout << "========================================" << std::endl;

    // Test 1: URL Normalization
    std::cout << "\n[TEST 1] URL Normalization..." << std::endl;
    std::string testUrl = "stremio://v3-cinemeta.strem.io/manifest.json";
    std::string clean = StremioClient::normalizeAddonUrl(testUrl);
    std::cout << "  Original:   " << testUrl << std::endl;
    std::cout << "  Normalized: " << clean << std::endl;
    assert(clean == "https://v3-cinemeta.strem.io");
    std::cout << "  PASS: URL normalized correctly." << std::endl;

    // Test 2: Fetch Cinemeta Manifest
    std::cout << "\n[TEST 2] Fetching Addon Manifest (Cinemeta)..." << std::endl;
    auto manifestOpt = StremioClient::fetchManifest("https://v3-cinemeta.strem.io");
    assert(manifestOpt.has_value());
    const auto& manifest = *manifestOpt;
    std::cout << "  Addon ID:          " << manifest.id << std::endl;
    std::cout << "  Addon Name:        " << manifest.name << std::endl;
    std::cout << "  Addon Version:     " << manifest.version << std::endl;
    std::cout << "  Addon Description: " << manifest.description << std::endl;
    std::cout << "  Catalogs count:    " << manifest.catalogs.size() << std::endl;
    for (const auto& cat : manifest.catalogs) {
        std::cout << "    - Catalog: [" << cat.type << "] " << cat.name << " (id: " << cat.id << ")" << std::endl;
    }
    assert(!manifest.catalogs.empty());
    assert(manifest.supportsType("movie"));
    assert(manifest.supportsResource("catalog"));
    std::cout << "  PASS: Manifest parsed successfully." << std::endl;

    // Test 3: Fetch Catalog (Top Movies)
    std::cout << "\n[TEST 3] Fetching Catalog (Top Movies)..." << std::endl;
    auto movies = StremioClient::fetchCatalog(manifest, "movie", "top");
    std::cout << "  Fetched " << movies.size() << " movies from catalog." << std::endl;
    assert(!movies.empty());
    for (size_t i = 0; i < std::min((size_t)5, movies.size()); ++i) {
        std::cout << "    [" << (i + 1) << "] " << movies[i].name
                  << " (" << movies[i].releaseInfo << ") - IMDb: " << movies[i].id
                  << " - Poster: " << (movies[i].poster.empty() ? "None" : movies[i].poster.substr(0, 40) + "...")
                  << std::endl;
    }
    std::cout << "  PASS: Catalog retrieved successfully." << std::endl;

    // Test 4: Search Catalog
    std::cout << "\n[TEST 4] Searching Cinemeta for 'Inception'..." << std::endl;
    auto searchResults = StremioClient::searchCatalog(manifest, "movie", "Inception");
    std::cout << "  Found " << searchResults.size() << " results." << std::endl;
    assert(!searchResults.empty());
    bool foundInception = false;
    for (const auto& item : searchResults) {
        std::cout << "    - " << item.name << " (" << item.releaseInfo << ") ID: " << item.id << std::endl;
        if (item.name.find("Inception") != std::string::npos || item.id == "tt1375666") {
            foundInception = true;
        }
    }
    assert(foundInception);
    std::cout << "  PASS: Search works properly." << std::endl;

    // Test 5: Fetch Metadata Details
    std::cout << "\n[TEST 5] Fetching Metadata for Inception (tt1375666)..." << std::endl;
    auto metaOpt = StremioClient::fetchMeta(manifest, "movie", "tt1375666");
    assert(metaOpt.has_value());
    const auto& meta = *metaOpt;
    std::cout << "  Title:       " << meta.name << std::endl;
    std::cout << "  Year:        " << meta.releaseInfo << std::endl;
    std::cout << "  Rating:      " << meta.imdbRating << std::endl;
    std::cout << "  Genres:      ";
    for (const auto& g : meta.genres) std::cout << g << " ";
    std::cout << std::endl;
    std::cout << "  Description: " << meta.description.substr(0, 100) << "..." << std::endl;
    std::cout << "  PASS: Meta item details parsed." << std::endl;

    // Test 6: Addon Manager & Persistence
    std::cout << "\n[TEST 6] Addon Manager & Local Persistence..." << std::endl;
    AddonManager manager;
    std::string testConfigPath = "/tmp/test_stremio_addons.json";
    FilterConfig customConfig;
    customConfig.mode = StreamFilterMode::CUSTOM_REGEX;
    customConfig.customExcludeRegex = R"(\b(cam|screener|ts)\b)";
    manager.setFilterConfig(customConfig);
    bool saved = manager.saveConfig(testConfigPath);
    assert(saved);

    AddonManager manager2;
    bool loaded = manager2.loadConfig(testConfigPath);
    assert(loaded);
    assert(manager2.getFilterConfig().mode == StreamFilterMode::CUSTOM_REGEX);
    assert(manager2.getFilterConfig().customExcludeRegex == R"(\b(cam|screener|ts)\b)");
    std::cout << "  Loaded " << manager2.getAddons().size() << " addons from saved file." << std::endl;
    std::cout << "  Filter config preserved: mode=" << StreamFilter::getModeName(manager2.getFilterConfig().mode) << std::endl;
    std::cout << "  PASS: Config loaded and saved cleanly with FilterConfig." << std::endl;

    // Test 7: Stream Filter Engine & Presets
    std::cout << "\n[TEST 7] Stream Filter Engine & Switch Optimization..." << std::endl;

    // File size parsing
    float sizeGB1 = StreamFilter::extractFileSizeGB("Movie 1080p [14.5 GB]");
    assert(std::fabs(sizeGB1 - 14.5f) < 0.01f);
    float sizeMB = StreamFilter::extractFileSizeGB("Series 720p [512 MB]");
    assert(std::fabs(sizeMB - 0.5f) < 0.01f);

    // Mock streams
    std::vector<StreamItem> mockStreams = {
        {"Torrentio\n4k Remux", "4K 2160p Remux [45.0 GB]", "", "info1", "desc1"},
        {"Torrentio\n1080p Debrid", "1080p WEB-DL [4.2 GB]", "https://debrid.example/stream1", "info2", "desc2"},
        {"Torrentio\n720p Debrid", "720p WEB-DL [1.1 GB]", "https://debrid.example/stream2", "info3", "desc3"},
        {"Torrentio\nCAM", "Inception NEW HDCAM [1.5 GB]", "https://debrid.example/stream4", "info4", "desc4"},
        {"Torrentio\nAV1", "Inception 1080p AV1 [2.0 GB]", "https://debrid.example/stream5", "info5", "desc5"},
        {"YTS\n1080p", "Inception 1080p Bluray", "https://debrid.example/stream6", "info6", "desc6"}
    };

    // Test SWITCH_OPTIMIZED mode: filters out 4K, CAM, AV1, and >25GB files; preserves YTS (not false-matching ts)
    FilterConfig optConfig;
    optConfig.mode = StreamFilterMode::SWITCH_OPTIMIZED;
    auto optFiltered = StreamFilter::filterAndRank(mockStreams, optConfig);
    std::cout << "  SWITCH_OPTIMIZED count: " << optFiltered.size() << " / " << mockStreams.size() << std::endl;
    for (const auto& s : optFiltered) {
        std::cout << "    - " << s.name << " | " << s.title << std::endl;
        assert(s.name.find("4k") == std::string::npos);
        assert(s.name.find("CAM") == std::string::npos);
        assert(s.name.find("AV1") == std::string::npos);
    }
    assert(optFiltered.size() == 3); // 1080p Debrid, 720p Debrid, YTS 1080p

    // Test ONLY_1080P mode
    FilterConfig p1080Config;
    p1080Config.mode = StreamFilterMode::ONLY_1080P;
    auto p1080Filtered = StreamFilter::filterAndRank(mockStreams, p1080Config);
    std::cout << "  ONLY_1080P count: " << p1080Filtered.size() << std::endl;
    for (const auto& s : p1080Filtered) {
        assert(s.title.find("1080p") != std::string::npos || s.name.find("1080p") != std::string::npos);
    }

    // Test ONLY_720P mode
    FilterConfig p720Config;
    p720Config.mode = StreamFilterMode::ONLY_720P;
    auto p720Filtered = StreamFilter::filterAndRank(mockStreams, p720Config);
    std::cout << "  ONLY_720P count: " << p720Filtered.size() << std::endl;
    assert(p720Filtered.size() == 1);
    assert(p720Filtered[0].name.find("720p") != std::string::npos);

    // Test CUSTOM_REGEX mode
    FilterConfig customRegexCfg;
    customRegexCfg.mode = StreamFilterMode::CUSTOM_REGEX;
    customRegexCfg.customExcludeRegex = R"(debrid)";
    auto customFiltered = StreamFilter::filterAndRank(mockStreams, customRegexCfg);
    std::cout << "  CUSTOM_REGEX (exclude 'debrid') count: " << customFiltered.size() << std::endl;
    for (const auto& s : customFiltered) {
        assert(s.name.find("Debrid") == std::string::npos);
    }

    // Test SHOW_ALL mode
    FilterConfig allConfig;
    allConfig.mode = StreamFilterMode::SHOW_ALL;
    auto allFiltered = StreamFilter::filterAndRank(mockStreams, allConfig);
    assert(allFiltered.size() == mockStreams.size());

    std::cout << "  PASS: Stream filter presets and custom regex validated successfully." << std::endl;

    std::cout << "\n========================================" << std::endl;
    std::cout << "  ALL STREMIO TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main() {
    HttpClient::initializeNetwork();
    runTests();
    HttpClient::cleanupNetwork();
    return 0;
}
