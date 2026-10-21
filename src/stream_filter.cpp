#include "stremio/stream_filter.hpp"
#include <regex>
#include <algorithm>
#include <iostream>
#include <cctype>

namespace Stremio {

std::string StreamFilter::getModeName(StreamFilterMode mode) {
    switch (mode) {
        case StreamFilterMode::SWITCH_OPTIMIZED:
            return "Switch Optimized";
        case StreamFilterMode::ONLY_1080P:
            return "1080p Only";
        case StreamFilterMode::ONLY_720P:
            return "720p Only";
        case StreamFilterMode::CUSTOM_REGEX:
            return "Custom Regex";
        case StreamFilterMode::SHOW_ALL:
            return "Show All";
        default:
            return "Unknown";
    }
}

std::string StreamFilter::getModeDescription(StreamFilterMode mode) {
    switch (mode) {
        case StreamFilterMode::SWITCH_OPTIMIZED:
            return "Filters 4K/AV1/CAM, prioritizes 1080p/720p & Debrid HTTPS";
        case StreamFilterMode::ONLY_1080P:
            return "Strictly 1080p streams";
        case StreamFilterMode::ONLY_720P:
            return "Strictly 720p streams (optimal for handheld & battery)";
        case StreamFilterMode::CUSTOM_REGEX:
            return "User-defined regular expression filter";
        case StreamFilterMode::SHOW_ALL:
            return "Raw stream results with no filters applied";
        default:
            return "";
    }
}

StreamFilterMode StreamFilter::cycleMode(StreamFilterMode current) {
    int next = (static_cast<int>(current) + 1) % static_cast<int>(StreamFilterMode::COUNT);
    return static_cast<StreamFilterMode>(next);
}

float StreamFilter::extractFileSizeGB(const std::string& text) {
    try {
        static const std::regex sizeRegex(R"((\d+(?:\.\d+)?)\s*(GB|MB))", std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(text, match, sizeRegex)) {
            float val = std::stof(match[1].str());
            std::string unit = match[2].str();
            for (auto& c : unit) c = static_cast<char>(toupper(c));
            if (unit == "MB") return val / 1024.0f;
            return val;
        }
    } catch (...) {}
    return -1.0f;
}

bool StreamFilter::matchesRegex(const std::string& text, const std::string& pattern) {
    if (pattern.empty() || text.empty()) return false;
    try {
        std::regex re(pattern, std::regex_constants::icase | std::regex_constants::ECMAScript);
        return std::regex_search(text, re);
    } catch (const std::regex_error& e) {
        std::cerr << "[StreamFilter] Regex error with pattern '" << pattern << "': " << e.what() << std::endl;
        return false;
    }
}

std::vector<StreamItem> StreamFilter::filterAndRank(const std::vector<StreamItem>& streams, const FilterConfig& config) {
    if (config.mode == StreamFilterMode::SHOW_ALL || streams.empty()) {
        return streams;
    }

    std::vector<StreamItem> filtered;

    for (const auto& stream : streams) {
        std::string fullText = stream.name + "\n" + stream.title + "\n" + stream.description;

        bool accept = true;

        if (config.mode == StreamFilterMode::SWITCH_OPTIMIZED) {
            // Exclude unplayable or heavy formats on Tegra X1
            static const std::string switchExclude = R"(\b(4k|2160p|cam|camrip|ts|telesync|telecine|hdcam|hd-ts|remux|av1)\b)";
            if (matchesRegex(fullText, switchExclude)) {
                accept = false;
            }

            // Exclude massive files that cause Switch Wi-Fi buffer timeouts
            if (accept && config.maxFileSizeGB > 0) {
                float sizeGB = extractFileSizeGB(fullText);
                if (sizeGB > 0 && sizeGB > config.maxFileSizeGB) {
                    accept = false;
                }
            }
        }
        else if (config.mode == StreamFilterMode::ONLY_1080P) {
            static const std::string p1080 = R"(\b(1080p|fhd)\b)";
            static const std::string exclude = R"(\b(4k|2160p|720p|cam|ts)\b)";
            if (!matchesRegex(fullText, p1080) || matchesRegex(fullText, exclude)) {
                accept = false;
            }
        }
        else if (config.mode == StreamFilterMode::ONLY_720P) {
            static const std::string p720 = R"(\b(720p|hd)\b)";
            static const std::string exclude = R"(\b(4k|2160p|1080p|cam|ts)\b)";
            if (!matchesRegex(fullText, p720) || matchesRegex(fullText, exclude)) {
                accept = false;
            }
        }
        else if (config.mode == StreamFilterMode::CUSTOM_REGEX) {
            if (!config.customIncludeRegex.empty()) {
                if (!matchesRegex(fullText, config.customIncludeRegex)) {
                    accept = false;
                }
            }
            if (accept && !config.customExcludeRegex.empty()) {
                if (matchesRegex(fullText, config.customExcludeRegex)) {
                    accept = false;
                }
            }
        }

        if (accept) {
            filtered.push_back(stream);
        }
    }

    // Rank / Sort for Switch playback convenience
    std::stable_sort(filtered.begin(), filtered.end(), [](const StreamItem& a, const StreamItem& b) {
        // Prioritize direct HTTPS / Debrid streams over raw torrents
        bool aIsHttp = !a.url.empty();
        bool bIsHttp = !b.url.empty();
        if (aIsHttp != bIsHttp) return aIsHttp > bIsHttp;

        // Prioritize 1080p over other resolutions in Switch Optimized mode
        std::string aFull = a.name + " " + a.title;
        std::string bFull = b.name + " " + b.title;
        bool a1080 = matchesRegex(aFull, R"(\b(1080p|fhd)\b)");
        bool b1080 = matchesRegex(bFull, R"(\b(1080p|fhd)\b)");
        if (a1080 != b1080) return a1080 > b1080;

        return false;
    });

    return filtered;
}

} // namespace Stremio
