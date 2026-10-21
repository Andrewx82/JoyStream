#include "stremio/addon_manager.hpp"
#include "stremio/stremio_client.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Stremio {

std::string AddonManager::getDefaultConfigPath() {
#ifdef __SWITCH__
    return "sdmc:/switch/joystream/addons.json";
#else
    return "addons.json";
#endif
}

AddonManager::AddonManager() {
    m_configPath = getDefaultConfigPath();
}

bool AddonManager::loadConfig(const std::string& path) {
    if (!path.empty()) {
        m_configPath = path;
    }

    std::ifstream file(m_configPath);
    if (!file.is_open()) {
        std::cout << "No existing config at " << m_configPath << ". Initializing with defaults." << std::endl;
        // Seed default addons: Cinemeta
        addAddon("https://v3-cinemeta.strem.io");
        saveConfig();
        return true;
    }

    try {
        json j;
        file >> j;
        m_addons.clear();

        if (j.contains("filterMode") && j["filterMode"].is_number_integer()) {
            m_filterConfig.mode = static_cast<StreamFilterMode>(j["filterMode"].get<int>());
        }
        if (j.contains("customExcludeRegex") && j["customExcludeRegex"].is_string()) {
            m_filterConfig.customExcludeRegex = j["customExcludeRegex"].get<std::string>();
        }
        if (j.contains("customIncludeRegex") && j["customIncludeRegex"].is_string()) {
            m_filterConfig.customIncludeRegex = j["customIncludeRegex"].get<std::string>();
        }
        if (j.contains("maxFileSizeGB") && j["maxFileSizeGB"].is_number()) {
            m_filterConfig.maxFileSizeGB = j["maxFileSizeGB"].get<float>();
        }

        if (j.contains("settings") && j["settings"].is_object()) {
            const auto& s = j["settings"];
            m_settings.hwdecEnabled = s.value("hwdecEnabled", true);
            m_settings.audioLanguage = s.value("audioLanguage", "auto");
            m_settings.subtitlesEnabled = s.value("subtitlesEnabled", true);
            m_settings.subtitleLanguage = s.value("subtitleLanguage", "eng");
            m_settings.bufferCacheSize = s.value("bufferCacheSize", "128M");
            m_settings.networkTimeoutSeconds = s.value("networkTimeoutSeconds", 20);
        }

        if (j.contains("addons") && j["addons"].is_array()) {
            for (const auto& a : j["addons"]) {
                std::string url = a.value("url", "");
                bool enabled = a.value("enabled", true);
                if (!url.empty()) {
                    auto manifestOpt = StremioClient::fetchManifest(url);
                    if (manifestOpt) {
                        InstalledAddon installed;
                        installed.url = StremioClient::normalizeAddonUrl(url);
                        installed.enabled = enabled;
                        installed.manifest = *manifestOpt;
                        m_addons.push_back(installed);
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error reading addon config: " << e.what() << std::endl;
        return false;
    }
}

bool AddonManager::saveConfig(const std::string& path) {
    std::string outPath = path.empty() ? m_configPath : path;

    // Ensure parent directories exist
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/joystream", 0777);
#endif

    json j;
    j["filterMode"] = static_cast<int>(m_filterConfig.mode);
    j["customExcludeRegex"] = m_filterConfig.customExcludeRegex;
    j["customIncludeRegex"] = m_filterConfig.customIncludeRegex;
    j["maxFileSizeGB"] = m_filterConfig.maxFileSizeGB;

    json s;
    s["hwdecEnabled"] = m_settings.hwdecEnabled;
    s["audioLanguage"] = m_settings.audioLanguage;
    s["subtitlesEnabled"] = m_settings.subtitlesEnabled;
    s["subtitleLanguage"] = m_settings.subtitleLanguage;
    s["bufferCacheSize"] = m_settings.bufferCacheSize;
    s["networkTimeoutSeconds"] = m_settings.networkTimeoutSeconds;
    j["settings"] = s;

    j["addons"] = json::array();

    for (const auto& addon : m_addons) {
        json a;
        a["url"] = addon.url;
        a["enabled"] = addon.enabled;
        a["name"] = addon.manifest.name;
        j["addons"].push_back(a);
    }

    std::ofstream file(outPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file for writing: " << outPath << std::endl;
        return false;
    }

    file << j.dump(4);
    return true;
}

bool AddonManager::addAddon(const std::string& url) {
    std::string cleanUrl = StremioClient::normalizeAddonUrl(url);

    // Check if already installed
    for (const auto& existing : m_addons) {
        if (existing.url == cleanUrl) {
            std::cout << "Addon already installed: " << cleanUrl << std::endl;
            return false;
        }
    }

    auto manifestOpt = StremioClient::fetchManifest(cleanUrl);
    if (!manifestOpt) {
        std::cerr << "Could not fetch manifest for: " << cleanUrl << std::endl;
        return false;
    }

    InstalledAddon installed;
    installed.url = cleanUrl;
    installed.enabled = true;
    installed.manifest = *manifestOpt;

    m_addons.push_back(installed);
    saveConfig();
    return true;
}

bool AddonManager::updateAddonUrl(const std::string& oldUrl, const std::string& newUrl) {
    std::string cleanOldUrl = StremioClient::normalizeAddonUrl(oldUrl);
    std::string cleanNewUrl = StremioClient::normalizeAddonUrl(newUrl);

    if (cleanNewUrl.empty()) return false;

    auto manifestOpt = StremioClient::fetchManifest(cleanNewUrl);
    if (!manifestOpt) {
        std::cerr << "[AddonManager] Could not fetch manifest for updated URL: " << cleanNewUrl << std::endl;
        return false;
    }

    for (auto& a : m_addons) {
        if (a.url == cleanOldUrl || a.url == oldUrl) {
            a.url = cleanNewUrl;
            a.manifest = *manifestOpt;
            saveConfig();
            std::cout << "[AddonManager] Successfully updated addon URL to: " << cleanNewUrl << std::endl;
            return true;
        }
    }

    return false;
}

bool AddonManager::removeAddon(const std::string& idOrUrl) {
    auto it = std::remove_if(m_addons.begin(), m_addons.end(), [&](const InstalledAddon& a) {
        return a.url == idOrUrl || a.manifest.id == idOrUrl;
    });

    if (it != m_addons.end()) {
        m_addons.erase(it, m_addons.end());
        saveConfig();
        return true;
    }
    return false;
}

void AddonManager::setAddonEnabled(const std::string& id, bool enabled) {
    for (auto& a : m_addons) {
        if (a.manifest.id == id || a.url == id) {
            a.enabled = enabled;
            saveConfig();
            break;
        }
    }
}

std::optional<AddonManifest> AddonManager::getCatalogAddon(const std::string& type) const {
    for (const auto& a : m_addons) {
        if (a.enabled && a.manifest.supportsResource("catalog") && a.manifest.supportsType(type)) {
            return a.manifest;
        }
    }
    return std::nullopt;
}

std::vector<StreamItem> AddonManager::aggregateStreams(const std::string& type, const std::string& id) {
    std::vector<StreamItem> allStreams;

    for (const auto& addon : m_addons) {
        if (!addon.enabled) continue;
        if (!addon.manifest.supportsResource("stream")) continue;

        auto streams = StremioClient::fetchStreams(addon.manifest, type, id);
        for (auto& s : streams) {
            // Tag with addon name if not present
            if (s.name.empty() || s.name == "Stream") {
                s.name = addon.manifest.name;
            }
            allStreams.push_back(std::move(s));
        }
    }

    return allStreams;
}

std::vector<SubtitleItem> AddonManager::aggregateSubtitles(const std::string& type, const std::string& id) {
    std::vector<SubtitleItem> allSubs;

    for (const auto& addon : m_addons) {
        if (!addon.enabled) continue;
        if (!addon.manifest.supportsResource("subtitles")) continue;

        auto subs = StremioClient::fetchSubtitles(addon.manifest, type, id);
        allSubs.insert(allSubs.end(), subs.begin(), subs.end());
    }

    return allSubs;
}

} // namespace Stremio
