#include "relay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((std::size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((std::size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

struct Url {
    bool secure = false;
    std::string host;
    int port = 0;
    std::string path;
};

bool parseUrl(const std::string& raw, Url& u) {
    std::string s = util::trim(raw);
    std::string low = util::toLower(s);
    if (util::startsWith(low, "https://")) {
        u.secure = true;
        s = s.substr(8);
    } else if (util::startsWith(low, "http://")) {
        u.secure = false;
        s = s.substr(7);
    } else {
        return false;
    }
    std::size_t slash = s.find('/');
    std::string hostport = slash == std::string::npos ? s : s.substr(0, slash);
    u.path = slash == std::string::npos ? std::string() : s.substr(slash);
    while (!u.path.empty() && u.path[u.path.size() - 1] == '/') u.path.erase(u.path.size() - 1);
    std::size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        u.host = hostport.substr(0, colon);
        u.port = std::atoi(hostport.c_str() + colon + 1);
    } else {
        u.host = hostport;
    }
    if (u.host.empty()) return false;
    if (u.port <= 0 || u.port > 65535) u.port = u.secure ? 443 : 80;
    return true;
}

bool readAllBody(HINTERNET req, std::string& out) {
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) return false;
        if (avail == 0) return true;
        std::vector<char> buf((std::size_t)avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &got)) return false;
        if (got == 0) return true;
        out.append(buf.data(), (std::size_t)got);
        if (out.size() > 64u * 1024u * 1024u) return true;
    }
}

bool findUsage(const std::string& text, long long& prompt, long long& completion) {
    std::size_t pos = text.rfind("\"usage\"");
    if (pos == std::string::npos) return false;
    std::size_t brace = text.find('{', pos);
    if (brace == std::string::npos) return false;
    int depth = 0;
    bool instr = false;
    bool esc = false;
    std::size_t end = std::string::npos;
    for (std::size_t i = brace; i < text.size(); ++i) {
        char c = text[i];
        if (instr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') instr = false;
        } else {
            if (c == '"') instr = true;
            else if (c == '{') ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0) {
                    end = i;
                    break;
                }
            }
        }
    }
    if (end == std::string::npos) return false;
    json::Value v;
    std::string err;
    if (!json::Value::parse(text.substr(brace, end - brace + 1), v, &err)) return false;
    if (!v.isObject()) return false;
    long long p = -1;
    long long c2 = -1;
    if (v.has("prompt_tokens")) p = v.get("prompt_tokens").asInt(0);
    if (v.has("completion_tokens")) c2 = v.get("completion_tokens").asInt(0);
    if (p < 0 && v.has("input_tokens")) p = v.get("input_tokens").asInt(0);
    if (c2 < 0 && v.has("output_tokens")) c2 = v.get("output_tokens").asInt(0);
    if (p < 0 && c2 < 0) return false;
    if (p < 0) p = 0;
    if (c2 < 0) c2 = 0;
    prompt = p;
    completion = c2;
    return true;
}

std::string contentTypeOf(HINTERNET req) {
    wchar_t buf[256];
    DWORD len = sizeof(buf);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                             buf, &len, WINHTTP_NO_HEADER_INDEX)) {
        return "application/json; charset=utf-8";
    }
    std::wstring w(buf, len / sizeof(wchar_t));
    while (!w.empty() && (w.back() == L'\0' || w.back() == L' ')) w.pop_back();
    std::string s = narrow(w);
    return s.empty() ? std::string("application/json; charset=utf-8") : s;
}

int statusOf(HINTERNET req) {
    DWORD status = 0;
    DWORD len = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    return (int)status;
}

std::string joinUrl(const Url& u, const std::string& suffix) {
    std::string base = u.path;
    if (base.size() >= 3 && util::endsWith(base, "/v1") && util::startsWith(suffix, "/v1/")) {
        base = base.substr(0, base.size() - 3);
    }
    return base + suffix;
}

} // namespace

