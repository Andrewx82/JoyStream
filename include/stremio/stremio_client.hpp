#pragma once

#include "stremio/models.hpp"
#include <string>
#include <vector>
#include <optional>
#include <future>

namespace Stremio {

class StremioClient {
public:
    // Helper to sanitize addon URL (e.g. converting stremio:// to https:// and removing manifest.json)
    static std::string normalizeAddonUrl(const std::string& rawUrl);

    // Fetch and parse manifest from addon URL
    static std::optional<AddonManifest> fetchManifest(const std::string& addonUrl);

    // Fetch catalog items (e.g. top movies/series)
    static std::vector<MetaItem> fetchCatalog(const AddonManifest& manifest,
                                             const std::string& type,
                                             const std::string& catalogId,
                                             const std::map<std::string, std::string>& extras = {});

    // Search catalog items
    static std::vector<MetaItem> searchCatalog(const AddonManifest& manifest,
                                              const std::string& type,
                                              const std::string& query);

    // Fetch full metadata for a specific movie or series (including episodes)
    static std::optional<MetaItem> fetchMeta(const AddonManifest& manifest,
                                            const std::string& type,
                                            const std::string& id);

    // Fetch stream links from an addon for a specific movie or episode
    static std::vector<StreamItem> fetchStreams(const AddonManifest& manifest,
                                               const std::string& type,
                                               const std::string& id);

    // Fetch subtitles from an addon
    static std::vector<SubtitleItem> fetchSubtitles(const AddonManifest& manifest,
                                                   const std::string& type,
                                                   const std::string& id);

private:
    static MetaItem parseMetaItem(const json& j);
};

} // namespace Stremio
