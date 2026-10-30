#include "relay.h"

#include <cstdio>
#include <cstring>
#include <sstream>

std::string HttpRequest::header(const std::string& name) const {
    std::string key = util::toLower(name);
    std::map<std::string, std::string>::const_iterator it = headers.find(key);
    if (it == headers.end()) return std::string();
    return it->second;
}

std::string httpReason(int code) { return util::reasonPhrase(code); }

bool sendAll(SOCKET s, const void* data, std::size_t len) {
    const char* p = (const char*)data;
    std::size_t sent = 0;
    while (sent < len) {
        int n = ::send(s, p + sent, (int)(len - sent), 0);
        if (n <= 0) return false;
        sent += (std::size_t)n;
    }
    return true;
}

bool sendAll(SOCKET s, const std::string& text) {
    if (text.empty()) return true;
    return sendAll(s, text.data(), text.size());
}

void sendResponse(SOCKET s, int code, const std::string& contentType, const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& extra) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << code << " " << httpReason(code) << "\r\n";
    ss << "Server: ai-relay\r\n";
    if (!contentType.empty()) ss << "Content-Type: " << contentType << "\r\n";
    ss << "Content-Length: " << body.size() << "\r\n";
    ss << "Cache-Control: no-store\r\n";
    ss << "X-Content-Type-Options: nosniff\r\n";
    ss << "Connection: close\r\n";
    for (std::size_t i = 0; i < extra.size(); ++i) {
        ss << extra[i].first << ": " << extra[i].second << "\r\n";
    }
    ss << "\r\n";
    std::string head = ss.str();
    if (!sendAll(s, head)) return;
    sendAll(s, body);
}

namespace {

bool readExact(SOCKET s, char* buf, std::size_t len) {
    std::size_t got = 0;
    while (got < len) {
        int n = ::recv(s, buf + got, (int)(len - got), 0);
        if (n <= 0) return false;
        got += (std::size_t)n;
    }
    return true;
}

bool readChunked(SOCKET s, std::string& out, std::size_t limit) {
    std::string line;
    for (;;) {
        line.clear();
        char c = 0;
        for (;;) {
            int n = ::recv(s, &c, 1, 0);
            if (n <= 0) return false;
            if (c == '\n') break;
            if (c != '\r') line.push_back(c);
            if (line.size() > 64) return false;
        }
        if (line.empty()) continue;
        unsigned long long size = 0;
        std::stringstream ss;
        ss << std::hex << line;
        ss >> size;
        if (size == 0) {
            // trailing headers
            for (;;) {
                line.clear();
                char c2 = 0;
                for (;;) {
                    int n = ::recv(s, &c2, 1, 0);
                    if (n <= 0) return true;
                    if (c2 == '\n') break;
                    if (c2 != '\r') line.push_back(c2);
                }
                if (line.empty()) return true;
            }
        }
        if (out.size() + size > limit) return false;
        std::vector<char> buf((std::size_t)size);
        if (size && !readExact(s, buf.data(), (std::size_t)size)) return false;
        out.append(buf.data(), (std::size_t)size);
        char crlf[2];
        if (!readExact(s, crlf, 2)) return false;
    }
}

} // namespace

bool readRequest(SOCKET s, HttpRequest& req, std::string& err) {
    std::string data;
    data.reserve(4096);
    char buf[4096];
    std::size_t headEnd = std::string::npos;

    while (headEnd == std::string::npos) {
        int n = ::recv(s, buf, (int)sizeof(buf), 0);
        if (n <= 0) {
            err = "connection closed";
            return false;
        }
        data.append(buf, (std::size_t)n);
        headEnd = data.find("\r\n\r\n");
        if (data.size() > 256 * 1024) {
            err = "request header too large";
            return false;
        }
    }

    std::string head = data.substr(0, headEnd);
    std::string rest = data.substr(headEnd + 4);

    std::vector<std::string> lines = util::split(head, '\n');
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        if (i == 0) {
            std::vector<std::string> parts = util::split(line, ' ');
            if (parts.size() < 3) {
                err = "bad request line";
                return false;
            }
            req.method = parts[0];
            req.target = parts[1];
            req.version = parts[2];
            continue;
        }
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = util::toLower(util::trim(line.substr(0, colon)));
        std::string value = util::trim(line.substr(colon + 1));
        req.headers[name] = value;
    }

    std::size_t qpos = req.target.find('?');
    if (qpos == std::string::npos) {
        req.path = req.target;
    } else {
        req.path = req.target.substr(0, qpos);
        req.query = req.target.substr(qpos + 1);
    }

    std::string te = req.header("transfer-encoding");
    std::string cl = req.header("content-length");
    const std::size_t kMaxBody = 64u * 1024u * 1024u;

    if (!te.empty() && util::toLower(te).find("chunked") != std::string::npos) {
        // feed already-received bytes through a small adapter is complex; fall back to
        // a simple inline parser when the whole chunked body already arrived.
        std::string body = rest;
        // If terminator present, parse in-memory.
        std::size_t pos = 0;
        bool done = false;
        std::string out;
        while (pos < body.size()) {
            std::size_t eol = body.find("\r\n", pos);
            if (eol == std::string::npos) break;
            std::string szline = body.substr(pos, eol - pos);
            pos = eol + 2;
            unsigned long long size = 0;
            std::stringstream ss;
            ss << std::hex << szline;
            ss >> size;
            if (size == 0) {
                done = true;
                break;
            }
            if (pos + size + 2 > body.size()) break;
            out.append(body, pos, (std::size_t)size);
            pos += (std::size_t)size + 2;
        }
        if (!done) {
            if (!readChunked(s, out, kMaxBody)) {
                err = "bad chunked body";
                return false;
            }
        }
        req.body = out;
        return true;
    }

    std::size_t want = 0;
    if (!cl.empty()) {
        want = (std::size_t)std::strtoull(cl.c_str(), nullptr, 10);
        if (want > kMaxBody) {
            err = "body too large";
            return false;
        }
    }
    req.body = rest;
    while (req.body.size() < want) {
        int n = ::recv(s, buf, (int)sizeof(buf), 0);
        if (n <= 0) {
            err = "connection closed in body";
            return false;
        }
        req.body.append(buf, (std::size_t)n);
        if (req.body.size() > kMaxBody) {
            err = "body too large";
            return false;
        }
    }
    if (req.body.size() > want) req.body.resize(want);
    return true;
}