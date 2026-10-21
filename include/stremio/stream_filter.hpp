#pragma once

#include "stremio/models.hpp"
#include <vector>
#include <string>

namespace Stremio {

enum class StreamFilterMode {
    SWITCH_OPTIMIZED = 0,
    ONLY_1080P,
    ONLY_720P,
    CUSTOM_REGEX,
    SHOW_ALL,
    COUNT
};

struct FilterConfig {
    StreamFilterMode mode = StreamFilterMode::SWITCH_OPTIMIZED;
    std::string customExcludeRegex = R"(\b(4k|2160p|cam|camrip|ts|telesync|hdcam|hd-ts|remux|av1)\b)";
    std::string customIncludeRegex = "";
    float maxFileSizeGB = 25.0f;
};

class StreamFilter {
public:
    static std::vector<StreamItem> filterAndRank(const std::vector<StreamItem>& streams, const FilterConfig& config);
    static std::string getModeName(StreamFilterMode mode);
    static std::string getModeDescription(StreamFilterMode mode);
    static StreamFilterMode cycleMode(StreamFilterMode current);
    static float extractFileSizeGB(const std::string& text);
    static bool matchesRegex(const std::string& text, const std::string& pattern);
};

} // namespace Stremio
