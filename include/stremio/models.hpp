#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "../external/json.hpp"

namespace Stremio {

using json = nlohmann::json;

struct VideoItem {
    std::string id;
    std::string title;
    int season = 0;
    int episode = 0;
    std::string released;
    std::string thumbnail;
    std::string overview;
};

struct MetaItem {
    std::string id;
    std::string type;
    std::string name;
    std::string poster;
    std::string background;
    std::string logo;
    std::string description;
    std::string releaseInfo;
    std::string imdbRating;
    std::vector<std::string> genres;
    std::vector<std::string> cast;
    std::vector<std::string> director;
    std::vector<VideoItem> videos; // For series
};

struct StreamItem {
    std::string name;
    std::string title;
    std::string description;
    std::string url;        // Direct HTTP/HTTPS or Debrid stream link
    std::string infoHash;   // Torrent infohash (if P2P)
    int fileIdx = -1;
    std::map<std::string, std::string> headers;
};

struct SubtitleItem {
    std::string id;
    std::string url;
    std::string lang;
};

struct CatalogDef {
    std::string type;
    std::string id;
    std::string name;
    std::vector<std::string> extraSupported; // e.g. "search", "genre", "skip"
};

struct AddonManifest {
    std::string id;
    std::string version;
    std::string name;
    std::string description;
    std::string icon;
    std::string baseUrl;
    std::vector<std::string> types;     // "movie", "series", etc.
    std::vector<std::string> resources; // "catalog", "meta", "stream", "subtitles"
    std::vector<CatalogDef> catalogs;

    bool supportsResource(const std::string& resource) const {
        for (const auto& r : resources) {
            if (r == resource) return true;
        }
        return false;
    }

    bool supportsType(const std::string& type) const {
        for (const auto& t : types) {
            if (t == type) return true;
        }
        return false;
    }
};

} // namespace Stremio
