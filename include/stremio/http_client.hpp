#pragma once

#include <string>
#include <vector>
#include <map>

namespace Stremio {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool success = false;
    std::string error;
};

class HttpClient {
public:
    static bool initializeNetwork();
    static void cleanupNetwork();

    static HttpResponse get(const std::string& url,
                            const std::map<std::string, std::string>& headers = {},
                            long timeoutSeconds = 15);

    static std::string urlEncode(const std::string& value);
};

} // namespace Stremio
