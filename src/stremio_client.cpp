#include "stremio/stremio_client.hpp"
#include "stremio/http_client.hpp"
#include <iostream>
#include <algorithm>

namespace Stremio {

std::string StremioClient::normalizeAddonUrl(const std::string& rawUrl) {
    std::string url = rawUrl;

    // Replace stremio:// with https://
    if (url.rfind("stremio://", 0) == 0) {
        url = "https://" + url.substr(10);
    }

    // Strip trailing /manifest.json if present
    const std::string suffix = "/manifest.json";
    if (url.length() >= suffix.length() &&
        url.compare(url.length() - suffix.length(), suffix.length(), suffix) == 0) {
        url = url.substr(0, url.length() - suffix.length());
    }

    // Strip trailing slash
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    return url;
}

std::optional<AddonManifest> StremioClient::fetchManifest(const std::string& rawUrl) {
    std::string base = normalizeAddonUrl(rawUrl);
    std::string manifestUrl = base + "/manifest.json";

    HttpResponse resp = HttpClient::get(manifestUrl);
    if (!resp.success) {
        std::cerr << "Failed to fetch manifest from: " << manifestUrl << " Error: " << resp.error << std::endl;
        return std::nullopt;
    }

    try {
        json j = json::parse(resp.body);
        AddonManifest manifest;
        manifest.baseUrl = base;
        manifest.id = j.value("id", "");
        manifest.name = j.value("name", "Unnamed Addon");
        manifest.version = j.value("version", "1.0.0");
        manifest.description = j.value("description", "");
        manifest.icon = j.value("icon", "");

        if (j.contains("types") && j["types"].is_array()) {
            for (const auto& t : j["types"]) {
                if (t.is_string()) manifest.types.push_back(t.get<std::string>());
            }
        }

        if (j.contains("resources") && j["resources"].is_array()) {
            for (const auto& r : j["resources"]) {
                if (r.is_string()) {
                    manifest.resources.push_back(r.get<std::string>());
                } else if (r.is_object() && r.contains("name") && r["name"].is_string()) {
                    manifest.resources.push_back(r["name"].get<std::string>());
                }
            }
        }

        if (j.contains("catalogs") && j["catalogs"].is_array()) {
            for (const auto& c : j["catalogs"]) {
                CatalogDef cat;
                cat.type = c.value("type", "");
                cat.id = c.value("id", "");
                cat.name = c.value("name", cat.id);

                if (c.contains("extra") && c["extra"].is_array()) {
                    for (const auto& ex : c["extra"]) {
                        if (ex.is_object() && ex.contains("name")) {
                            cat.extraSupported.push_back(ex["name"].get<std::string>());
                        } else if (ex.is_string()) {
                            cat.extraSupported.push_back(ex.get<std::string>());
                        }
                    }
                }
                manifest.catalogs.push_back(cat);
            }
        }

        return manifest;
    } catch (const std::exception& e) {
        std::cerr << "JSON parse error in manifest: " << e.what() << std::endl;
        return std::nullopt;
    }
}

MetaItem StremioClient::parseMetaItem(const json& j) {
    MetaItem item;
    item.id = j.value("id", j.value("imdb_id", ""));
    item.type = j.value("type", "movie");
    item.name = j.value("name", j.value("title", "Unknown Title"));
    item.poster = j.value("poster", "");
    item.background = j.value("background", "");
    item.logo = j.value("logo", "");
    item.description = j.value("description", j.value("overview", ""));

    if (j.contains("releaseInfo")) {
        if (j["releaseInfo"].is_string()) item.releaseInfo = j["releaseInfo"].get<std::string>();
        else if (j["releaseInfo"].is_number()) item.releaseInfo = std::to_string(j["releaseInfo"].get<int>());
    } else if (j.contains("year")) {
        if (j["year"].is_number()) item.releaseInfo = std::to_string(j["year"].get<int>());
        else if (j["year"].is_string()) item.releaseInfo = j["year"].get<std::string>();
    }

    if (j.contains("imdbRating")) {
        if (j["imdbRating"].is_string()) item.imdbRating = j["imdbRating"].get<std::string>();
        else if (j["imdbRating"].is_number()) {
            std::ostringstream ss;
            ss << j["imdbRating"].get<double>();
            item.imdbRating = ss.str();
        }
    }

    if (j.contains("genres") && j["genres"].is_array()) {
        for (const auto& g : j["genres"]) if (g.is_string()) item.genres.push_back(g.get<std::string>());
    } else if (j.contains("genre") && j["genre"].is_array()) {
        for (const auto& g : j["genre"]) if (g.is_string()) item.genres.push_back(g.get<std::string>());
    }

    if (j.contains("cast") && j["cast"].is_array()) {
        for (const auto& c : j["cast"]) if (c.is_string()) item.cast.push_back(c.get<std::string>());
    }

    if (j.contains("director") && j["director"].is_array()) {
        for (const auto& d : j["director"]) if (d.is_string()) item.director.push_back(d.get<std::string>());
    }

    if (j.contains("videos") && j["videos"].is_array()) {
        for (const auto& v : j["videos"]) {
            VideoItem video;
            video.id = v.value("id", "");
            video.title = v.value("title", v.value("name", ""));
            video.season = v.value("season", 0);
            video.episode = v.value("episode", v.value("number", 0));
            video.released = v.value("released", "");
            video.thumbnail = v.value("thumbnail", "");
            video.overview = v.value("overview", "");
            item.videos.push_back(video);
        }
    }

    return item;
}

std::vector<MetaItem> StremioClient::fetchCatalog(const AddonManifest& manifest,
                                                 const std::string& type,
                                                 const std::string& catalogId,
                                                 const std::map<std::string, std::string>& extras) {
    std::vector<MetaItem> results;
    std::string url = manifest.baseUrl + "/catalog/" + type + "/" + catalogId;

    if (!extras.empty()) {
        std::string extraStr;
        for (const auto& [k, v] : extras) {
            if (!extraStr.empty()) extraStr += "&";
            extraStr += k + "=" + HttpClient::urlEncode(v);
        }
        url += "/" + extraStr;
    }
    url += ".json";

    HttpResponse resp = HttpClient::get(url);
    if (!resp.success) {
        std::cerr << "Failed to fetch catalog: " << url << " error: " << resp.error << std::endl;
        return results;
    }

    try {
        json j = json::parse(resp.body);
        if (j.contains("metas") && j["metas"].is_array()) {
            for (const auto& m : j["metas"]) {
                results.push_back(parseMetaItem(m));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON error parsing catalog: " << e.what() << std::endl;
    }

    return results;
}

std::vector<MetaItem> StremioClient::searchCatalog(const AddonManifest& manifest,
                                                  const std::string& type,
                                                  const std::string& query) {
    std::map<std::string, std::string> extras = {{"search", query}};
    return fetchCatalog(manifest, type, "top", extras);
}

std::optional<MetaItem> StremioClient::fetchMeta(const AddonManifest& manifest,
                                                const std::string& type,
                                                const std::string& id) {
    std::string url = manifest.baseUrl + "/meta/" + type + "/" + id + ".json";
    HttpResponse resp = HttpClient::get(url);
    if (!resp.success) {
        std::cerr << "Failed to fetch meta: " << url << " error: " << resp.error << std::endl;
        return std::nullopt;
    }

    try {
        json j = json::parse(resp.body);
        if (j.contains("meta") && j["meta"].is_object()) {
            return parseMetaItem(j["meta"]);
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON error parsing meta: " << e.what() << std::endl;
    }

    return std::nullopt;
}

std::vector<StreamItem> StremioClient::fetchStreams(const AddonManifest& manifest,
                                                   const std::string& type,
                                                   const std::string& id) {
    std::vector<StreamItem> streams;
    std::string url = manifest.baseUrl + "/stream/" + type + "/" + id + ".json";

    HttpResponse resp = HttpClient::get(url);
    if (!resp.success) {
        std::cerr << "Failed to fetch streams: " << url << " error: " << resp.error << std::endl;
        return streams;
    }

    try {
        json j = json::parse(resp.body);
        if (j.contains("streams") && j["streams"].is_array()) {
            for (const auto& s : j["streams"]) {
                StreamItem item;
                item.name = s.value("name", "Stream");
                item.title = s.value("title", s.value("description", ""));
                item.url = s.value("url", "");
                item.infoHash = s.value("infoHash", "");
                item.fileIdx = s.value("fileIdx", -1);

                if (s.contains("behaviorHints") && s["behaviorHints"].is_object()) {
                    const auto& hints = s["behaviorHints"];
                    if (hints.contains("proxyHeaders") && hints["proxyHeaders"].is_object()) {
                        const auto& ph = hints["proxyHeaders"];
                        if (ph.contains("request") && ph["request"].is_object()) {
                            for (auto it = ph["request"].begin(); it != ph["request"].end(); ++it) {
                                if (it.value().is_string()) {
                                    item.headers[it.key()] = it.value().get<std::string>();
                                }
                            }
                        }
                    }
                }
                streams.push_back(item);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON error parsing streams: " << e.what() << std::endl;
    }

    return streams;
}

std::vector<SubtitleItem> StremioClient::fetchSubtitles(const AddonManifest& manifest,
                                                       const std::string& type,
                                                       const std::string& id) {
    std::vector<SubtitleItem> subs;
    std::string url = manifest.baseUrl + "/subtitles/" + type + "/" + id + ".json";

    HttpResponse resp = HttpClient::get(url);
    if (!resp.success) {
        return subs;
    }

    try {
        json j = json::parse(resp.body);
        if (j.contains("subtitles") && j["subtitles"].is_array()) {
            for (const auto& s : j["subtitles"]) {
                SubtitleItem item;
                item.id = s.value("id", "");
                item.url = s.value("url", "");
                item.lang = s.value("lang", "en");
                subs.push_back(item);
            }
        }
    } catch (...) {}

    return subs;
}

} // namespace Stremio
