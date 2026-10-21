#include "stremio/curl_stream.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace Stremio {

static const char* kStreamUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 JoyStream/0.1";

struct HeaderContext {
    int64_t totalSize = -1;
    bool acceptRanges = false;
};

static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t totalBytes = size * nitems;
    auto* ctx = static_cast<HeaderContext*>(userdata);
    std::string line(buffer, totalBytes);

    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.rfind("content-range:", 0) == 0) {
        // e.g. "Content-Range: bytes 0-0/1234567"
        auto slashPos = line.find('/');
        if (slashPos != std::string::npos && slashPos + 1 < line.size()) {
            std::string sizeStr = line.substr(slashPos + 1);
            try {
                ctx->totalSize = std::stoll(sizeStr);
            } catch (...) {}
        }
    } else if (lower.rfind("content-length:", 0) == 0 && ctx->totalSize <= 0) {
        auto colonPos = line.find(':');
        if (colonPos != std::string::npos && colonPos + 1 < line.size()) {
            std::string lenStr = line.substr(colonPos + 1);
            try {
                ctx->totalSize = std::stoll(lenStr);
            } catch (...) {}
        }
    } else if (lower.find("bytes") != std::string::npos && lower.rfind("accept-ranges:", 0) == 0) {
        ctx->acceptRanges = true;
    }

    return totalBytes;
}

CurlStream::CurlStream(const std::string& url, const std::map<std::string, std::string>& headers)
    : m_originalUrl(url), m_effectiveUrl(url), m_headers(headers) {
    for (const auto& [k, v] : m_headers) {
        std::string h = k + ": " + v;
        m_headerList = curl_slist_append(m_headerList, h.c_str());
    }
}

CurlStream::~CurlStream() {
    m_cancelled.store(true);
    if (m_curl) {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
    if (m_headerList) {
        curl_slist_free_all(m_headerList);
        m_headerList = nullptr;
    }
}

void CurlStream::setupCurlOptions(CURL* handle) {
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, kStreamUserAgent);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPINTVL, 15L);
    if (m_headerList) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, m_headerList);
    }
}

bool CurlStream::open() {
    m_curl = curl_easy_init();
    if (!m_curl) {
        m_lastError = "curl_easy_init failed";
        return false;
    }

    setupCurlOptions(m_curl);
    curl_easy_setopt(m_curl, CURLOPT_URL, m_originalUrl.c_str());

    // Probe initial 1-byte range (bytes=0-0)
    // Using GET with Range allows resolving redirects, getting status code,
    // and querying total file size from Content-Range in one lightweight call!
    curl_easy_setopt(m_curl, CURLOPT_RANGE, "0-0");

    HeaderContext hdrCtx;
    curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, &hdrCtx);

    std::vector<uint8_t> probeByte;
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, void* userp) -> size_t {
        auto* v = static_cast<std::vector<uint8_t>*>(userp);
        size_t b = size * nmemb;
        v->insert(v->end(), static_cast<uint8_t*>(ptr), static_cast<uint8_t*>(ptr) + b);
        return b;
    });
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &probeByte);

    CURLcode res = curl_easy_perform(m_curl);
    if (res != CURLE_OK) {
        m_lastError = "Connection failed: " + std::string(curl_easy_strerror(res));
        std::cerr << "[CurlStream] " << m_lastError << " (URL: " << m_originalUrl << ")" << std::endl;
        return false;
    }

    long httpCode = 0;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode >= 400) {
        if (httpCode == 403) {
            m_lastError = "HTTP 403: Access Denied / Expired Account";
        } else if (httpCode == 404) {
            m_lastError = "HTTP 404: Media Not Found";
        } else {
            m_lastError = "HTTP Error " + std::to_string(httpCode);
        }
        std::cerr << "[CurlStream] " << m_lastError << " for URL: " << m_originalUrl << std::endl;
        return false;
    }

    // Retrieve redirected effective URL
    char* eff = nullptr;
    curl_easy_getinfo(m_curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff && std::strlen(eff) > 0) {
        m_effectiveUrl = eff;
    }

    if (hdrCtx.totalSize > 0) {
        m_totalSize = hdrCtx.totalSize;
    } else {
        curl_off_t cl = -1;
        curl_easy_getinfo(m_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
        if (cl > 0) m_totalSize = cl;
    }

    m_seekable = (httpCode == 206 || hdrCtx.acceptRanges || m_totalSize > 0);

    // Reset buffer so the first read() fetches a full chunk
    m_buffer.clear();
    m_bufferStart = -1;
    m_bufferLen = 0;
    m_currentPos = 0;

    std::cout << "[CurlStream] Connected. Effective URL: " << m_effectiveUrl
              << " | Total Size: " << m_totalSize << " bytes | Seekable: " << (m_seekable ? "yes" : "no") << std::endl;
    return true;
}

