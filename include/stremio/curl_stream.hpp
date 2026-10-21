#pragma once

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <cstdint>
#include <curl/curl.h>

namespace Stremio {

class CurlStream {
public:
    CurlStream(const std::string& url, const std::map<std::string, std::string>& headers = {});
    ~CurlStream();

    bool open();
    int64_t read(char* buf, uint64_t nbytes);
    int64_t seek(int64_t offset);
    int64_t getSize() const { return m_totalSize; }
    void cancel();

    const std::string& getLastError() const { return m_lastError; }
    const std::string& getEffectiveUrl() const { return m_effectiveUrl; }

    // Callbacks passed to mpv_stream_cb_info
    static int64_t mpvRead(void* cookie, char* buf, uint64_t nbytes);
    static int64_t mpvSeek(void* cookie, int64_t offset);
    static int64_t mpvSize(void* cookie);
    static void mpvClose(void* cookie);
    static void mpvCancel(void* cookie);

private:
    bool fetchChunk(int64_t startOffset, size_t requestedBytes);
    void setupCurlOptions(CURL* handle);

    std::string m_originalUrl;
    std::string m_effectiveUrl;
    std::map<std::string, std::string> m_headers;
    struct curl_slist* m_headerList = nullptr;

    CURL* m_curl = nullptr;
    int64_t m_currentPos = 0;
    int64_t m_totalSize = -1;
    bool m_seekable = true;
    std::atomic<bool> m_cancelled{false};
    std::string m_lastError;

    // Buffer for chunked playback (1MB buffer)
    static constexpr size_t kChunkSize = 1024 * 1024;
    std::vector<uint8_t> m_buffer;
    int64_t m_bufferStart = -1;
    size_t m_bufferLen = 0;
};

} // namespace Stremio