RelayOutcome relayUpstream(SOCKET client, const std::string& method, const std::string& targetPath,
                           const std::string& query, const std::string& body,
                           const ApiEndpoint& api, bool streamMode, bool deferSend) {
    RelayOutcome out;
    Url u;
    if (!parseUrl(api.baseUrl, u)) {
        out.error = "API 地址格式无效";
        return out;
    }
    if (api.apiKey.empty()) {
        out.error = "该 API 未配置密钥";
        return out;
    }

    std::wstring agent = L"ai-relay/" + widen(RELAY_VERSION);
    HINTERNET ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        out.error = "初始化 WinHTTP 失败";
        return out;
    }
    WinHttpSetTimeouts(ses, 15000, 15000, 120000, 600000);

    std::wstring whost = widen(u.host);
    HINTERNET con = WinHttpConnect(ses, whost.c_str(), (INTERNET_PORT)u.port, 0);
    if (!con) {
        WinHttpCloseHandle(ses);
        out.error = "无法连接上游主机";
        return out;
    }

    std::string rel = joinUrl(u, targetPath);
    if (!query.empty()) rel += "?" + query;
    std::wstring wrel = widen(rel);
    std::wstring wmethod = widen(method);

    HINTERNET req = WinHttpOpenRequest(con, wmethod.c_str(), wrel.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        out.error = "创建上游请求失败";
        return out;
    }

    std::string hdrs = "Content-Type: application/json\r\nAuthorization: Bearer " + api.apiKey +
                       "\r\nAccept: " + (streamMode ? "text/event-stream" : "application/json") + "\r\n";
    std::wstring wh = widen(hdrs);
    BOOL sent = WinHttpSendRequest(req, wh.c_str(), (DWORD)-1,
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                   (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        out.error = "上游服务无响应";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return out;
    }

    out.status = statusOf(req);
    std::string ctype = contentTypeOf(req);
    bool success = out.status >= 200 && out.status < 300;

    if (streamMode && success) {
        std::string head = "HTTP/1.1 " + std::to_string(out.status) + " " + httpReason(out.status) + "\r\n";
        head += "Content-Type: " + ctype + "\r\n";
        head += "Cache-Control: no-cache\r\n";
        head += "X-Accel-Buffering: no\r\n";
        head += "Transfer-Encoding: chunked\r\n";
        head += "Connection: close\r\n\r\n";
        if (!sendAll(client, head)) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            out.started = true;
            out.ok = true;
            return out;
        }
        out.started = true;
        out.ok = true;
        std::string capture;
        bool clientAlive = true;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            std::vector<char> buf((std::size_t)avail);
            DWORD got = 0;
            if (!WinHttpReadData(req, buf.data(), avail, &got) || got == 0) break;
            if (clientAlive) {
                char head2[32];
                std::snprintf(head2, sizeof(head2), "%x\r\n", (unsigned)got);
                if (!sendAll(client, head2) || !sendAll(client, buf.data(), (std::size_t)got) ||
                    !sendAll(client, "\r\n", 2)) {
                    clientAlive = false;
                }
            }
            if (capture.size() < 32u * 1024u * 1024u) capture.append(buf.data(), (std::size_t)got);
        }
        if (clientAlive) {
            sendAll(client, "0\r\n\r\n", 5);
        }
        long long p = 0;
        long long c2 = 0;
        if (findUsage(capture, p, c2)) {
            out.promptTokens = p;
            out.completionTokens = c2;
            out.usageFound = true;
        } else {
            out.promptTokens = util::estimateTokens(body);
            out.completionTokens = util::estimateTokens(capture);
        }
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return out;
    }

    std::string resp;
    readAllBody(req, resp);
    out.started = true;
    out.ok = success;
    out.body = resp;
    out.contentType = ctype;
    if (success) {
        long long p = 0;
        long long c2 = 0;
        if (findUsage(resp, p, c2)) {
            out.promptTokens = p;
            out.completionTokens = c2;
            out.usageFound = true;
        } else {
            out.promptTokens = util::estimateTokens(body);
            out.completionTokens = util::estimateTokens(resp);
        }
    } else {
        out.error = resp.size() > 400 ? resp.substr(0, 400) : resp;
    }
    if (deferSend) {
        out.deferred = true;   // 由调用方决定是否发送（失败时可以改投下一个上游）
    } else {
        sendResponse(client, out.status > 0 ? out.status : 502, ctype, resp);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return out;
}

bool fetchModels(const std::string& baseUrl, const std::string& apiKey,
                 std::vector<std::string>& out, int& status, std::string& err) {
    out.clear();
    status = 0;
    err.clear();
    Url u;
    if (!parseUrl(baseUrl, u)) {
        err = "地址格式无效，请以 http:// 或 https:// 开头";
        return false;
    }
    std::wstring agent = L"ai-relay/" + widen(RELAY_VERSION);
    HINTERNET ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        err = "初始化 WinHTTP 失败";
        return false;
    }
    WinHttpSetTimeouts(ses, 8000, 8000, 15000, 20000);
    std::wstring whost = widen(u.host);
    HINTERNET con = WinHttpConnect(ses, whost.c_str(), (INTERNET_PORT)u.port, 0);
    if (!con) {
        WinHttpCloseHandle(ses);
        err = "无法连接该主机";
        return false;
    }
    std::string rel = joinUrl(u, "/models");
    std::wstring wrel = widen(rel);
    HINTERNET req = WinHttpOpenRequest(con, L"GET", wrel.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        err = "创建请求失败";
        return false;
    }
    std::string hdrs = "Authorization: Bearer " + apiKey + "\r\nAccept: application/json\r\n";
    std::wstring wh = widen(hdrs);
    BOOL sent = WinHttpSendRequest(req, wh.c_str(), (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        err = "请求发送失败";
        return false;
    }
    status = statusOf(req);
    std::string resp;
    readAllBody(req, resp);

    bool ok = false;
    if (status != 200) {
        std::string snippet = resp.size() > 200 ? resp.substr(0, 200) : resp;
        err = "HTTP " + std::to_string(status);
        if (!snippet.empty()) err += " " + snippet;
    } else {
        json::Value v;
        std::string perr;
        if (!json::Value::parse(resp, v, &perr)) {
            err = "上游返回内容不是有效 JSON";
        } else {
            const json::Value& data = v.isArray() ? v : v.get("data");
            if (!data.isArray()) {
                err = "上游返回中没有模型列表";
            } else {
                std::set<std::string> seen;
                for (std::size_t i = 0; i < data.size() && i < 4000; ++i) {
                    const json::Value& item = data.at(i);
                    std::string id = item.isString() ? item.asString() : item.get("id").asString();
                    id = util::trim(id);
                    if (id.empty()) continue;
                    if (seen.insert(id).second) out.push_back(id);
                }
                ok = true;
            }
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

bool testUpstream(const std::string& baseUrl, const std::string& apiKey, int& status, std::string& info) {
    std::vector<std::string> models;
    std::string err;
    int st = 0;
    bool ok = fetchModels(baseUrl, apiKey, models, st, err);
    status = st;
    if (!ok) {
        info = err;
        return false;
    }
    info = "可用，共 " + std::to_string(models.size()) + " 个模型";
    if (!models.empty()) info += "，例如 " + models[0];
    return true;
}