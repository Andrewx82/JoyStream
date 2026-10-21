#include "stremio/http_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <iomanip>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Stremio {

static bool s_networkInitialized = false;

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* mem = static_cast<std::string*>(userp);
    mem->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool HttpClient::initializeNetwork() {
    if (s_networkInitialized) return true;

#ifdef __SWITCH__
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        std::cerr << "Failed to initialize switch socket service: 0x" << std::hex << rc << std::endl;
        return false;
    }
#endif

    curl_global_init(CURL_GLOBAL_ALL);
    s_networkInitialized = true;
    return true;
}

void HttpClient::cleanupNetwork() {
    if (!s_networkInitialized) return;

    curl_global_cleanup();

#ifdef __SWITCH__
    socketExit();
#endif

    s_networkInitialized = false;
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             long timeoutSeconds) {
    if (!s_networkInitialized) {
        initializeNetwork();
    }

    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL handle";
        return response;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& [key, val] : headers) {
        std::string headerStr = key + ": " + val;
        chunk = curl_slist_append(chunk, headerStr.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "JoyStream/0.1 (Nintendo Switch)");

    // On Switch homebrew, cert bundles may be missing or system clocks skewed by emuMMC
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
        response.success = false;
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response.statusCode = static_cast<int>(http_code);
        response.success = (response.statusCode >= 200 && response.statusCode < 400);
    }

    if (chunk) {
        curl_slist_free_all(chunk);
    }
    curl_easy_cleanup(curl);

    return response;
}

std::string HttpClient::urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

} // namespace Stremio
