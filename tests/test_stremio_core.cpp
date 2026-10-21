#include "stremio/stremio_client.hpp"
#include "stremio/addon_manager.hpp"
#include "stremio/http_client.hpp"
#include <iostream>
#include <cassert>

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
    bool saved = manager.saveConfig(testConfigPath);
    assert(saved);

    AddonManager manager2;
    bool loaded = manager2.loadConfig(testConfigPath);
    assert(loaded);
    std::cout << "  Loaded " << manager2.getAddons().size() << " addons from saved file." << std::endl;
    std::cout << "  PASS: Config loaded and saved cleanly." << std::endl;

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
