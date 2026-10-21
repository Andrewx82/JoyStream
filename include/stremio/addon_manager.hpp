#pragma once

#include "stremio/models.hpp"
#include "stremio/stream_filter.hpp"
#include <vector>
#include <string>
#include <optional>

namespace Stremio {

struct InstalledAddon {
    std::string url;
    bool enabled = true;
    AddonManifest manifest;
};

struct AppSettings {
    bool hwdecEnabled = true;
    std::string audioLanguage = "auto";       // auto, eng, spa, fre, ger, jpn, por, ita
    bool subtitlesEnabled = true;
    std::string subtitleLanguage = "eng";     // eng, spa, fre, ger, jpn, por, ita
    std::string bufferCacheSize = "128M";     // 64M, 128M, 256M
    int networkTimeoutSeconds = 20;           // 10, 15, 20, 30
};

class AddonManager {
public:
    AddonManager();

    // Load installed addons from disk (sdmc:/switch/joystream/addons.json or local)
    bool loadConfig(const std::string& path = "");

    // Save installed addons to disk
    bool saveConfig(const std::string& path = "");

    // Add a new addon by URL (fetches manifest and adds if valid)
    bool addAddon(const std::string& url);

    // Remove an addon by URL or ID
    bool removeAddon(const std::string& idOrUrl);

    // Toggle addon enabled state
    void setAddonEnabled(const std::string& id, bool enabled);

    // Settings
    const AppSettings& getSettings() const { return m_settings; }
    void setSettings(const AppSettings& s) { m_settings = s; saveConfig(); }
    AppSettings& getSettingsMutable() { return m_settings; }

    // Filter Config
    const FilterConfig& getFilterConfig() const { return m_filterConfig; }
    void setFilterConfig(const FilterConfig& config) { m_filterConfig = config; saveConfig(); }

    // Get all installed addons
    const std::vector<InstalledAddon>& getAddons() const { return m_addons; }

    // Get the primary catalog addon (Cinemeta by default)
    std::optional<AddonManifest> getCatalogAddon(const std::string& type = "movie") const;

    // Fetch streams aggregated from all enabled stream-providing addons
    std::vector<StreamItem> aggregateStreams(const std::string& type, const std::string& id);

    // Fetch subtitles aggregated from all enabled subtitle-providing addons
    std::vector<SubtitleItem> aggregateSubtitles(const std::string& type, const std::string& id);

    static std::string getDefaultConfigPath();

private:
    std::vector<InstalledAddon> m_addons;
    std::string m_configPath;
    FilterConfig m_filterConfig;
    AppSettings m_settings;
};

} // namespace Stremio
