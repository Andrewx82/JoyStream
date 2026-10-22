#pragma once

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
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
    void startPrefetch(int64_t offset);
    void prefetchWorker();

    std::string m_originalUrl;
    std::string m_effectiveUrl;
    std::map<std::string, std::string> m_headers;
    struct curl_slist* m_headerList = nullptr;

    CURL* m_curl = nullptr;
    CURL* m_prefetchCurl = nullptr;

    int64_t m_currentPos = 0;
    int64_t m_totalSize = -1;
    bool m_seekable = true;
    std::atomic<bool> m_cancelled{false};
    std::string m_lastError;

    // Active buffer (4MB chunk size)
    static constexpr size_t kChunkSize = 4 * 1024 * 1024;
    std::vector<uint8_t> m_buffer;
    int64_t m_bufferStart = -1;
    size_t m_bufferLen = 0;

    // Background prefetch buffer (4MB)
    std::vector<uint8_t> m_prefetchBuffer;
    int64_t m_prefetchStart = -1;
    size_t m_prefetchLen = 0;
    std::atomic<bool> m_prefetchReady{false};

    std::thread m_prefetchThread;
    std::mutex m_prefetchMutex;
    std::condition_variable m_prefetchCv;
    int64_t m_pendingPrefetchOffset = -1;
    bool m_stopPrefetchThread = false;
};

} // namespace Stremio
