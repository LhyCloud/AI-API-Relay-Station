#include "relay.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>

namespace util {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string toLower(const std::string& s) {
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = (char)std::tolower((unsigned char)out[i]);
    return out;
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(s[i]);
    }
    out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string randomHex(std::size_t bytes) {
    static std::mt19937_64 rng = []() {
        std::random_device rd;
        unsigned long long a = (unsigned long long)rd();
        unsigned long long b = (unsigned long long)rd();
        unsigned long long c = (unsigned long long)GetTickCount64();
        return std::mt19937_64((a << 32) ^ b ^ c);
    }();
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        unsigned int v = (unsigned int)(rng() & 0xFFu);
        out.push_back(hex[v >> 4]);
        out.push_back(hex[v & 0x0F]);
    }
    return out;
}

std::string randomKey() {
    return "sk-" + randomHex(24);
}

long long nowSeconds() {
    return (long long)std::time(nullptr);
}

std::string dateKey(long long ts) {
    std::time_t t = (std::time_t)ts;
    std::tm tmv;
    localtime_s(&tmv, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

std::string dateKeyToday() {
    return dateKey(nowSeconds());
}

std::string timeString(long long ts) {
    std::time_t t = (std::time_t)ts;
    std::tm tmv;
    localtime_s(&tmv, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

std::string exeDir() {
    char buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof(buf));
    if (n == 0) return std::string(".");
    std::string full(buf, n);
    std::size_t pos = full.find_last_of("\\/");
    if (pos == std::string::npos) return std::string(".");
    return full.substr(0, pos);
}

std::string readFile(const std::string& path, bool* ok) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) { if (ok) *ok = false; return std::string(); }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (ok) *ok = true;
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        f.flush();
        if (!f) return false;
    }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') { out.push_back(' '); continue; }
        if (c == '%' && i + 2 < s.size()) {
            auto hexv = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            int hi = hexv(s[i + 1]);
            int lo = hexv(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

long long estimateTokens(const std::string& text) {
    long long n = (long long)((text.size() + 3) / 4);
    return n < 1 ? 1 : n;
}

std::string reasonPhrase(int code) {
    switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "Unknown";
    }
}

std::string formatCount(long long n) {
    if (n < 1000) return std::to_string(n);
    std::string s = std::to_string(n);
    std::string out;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out.push_back(s[(std::size_t)i]);
        if (++c % 3 == 0 && i != 0) out.push_back(',');
    }
    std::string rev(out.rbegin(), out.rend());
    return rev;
}

/* ---------------- sha-256 / password hash ---------------- */

namespace {

inline unsigned int rotr(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256Block(unsigned int h[8], const unsigned char* p) {
    static const unsigned int K[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    unsigned int w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) |
               ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        unsigned int s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    unsigned int a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned int e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        unsigned int S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        unsigned int ch = (e & f) ^ ((~e) & g);
        unsigned int t1 = hh + S1 + ch + K[i] + w[i];
        unsigned int S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        unsigned int mj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int t2 = S0 + mj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace

std::string sha256Hex(const std::string& data) {
    unsigned int h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                         0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    std::size_t len = data.size();
    std::size_t full = len / 64;
    const unsigned char* p = (const unsigned char*)data.data();
    for (std::size_t i = 0; i < full; ++i) sha256Block(h, p + i * 64);

    unsigned char tail[128];
    std::size_t rem = len - full * 64;
    std::memset(tail, 0, sizeof(tail));
    if (rem) std::memcpy(tail, p + full * 64, rem);
    tail[rem] = 0x80;
    std::size_t tailLen = (rem < 56) ? 64 : 128;
    unsigned long long bits = (unsigned long long)len * 8ull;
    for (int i = 0; i < 8; ++i) {
        tail[tailLen - 1 - (std::size_t)i] = (unsigned char)((bits >> (8 * i)) & 0xFFu);
    }
    sha256Block(h, tail);
    if (tailLen == 128) sha256Block(h, tail + 64);

    char out[65];
    for (int i = 0; i < 8; ++i) {
        std::snprintf(out + i * 8, 9, "%08x", h[i]);
    }
    out[64] = 0;
    return std::string(out, 64);
}

std::string kdfHash(const std::string& saltHex, const std::string& password) {
    std::string h = sha256Hex(saltHex + password);
    for (int i = 0; i < 2000; ++i) h = sha256Hex(h + saltHex);
    return h;
}

bool secureEquals(const std::string& a, const std::string& b) {
    unsigned char diff = (unsigned char)(a.size() ^ b.size());
    std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

std::string beijingDayKey(long long ts) {
    std::time_t t = (std::time_t)(ts + 8 * 3600);
    std::tm tmv;
    gmtime_s(&tmv, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

// 每日重置日界 = 北京时间 0 点 + offsetMinutes（例如 240 表示凌晨 4 点重置）
std::string shiftedDayKey(long long ts, int offsetMinutes) {
    return beijingDayKey(ts + (long long)offsetMinutes * 60);
}

namespace {
bool parseV4(const std::string& s, unsigned int& out) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    char extra = 0;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}
} // namespace

bool ipMatches(const std::string& pattern, const std::string& ip) {
    std::string p = trim(pattern);
    if (p.empty()) return false;
    if (p == "*") return true;
    unsigned int pv = 0, iv = 0;
    std::size_t slash = p.find('/');
    if (slash == std::string::npos) {
        if (parseV4(p, pv) && parseV4(ip, iv)) return pv == iv;
        return p == ip;
    }
    if (!parseV4(p.substr(0, slash), pv) || !parseV4(ip, iv)) return false;
    int bits = std::atoi(p.c_str() + slash + 1);
    if (bits < 0) bits = 0;
    if (bits > 32) bits = 32;
    unsigned int mask = bits == 0 ? 0u : (0xFFFFFFFFu << (32 - bits));
    return (pv & mask) == (iv & mask);
}

bool isLoopbackIp(const std::string& ip) {
    return ip == "::1" || startsWith(ip, "127.");
}

} // namespace util