bool CurlStream::fetchChunk(int64_t startOffset, size_t requestedBytes) {
    if (m_cancelled.load() || !m_curl) return false;

    size_t fetchSize = std::max(requestedBytes, kChunkSize);
    if (m_totalSize > 0 && startOffset + static_cast<int64_t>(fetchSize) > m_totalSize) {
        fetchSize = static_cast<size_t>(m_totalSize - startOffset);
    }
    if (fetchSize == 0) return false;

    int64_t endOffset = startOffset + static_cast<int64_t>(fetchSize) - 1;
    std::string rangeStr = std::to_string(startOffset) + "-" + std::to_string(endOffset);

    curl_easy_setopt(m_curl, CURLOPT_URL, m_effectiveUrl.c_str());
    curl_easy_setopt(m_curl, CURLOPT_RANGE, rangeStr.c_str());
    curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, nullptr);
    curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, nullptr);

    m_buffer.clear();
    m_buffer.reserve(fetchSize);

    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
        auto* self = static_cast<CurlStream*>(userp);
        if (self->m_cancelled.load()) return 0; // Abort
        size_t total = size * nmemb;
        auto* bytes = static_cast<const uint8_t*>(contents);
        self->m_buffer.insert(self->m_buffer.end(), bytes, bytes + total);
        return total;
    });
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);

    CURLcode res = curl_easy_perform(m_curl);
    if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        m_lastError = "Chunk fetch error: " + std::string(curl_easy_strerror(res));
        std::cerr << "[CurlStream] " << m_lastError << " at offset " << startOffset << std::endl;
        return false;
    }

    if (m_buffer.empty()) return false;

    m_bufferStart = startOffset;
    m_bufferLen = m_buffer.size();
    return true;
}

int64_t CurlStream::read(char* buf, uint64_t nbytes) {
    if (m_cancelled.load()) return -1;
    if (nbytes == 0) return 0;

    if (m_totalSize > 0 && m_currentPos >= m_totalSize) {
        return 0; // EOF
    }

    // If currentPos is not within buffer range, fetch chunk
    if (m_bufferStart < 0 || m_currentPos < m_bufferStart || m_currentPos >= m_bufferStart + static_cast<int64_t>(m_bufferLen)) {
        if (!fetchChunk(m_currentPos, static_cast<size_t>(nbytes))) {
            return 0; // EOF or error
        }
    }

    size_t offsetInBuffer = static_cast<size_t>(m_currentPos - m_bufferStart);
    size_t available = m_bufferLen - offsetInBuffer;
    size_t toCopy = std::min(static_cast<size_t>(nbytes), available);

    std::memcpy(buf, m_buffer.data() + offsetInBuffer, toCopy);
    m_currentPos += toCopy;
    return static_cast<int64_t>(toCopy);
}

int64_t CurlStream::seek(int64_t offset) {
    if (m_cancelled.load()) return -1;
    if (!m_seekable && offset != m_currentPos) {
        return -1; // MPV_ERROR_UNSUPPORTED
    }

    if (offset < 0) offset = 0;
    if (m_totalSize > 0 && offset > m_totalSize) {
        offset = m_totalSize;
    }

    m_currentPos = offset;
    return m_currentPos;
}

void CurlStream::cancel() {
    m_cancelled.store(true);
}

int64_t CurlStream::mpvRead(void* cookie, char* buf, uint64_t nbytes) {
    auto* s = static_cast<CurlStream*>(cookie);
    return s ? s->read(buf, nbytes) : -1;
}

int64_t CurlStream::mpvSeek(void* cookie, int64_t offset) {
    auto* s = static_cast<CurlStream*>(cookie);
    return s ? s->seek(offset) : -1;
}

int64_t CurlStream::mpvSize(void* cookie) {
    auto* s = static_cast<CurlStream*>(cookie);
    if (!s || s->getSize() <= 0) return -1;
    return s->getSize();
}

void CurlStream::mpvClose(void* cookie) {
    auto* s = static_cast<CurlStream*>(cookie);
    delete s;
}

void CurlStream::mpvCancel(void* cookie) {
    auto* s = static_cast<CurlStream*>(cookie);
    if (s) s->cancel();
}

} // namespace Stremio
