#pragma once

#include "stremio/models.hpp"
#include <vector>
#include <string>
#include <optional>

namespace Stremio {

struct InstalledAddon {
    std::string url;
    bool enabled = true;
    AddonManifest manifest;
};

class AddonManager {
public:
    AddonManager();

    // Load installed addons from disk (sdmc:/switch/stremio-nx/addons.json or local)
    bool loadConfig(const std::string& path = "");

    // Save installed addons to disk
    bool saveConfig(const std::string& path = "");

    // Add a new addon by URL (fetches manifest and adds if valid)
    bool addAddon(const std::string& url);

    // Remove an addon by URL or ID
    bool removeAddon(const std::string& idOrUrl);

    // Toggle addon enabled state
    void setAddonEnabled(const std::string& id, bool enabled);

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
};

} // namespace Stremio
