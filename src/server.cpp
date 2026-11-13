#include "relay.h"

#include <thread>
#include <deque>
#include <set>
#include <cstdio>

namespace {

std::mutex g_logMu;
std::deque<std::string> g_logs;
const std::size_t kMaxLogs = 300;

std::atomic<bool> g_stop(false);
std::atomic<bool> g_rebind(false);
std::atomic<int> g_active(0);

std::mutex g_sessMu;
std::map<std::string, long long> g_sessions;

/* 登录限速 / 挑战 */
std::mutex g_loginMu;
struct LoginThrottle {
    long long nextAllowed = 0;
    int fails = 0;
};
std::map<std::string, LoginThrottle> g_loginByIp;
long long g_loginPenalty = 0;   // 累计惩罚秒数
bool g_loginLocked = false;     // 超过 1 小时，锁定到下次启动
std::map<std::string, long long> g_nonces;  // nonce -> 过期时间

// 从查询串里取一个整数参数（"/api/config?days=30&offset=14"）
static int queryInt(const std::string& q, const std::string& key, int def) {
    std::size_t pos = 0;
    while (pos < q.size()) {
        std::size_t amp = q.find('&', pos);
        std::string kv = q.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        std::size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key) {
            std::string val = kv.substr(eq + 1);
            int out = def;
            for (std::size_t i = 0; i < val.size(); ++i) {
                if (val[i] < '0' || val[i] > '9') return def;
            }
            if (!val.empty()) {
                long long n = 0;
                for (std::size_t i = 0; i < val.size(); ++i) n = n * 10 + (val[i] - '0');
                out = (int)n;
            }
            return out;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return def;
}

std::string notFoundBody() { return "Not Found"; }

void notFound(SOCKET s) {
    sendResponse(s, 404, "text/plain; charset=utf-8", notFoundBody());
}

void jsonError(SOCKET s, int status, const std::string& message, const std::string& type,
               const std::string& code = std::string()) {
    json::Value root = json::Value::object();
    json::Value err = json::Value::object();
    err.set("message", json::Value::str(message));
    err.set("type", json::Value::str(type));
    if (!code.empty()) err.set("code", json::Value::str(code));
    root.set("error", err);
    sendResponse(s, status, "application/json; charset=utf-8", root.dump(0));
}

bool ipInList(const std::vector<std::string>& list, const std::string& ip) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (util::ipMatches(list[i], ip)) return true;
    }
    return false;
}

bool panelAllowed(const HttpRequest& req, const Config& cfg) {
    if (cfg.settings.panelPublic) return true;
    return util::isLoopbackIp(req.remoteIp);
}

std::string issueNonce() {
    std::string n = util::randomHex(16);
    std::lock_guard<std::mutex> lock(g_loginMu);
    long long now = util::nowSeconds();
    for (std::map<std::string, long long>::iterator it = g_nonces.begin(); it != g_nonces.end();) {
        if (it->second < now) it = g_nonces.erase(it);
        else ++it;
    }
    g_nonces[n] = now + 120;
    return n;
}

bool consumeNonce(const std::string& n) {
    if (n.empty()) return false;
    std::lock_guard<std::mutex> lock(g_loginMu);
    std::map<std::string, long long>::iterator it = g_nonces.find(n);
    if (it == g_nonces.end()) return false;
    bool ok = it->second >= util::nowSeconds();
    g_nonces.erase(it);
    return ok;
}

bool loginBlocked(const std::string& ip, const Config& cfg) {
    if (util::isLoopbackIp(ip)) return false;
    bool whitelisted = ipInList(cfg.settings.ipWhitelist, ip);
    std::lock_guard<std::mutex> lock(g_loginMu);
    if (!whitelisted && g_loginLocked) return true;
    std::map<std::string, LoginThrottle>::const_iterator it = g_loginByIp.find(ip);
    if (it == g_loginByIp.end()) return false;
    return util::nowSeconds() < it->second.nextAllowed;
}

void recordLoginFailure(const std::string& ip, const Config& cfg) {
    if (util::isLoopbackIp(ip)) return;   // 本机不计数、不延迟、不锁定
    bool whitelisted = ipInList(cfg.settings.ipWhitelist, ip);
    std::lock_guard<std::mutex> lock(g_loginMu);
    LoginThrottle& st = g_loginByIp[ip];
    st.fails++;
    int k = st.fails - 1;
    if (k > 4) k = 4;
    long long d = 1LL << k;   // 1, 2, 4, 8, 16
    st.nextAllowed = util::nowSeconds() + d;
    if (!whitelisted) {
        g_loginPenalty += d;
        if (!g_loginLocked && g_loginPenalty > 3600) {
            g_loginLocked = true;
        }
    }
}

std::string newSession() {
    std::string token = util::randomHex(20);
    std::lock_guard<std::mutex> lock(g_sessMu);
    long long now = util::nowSeconds();
    for (std::map<std::string, long long>::iterator it = g_sessions.begin(); it != g_sessions.end();) {
        if (it->second < now) g_sessions.erase(it++);
        else ++it;
    }
    g_sessions[token] = now + 7 * 86400;
    return token;
}

std::string sessionOf(const HttpRequest& req) {
    std::string cookie = req.header("cookie");
    if (cookie.empty()) return std::string();
    std::vector<std::string> parts = util::split(cookie, ';');
    for (std::size_t i = 0; i < parts.size(); ++i) {
        std::string p = util::trim(parts[i]);
        std::size_t eq = p.find('=');
        if (eq == std::string::npos) continue;
        std::string name = util::trim(p.substr(0, eq));
        std::string value = util::trim(p.substr(eq + 1));
        if (name == "relay_session") return value;
    }
    return std::string();
}

bool adminOk(const HttpRequest& req) {
    std::string sid = sessionOf(req);
    if (sid.empty()) return false;
    std::lock_guard<std::mutex> lock(g_sessMu);
    std::map<std::string, long long>::iterator it = g_sessions.find(sid);
    if (it == g_sessions.end()) return false;
    if (it->second < util::nowSeconds()) {
        g_sessions.erase(it);
        return false;
    }
    return true;
}

const Group* findGroup(const Config& cfg, const std::string& id) {
    for (std::size_t i = 0; i < cfg.groups.size(); ++i) {
        if (cfg.groups[i].id == id) return &cfg.groups[i];
    }
    return nullptr;
}

bool apiHasModel(const ApiEndpoint& api, const std::string& model) {
    for (std::size_t i = 0; i < api.models.size(); ++i) {
        if (api.models[i] == model) return true;
    }
    return false;
}

struct Route {
    bool ok = false;
    const Group* group = nullptr;
    const ApiEndpoint* api = nullptr;
    std::string model;    // 上游模型名
    std::string exposed;  // 对外名称（不含分组前缀）
    bool trippedFallback = false;  // 组内 API 全部熔断，回落第一个
};

std::string exposedName(const ApiEndpoint& a, const std::string& upstream) {
    std::map<std::string, std::string>::const_iterator it = a.alias.find(upstream);
    return it == a.alias.end() ? upstream : it->second;
}

bool apiOverQuota(Store& store, const ApiEndpoint& a) {
    if (a.dailyLimitM <= 0) return false;
    long long limit = (long long)(a.dailyLimitM * 1000000.0 + 0.5);
    return store.apiUsedToday(a.id) >= limit;
}

int failCountOf(const std::map<std::string, int>& fails, const std::string& apiId) {
    std::map<std::string, int>::const_iterator it = fails.find(apiId);
    return it == fails.end() ? 0 : it->second;
}

// 该 API 是否提供对外名称为 wanted 的模型；成立时给出上游真名
bool apiServes(const ApiEndpoint& a, const std::string& wanted, std::string& upstream) {
    if (a.models.empty()) {   // 未配置模型清单：按同名直接透传
        upstream = wanted;
        return true;
    }
    for (std::size_t m = 0; m < a.models.size(); ++m) {
        if (exposedName(a, a.models[m]) == wanted) {
            upstream = a.models[m];
            return true;
        }
    }
    return false;
}

/* 选路规则
 * 1. 模型名必须带分组前缀；不带前缀一律视为模型不存在（不自动挑组）
 * 2. 前缀只认该用户被授权的分组，且按面板中自上而下的顺序取第一个匹配的分组
 * 3. 组内按 API 顺序取第一个「提供该对外名称 + 今日额度未用尽 + 未熔断」的
 * 4. 组内全部上游都熔断时，回落到组内第一个提供该模型的上游（不跨组）
 */
Route resolveRoute(const Config& cfg, const User& user, const std::string& requested,
                   Store& store, const std::map<std::string, int>& fails, bool& quotaBlocked) {
    Route r;
    quotaBlocked = false;
    const int failLimit = cfg.settings.failLimit;

    const Group* group = nullptr;
    std::string wanted;
    for (std::size_t i = 0; i < cfg.groups.size(); ++i) {
        const Group& g = cfg.groups[i];
        bool allowed = false;
        for (std::size_t k = 0; k < user.groupIds.size(); ++k) {
            if (user.groupIds[k] == g.id) {
                allowed = true;
                break;
            }
        }
        if (!allowed) continue;
        if (g.prefix.empty()) continue;
        if (requested.size() <= g.prefix.size()) continue;
        if (!util::startsWith(requested, g.prefix)) continue;
        group = &g;
        wanted = requested.substr(g.prefix.size());
        break;
    }
    if (!group) return r;   // 必须带组前缀，找不到就是 404

    Route fallback;
    for (std::size_t k = 0; k < group->apis.size(); ++k) {
        const ApiEndpoint& a = group->apis[k];
        std::string upstream;
        if (!apiServes(a, wanted, upstream)) continue;
        if (apiOverQuota(store, a)) {
            quotaBlocked = true;
            continue;
        }
        if (failLimit > 0 && failCountOf(fails, a.id) >= failLimit) {
            if (!fallback.ok) {
                fallback.ok = true;
                fallback.group = group;
                fallback.api = &a;
                fallback.model = upstream;
                fallback.exposed = wanted;
                fallback.trippedFallback = true;
            }
            continue;
        }
        r.ok = true;
        r.group = group;
        r.api = &a;
        r.model = upstream;
        r.exposed = wanted;
        return r;
    }

    if (fallback.ok) return fallback;
    return r;
}

// 组内下一个可用候选（跳过指定 API），用于 failLimit=0 时立即顺延一次
Route nextInGroup(const Group& g, const std::string& wanted, const std::string& skipApiId,
                  Store& store, bool& quotaBlocked) {
    Route r;
    for (std::size_t k = 0; k < g.apis.size(); ++k) {
        const ApiEndpoint& a = g.apis[k];
        if (!skipApiId.empty() && a.id == skipApiId) continue;
        std::string upstream;
        if (!apiServes(a, wanted, upstream)) continue;
        if (apiOverQuota(store, a)) {
            quotaBlocked = true;
            continue;
        }
        r.ok = true;
        r.group = &g;
        r.api = &a;
        r.model = upstream;
        r.exposed = wanted;
        return r;
    }
    return r;
}

void handleModels(SOCKET s, const Config& cfg, const User& user) {
    json::Value data = json::Value::array();
    std::set<std::string> seen;
    for (std::size_t i = 0; i < user.groupIds.size(); ++i) {
        const Group* g = findGroup(cfg, user.groupIds[i]);
        if (!g) continue;
        std::string prefix = g->prefix.empty() ? (g->name + "/") : g->prefix;
        for (std::size_t k = 0; k < g->apis.size(); ++k) {
            const ApiEndpoint& a = g->apis[k];
            for (std::size_t m = 0; m < a.models.size(); ++m) {
                std::string id = prefix + exposedName(a, a.models[m]);
                if (!seen.insert(id).second) continue;
                json::Value item = json::Value::object();
                item.set("id", json::Value::str(id));
                item.set("object", json::Value::str("model"));
                item.set("created", json::Value::integer(0));
                item.set("owned_by", json::Value::str(g->name));
                data.push(item);
            }
        }
    }
    json::Value root = json::Value::object();
    root.set("object", json::Value::str("list"));
    root.set("data", data);
    sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
}

void handleV1(SOCKET s, Store& store, const HttpRequest& req) {
    std::string key;
    std::string auth = req.header("authorization");
    if (util::startsWith(util::toLower(auth), "bearer ")) key = util::trim(auth.substr(7));
    if (key.empty()) key = util::trim(req.header("x-api-key"));

    User user;
    if (key.empty() || !store.findUserByKey(key, user) || !user.enabled) {
        notFound(s);
        return;
    }
    if (!user.unlimited && store.usedTokens(user.id) >= user.quota) {
        jsonError(s, 429, "总额度已用尽，请联系管理员", "insufficient_quota", "insufficient_quota");
        return;
    }
    if (user.dailyQuota > 0 && store.userUsedToday(user.id) >= user.dailyQuota) {
        jsonError(s, 429, "当日额度已用尽，请次日再试", "insufficient_quota", "insufficient_quota");
        return;
    }

    Config cfg = store.snapshot();

    if (util::iequals(req.path, "/v1/models")) {
        handleModels(s, cfg, user);
        return;
    }

    if (req.method != "POST" && req.method != "PUT") {
        jsonError(s, 405, "Method Not Allowed", "invalid_request_error");
        return;
    }

    json::Value body;
    std::string perr;
    if (!json::Value::parse(req.body, body, &perr) || !body.isObject()) {
        jsonError(s, 400, "请求体不是有效的 JSON", "invalid_request_error");
        return;
    }
    std::string requested = body.get("model").asString();
    if (requested.empty()) {
        jsonError(s, 400, "缺少 model 参数", "invalid_request_error");
        return;
    }

    bool quotaBlocked = false;
    std::map<std::string, int> fails = store.apiFailMap();
    Route route = resolveRoute(cfg, user, requested, store, fails, quotaBlocked);
    if (!route.ok || !route.api) {
        if (quotaBlocked) {
            jsonError(s, 429, "所有上游 API 今日额度已用尽", "insufficient_quota", "insufficient_quota");
        } else {
            jsonError(s, 404, "模型不存在或无权访问", "invalid_request_error", "model_not_found");
        }
        return;
    }

    bool wantStream = body.get("stream").asBool(false);
    const int failLimit = cfg.settings.failLimit;

    // 组内提供该模型的上游全部熔断：本轮清零，从第一个重新开始按规则切换
    if (route.trippedFallback) {
        for (std::size_t k = 0; k < route.group->apis.size(); ++k) {
            const ApiEndpoint& a = route.group->apis[k];
            std::string up;
            if (apiServes(a, route.exposed, up)) store.resetApiFails(a.id);
        }
        logLine("组内上游全部熔断，重置异常计数后从 " + route.group->name + "/" +
                route.api->name + " 重新开始");
    }
    // 阈值为 0 时不熔断，但异常要立即顺延一次，因此第一跳先不把响应发给客户端
    bool deferFirst = (failLimit == 0);
    bool retried = false;

    for (;;) {
        std::string forwardBody = req.body;
        if (requested != route.model) {
            body.set("model", json::Value::str(route.model));
            forwardBody = body.dump(0);
        }
        std::string via = route.group->name + "/" + route.api->name;
        std::string statModel = route.group->prefix + route.exposed;

        RelayOutcome out = relayUpstream(s, req.method, req.path, req.query, forwardBody,
                                         *route.api, wantStream, deferFirst && !retried);
        bool failed = (!out.started) || (!out.ok);

        if (failed) {
            bool fault = (!out.started);
            if (out.started) {
                int st = out.status;
                // 上游自身故障才算异常；400/413/415/422 属于请求本身的问题
                fault = (st >= 500) || st == 401 || st == 403 || st == 404 ||
                        st == 408 || st == 409 || st == 429;
            }
            int n = fault ? store.recordApiFailure(route.api->id, statModel) : 0;
            std::string detail = out.started ? ("上游返回 " + std::to_string(out.status))
                                             : ("上游无响应: " + out.error);
            logLine(user.name + "  " + requested + " via " + via + " -> " + detail +
                    (fault ? ("  连续异常 " + std::to_string(n) + "/" + std::to_string(failLimit) +
                              (failLimit > 0 && n >= failLimit ? "（已熔断，切换下一个上游）" : ""))
                           : std::string()));

            if (fault && failLimit == 0 && !retried) {
                bool nextQuotaBlocked = false;
                Route next = nextInGroup(*route.group, route.exposed, route.api->id, store,
                                         nextQuotaBlocked);
                if (next.ok) {
                    retried = true;
                    route = next;
                    logLine("异常重试：改投 " + route.group->name + "/" + route.api->name);
                    continue;
                }
            }

            if (!out.started) {
                jsonError(s, 502, out.error.empty() ? "上游服务不可用" : out.error, "upstream_error");
            } else if (out.deferred) {
                sendResponse(s, out.status > 0 ? out.status : 502,
                             out.contentType.empty() ? "application/json" : out.contentType, out.body);
            }
            return;
        }

        store.recordApiSuccess(route.api->id);
        if (out.deferred) {
            sendResponse(s, out.status, out.contentType.empty() ? "application/json" : out.contentType,
                         out.body);
        }
        long long total = out.promptTokens + out.completionTokens;
        store.recordUsage(user.id, statModel, out.promptTokens, out.completionTokens);
        store.recordApiUsage(route.api->id, total);
        logLine(user.name + "  " + util::formatCount(out.promptTokens) + "+" +
                util::formatCount(out.completionTokens) + " tokens  " + statModel +
                "  via " + via + (retried ? "（异常重试）" : "") +
                (route.trippedFallback ? "（全组熔断，回落）" : ""));
        return;
    }
}

void handleApi(SOCKET s, Store& store, const HttpRequest& req) {
    const std::string& p = req.path;

    // 会话自检：未登录时不暴露自身存在（与其它管理接口同样按不存在处理）
    if (p == "/api/session") {
        if (!adminOk(req)) {
            notFound(s);
            return;
        }
        Config cfg = store.snapshot();
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(true));
        root.set("siteName", json::Value::str(cfg.settings.siteName));
        root.set("version", json::Value::str(RELAY_VERSION));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        return;
    }

    if (p == "/api/auth" && req.method == "GET") {
        Config cfg0 = store.snapshot();
        if (loginBlocked(req.remoteIp, cfg0)) {
            notFound(s);
            return;
        }
        Config cfg = store.snapshot();
        json::Value root = json::Value::object();
        root.set("salt", json::Value::str(cfg.settings.adminSalt));
        root.set("nonce", json::Value::str(issueNonce()));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        return;
    }

    if (p == "/api/login" && req.method == "POST") {
        Config cfg = store.snapshot();
        if (loginBlocked(req.remoteIp, cfg)) {
            logLine("登录被拒绝（限速/锁定） IP=" + req.remoteIp);
            notFound(s);
            return;
        }
        json::Value body;
        std::string err;
        if (!json::Value::parse(req.body, body, &err)) {
            notFound(s);
            return;
        }
        std::string nonce = body.get("nonce").asString();
        std::string proof = util::toLower(body.get("proof").asString());
        if (nonce.empty() || proof.size() != 64 || !consumeNonce(nonce)) {
            logLine("登录失败（请求无效） IP=" + req.remoteIp);
            notFound(s);
            return;
        }
        std::string expected = util::sha256Hex(nonce + cfg.settings.adminHash);
        if (!util::secureEquals(proof, expected)) {
            recordLoginFailure(req.remoteIp, cfg);
            bool locked = false;
            {
                std::lock_guard<std::mutex> lock(g_loginMu);
                locked = g_loginLocked;
            }
            logLine("登录失败：密码错误 IP=" + req.remoteIp);
            if (locked) logLine("登录失败次数过多，登录功能已锁定（可在控制面板解除）");
            notFound(s);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_loginMu);
            g_loginByIp.erase(req.remoteIp);
        }
        std::string token = newSession();
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(true));
        std::vector<std::pair<std::string, std::string>> extra;
        extra.push_back(std::make_pair(std::string("Set-Cookie"),
                                       "relay_session=" + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=604800"));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0), extra);
        logLine("管理员登录成功 IP=" + req.remoteIp);
        return;
    }

    if (p == "/api/logout" && req.method == "POST") {
        std::string sid = sessionOf(req);
        if (!sid.empty()) {
            std::lock_guard<std::mutex> lock(g_sessMu);
            g_sessions.erase(sid);
        }
        std::vector<std::pair<std::string, std::string>> extra;
        extra.push_back(std::make_pair(std::string("Set-Cookie"),
                                       std::string("relay_session=; Path=/; HttpOnly; Max-Age=0")));
        sendResponse(s, 200, "application/json; charset=utf-8", "{\"ok\":true}", extra);
        return;
    }

    if (!adminOk(req)) {
        notFound(s);
        return;
    }

    if (p == "/api/security" && req.method == "GET") {
        Config cfg = store.snapshot();
        json::Value root = json::Value::object();
        json::Value wl = json::Value::array();
        for (std::size_t i = 0; i < cfg.settings.ipWhitelist.size(); ++i) {
            wl.push(json::Value::str(cfg.settings.ipWhitelist[i]));
        }
        root.set("whitelist", wl);
        json::Value bl = json::Value::array();
        for (std::size_t i = 0; i < cfg.settings.ipBlacklist.size(); ++i) {
            bl.push(json::Value::str(cfg.settings.ipBlacklist[i]));
        }
        root.set("blacklist", bl);

        long long now = util::nowSeconds();
        long long penalty = 0;
        bool locked = false;
        json::Value arr = json::Value::array();
        {
            std::lock_guard<std::mutex> lock(g_loginMu);
            penalty = g_loginPenalty;
            locked = g_loginLocked;
            for (std::map<std::string, LoginThrottle>::const_iterator it = g_loginByIp.begin();
                 it != g_loginByIp.end(); ++it) {
                json::Value e = json::Value::object();
                e.set("ip", json::Value::str(it->first));
                e.set("fails", json::Value::integer(it->second.fails));
                long long wait = it->second.nextAllowed - now;
                if (wait < 0) wait = 0;
                e.set("retryIn", json::Value::integer(wait));
                e.set("whitelisted", json::Value::boolean(ipInList(cfg.settings.ipWhitelist, it->first)));
                arr.push(e);
            }
        }
        root.set("locked", json::Value::boolean(locked));
        root.set("penalty", json::Value::integer(penalty));
        root.set("limit", json::Value::integer(3600));
        root.set("entries", arr);
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(1));
        return;
    }

    if (p == "/api/security" && req.method == "POST") {
        json::Value body;
        std::string perr;
        if (!json::Value::parse(req.body, body, &perr) || !body.isObject()) {
            notFound(s);
            return;
        }
        std::string action = body.get("action").asString();
        std::string ip = util::trim(body.get("ip").asString());
        Config cfg = store.snapshot();
        std::vector<std::string> wl = cfg.settings.ipWhitelist;
        std::vector<std::string> bl = cfg.settings.ipBlacklist;

        if (action == "clear") {
            std::lock_guard<std::mutex> lock(g_loginMu);
            if (ip.empty()) {
                g_loginByIp.clear();
            } else {
                g_loginByIp.erase(ip);
            }
            logLine("安全设置：清除登录失败记录 " + (ip.empty() ? std::string("(全部)") : ip));
        } else if (action == "unlock") {
            std::lock_guard<std::mutex> lock(g_loginMu);
            g_loginLocked = false;
            g_loginPenalty = 0;
            logLine("安全设置：解除登录锁定");
        } else if (action == "blacklist") {
            if (ip.empty()) {
                jsonError(s, 400, "缺少 IP", "invalid_request_error");
                return;
            }
            if (!ipInList(bl, ip)) bl.push_back(ip);
            {
                std::lock_guard<std::mutex> lock(g_loginMu);
                g_loginByIp.erase(ip);
            }
            store.setIpLists(wl, bl);
            store.save();
            logLine("安全设置：拉黑 IP " + ip);
        } else if (action == "unblacklist") {
            for (std::size_t i = 0; i < bl.size();) {
                if (bl[i] == ip) bl.erase(bl.begin() + (std::ptrdiff_t)i);
                else ++i;
            }
            store.setIpLists(wl, bl);
            store.save();
            logLine("安全设置：解除拉黑 " + ip);
        } else if (action == "whitelist-add") {
            if (ip.empty()) {
                jsonError(s, 400, "缺少 IP", "invalid_request_error");
                return;
            }
            if (!ipInList(wl, ip)) wl.push_back(ip);
            store.setIpLists(wl, bl);
            store.save();
            logLine("安全设置：加入白名单 " + ip);
        } else if (action == "whitelist-remove") {
            for (std::size_t i = 0; i < wl.size();) {
                if (wl[i] == ip) wl.erase(wl.begin() + (std::ptrdiff_t)i);
                else ++i;
            }
            store.setIpLists(wl, bl);
            store.save();
            logLine("安全设置：移出白名单 " + ip);
        } else {
            notFound(s);
            return;
        }
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(true));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        return;
    }

    if (p == "/api/config" && req.method == "GET") {
        Config cfg = store.snapshot();
        std::map<std::string, int> fails = store.apiFailMap();
        json::Value root = json::Value::object();
        root.set("failLimit", json::Value::integer(cfg.settings.failLimit));
        json::Value st = json::Value::object();
        st.set("port", json::Value::integer(cfg.settings.port));
        st.set("siteName", json::Value::str(cfg.settings.siteName));
        st.set("panelPublic", json::Value::boolean(cfg.settings.panelPublic));
        st.set("failLimit", json::Value::integer(cfg.settings.failLimit));
        st.set("resetOffsetMinutes", json::Value::integer(cfg.settings.resetOffsetMinutes));
        st.set("passwordSet", json::Value::boolean(!cfg.settings.adminHash.empty()));
        root.set("settings", st);
        json::Value groups = json::Value::array();
        for (std::size_t i = 0; i < cfg.groups.size(); ++i) {
            const Group& g = cfg.groups[i];
            json::Value gj = json::Value::object();
            gj.set("id", json::Value::str(g.id));
            gj.set("name", json::Value::str(g.name));
            gj.set("prefix", json::Value::str(g.prefix));
            json::Value apis = json::Value::array();
            std::string activeApi;
            for (std::size_t k = 0; k < g.apis.size(); ++k) {
                const ApiEndpoint& a = g.apis[k];
                json::Value aj = json::Value::object();
                aj.set("id", json::Value::str(a.id));
                aj.set("name", json::Value::str(a.name));
                aj.set("baseUrl", json::Value::str(a.baseUrl));
                aj.set("apiKey", json::Value::str(a.apiKey));
                json::Value ms = json::Value::array();
                for (std::size_t m = 0; m < a.models.size(); ++m) ms.push(json::Value::str(a.models[m]));
                aj.set("models", ms);
                json::Value av = json::Value::array();
                for (std::size_t m = 0; m < a.available.size(); ++m) av.push(json::Value::str(a.available[m]));
                aj.set("available", av);
                json::Value al = json::Value::object();
                for (std::map<std::string, std::string>::const_iterator mi = a.alias.begin(); mi != a.alias.end(); ++mi) {
                    al.set(mi->first, json::Value::str(mi->second));
                }
                aj.set("alias", al);
                aj.set("dailyLimitM", json::Value::number(a.dailyLimitM));
                int n = fails.count(a.id) ? fails[a.id] : 0;
                bool tripped = cfg.settings.failLimit > 0 && n >= cfg.settings.failLimit;
                aj.set("fails", json::Value::integer(n));
                aj.set("tripped", json::Value::boolean(tripped));
                if (activeApi.empty() && !tripped) activeApi = a.id;
                apis.push(aj);
            }
            gj.set("apis", apis);
            // 当前在用的上游：组内第一个未熔断的 API；全熔断时回落第一个
            if (activeApi.empty() && !g.apis.empty()) activeApi = g.apis[0].id;
            gj.set("activeApi", json::Value::str(activeApi));
            groups.push(gj);
        }
        root.set("groups", groups);
        json::Value users = json::Value::array();
        for (std::size_t i = 0; i < cfg.users.size(); ++i) {
            const User& u = cfg.users[i];
            json::Value uj = json::Value::object();
            uj.set("id", json::Value::str(u.id));
            uj.set("name", json::Value::str(u.name));
            uj.set("apiKey", json::Value::str(u.apiKey));
            uj.set("note", json::Value::str(u.note));
            uj.set("unlimited", json::Value::boolean(u.unlimited));
            uj.set("enabled", json::Value::boolean(u.enabled));
            uj.set("quota", json::Value::integer(u.quota));
            uj.set("dailyQuota", json::Value::integer(u.dailyQuota));
            uj.set("created", json::Value::integer(u.created));
            json::Value gs = json::Value::array();
            for (std::size_t k = 0; k < u.groupIds.size(); ++k) gs.push(json::Value::str(u.groupIds[k]));
            uj.set("groupIds", gs);
            users.push(uj);
        }
        root.set("users", users);
        root.set("usage", store.stats(queryInt(req.query, "days", 14),
                                      queryInt(req.query, "offset", 0)));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(2));
        return;
    }

    if (p == "/api/config" && req.method == "POST") {
        json::Value body;
        std::string err;
        if (!json::Value::parse(req.body, body, &err)) {
            jsonError(s, 400, "请求格式错误", "invalid_request_error");
            return;
        }
        int oldPort = store.port();
        std::string aerr;
        if (!store.apply(body, aerr)) {
            jsonError(s, 400, aerr, "invalid_request_error");
            return;
        }
        if (!store.save()) {
            jsonError(s, 500, "配置保存失败，请检查目录写入权限", "server_error");
            return;
        }
        int newPort = store.port();
        bool rebind = newPort != oldPort;
        if (rebind) serverRequestRebind();
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(true));
        root.set("rebind", json::Value::boolean(rebind));
        root.set("port", json::Value::integer(newPort));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        logLine("配置已保存");
        return;
    }

    if (p == "/api/stats" && req.method == "GET") {
        sendResponse(s, 200, "application/json; charset=utf-8",
                     store.stats(queryInt(req.query, "days", 14),
                                 queryInt(req.query, "offset", 0)).dump(2));
        return;
    }

    if (p == "/api/update/check" && req.method == "POST") {
        json::Value root = updateCheck();
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        return;
    }

    if (p == "/api/update/apply" && req.method == "POST") {
        std::string uerr, note;
        if (!updateApply(uerr, note)) {
            jsonError(s, 400, uerr, "invalid_request_error");
            return;
        }
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(true));
        root.set("restarting", json::Value::boolean(true));
        root.set("message", json::Value::str(note));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        logLine(note);
        // 给响应留出时间，再让本进程退出，新进程接管端口
        std::thread([]() {
            Sleep(600);
            serverRequestStop();
        }).detach();
        return;
    }

    if (p == "/api/logs" && req.method == "GET") {
        json::Value arr = json::Value::array();
        std::lock_guard<std::mutex> lock(g_logMu);
        for (std::size_t i = 0; i < g_logs.size(); ++i) arr.push(json::Value::str(g_logs[i]));
        json::Value root = json::Value::object();
        root.set("logs", arr);
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        return;
    }

    if (p == "/api/apifail/reset" && req.method == "POST") {
        json::Value body;
        std::string err;
        std::string apiId;
        if (json::Value::parse(req.body, body, &err) && body.isObject()) {
            apiId = util::trim(body.get("apiId").asString());
        }
        store.resetApiFails(apiId);
        store.save();
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(true));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        logLine(apiId.empty() ? "已重置全部上游异常计数" : ("已重置上游异常计数 " + apiId));
        return;
    }

    if (p == "/api/usage/clear" && req.method == "POST") {
        jsonError(s, 410, "用量统计不可清空", "invalid_request_error");
        return;
    }

    if (p == "/api/models" && req.method == "POST") {
        json::Value body;
        std::string err;
        if (!json::Value::parse(req.body, body, &err)) {
            jsonError(s, 400, "请求格式错误", "invalid_request_error");
            return;
        }
        std::string baseUrl = util::trim(body.get("baseUrl").asString());
        std::string apiKey = util::trim(body.get("apiKey").asString());
        if (baseUrl.empty()) {
            jsonError(s, 400, "请先填写 API 地址", "invalid_request_error");
            return;
        }
        std::vector<std::string> models;
        int status = 0;
        std::string msg;
        bool ok = fetchModels(baseUrl, apiKey, models, status, msg);
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(ok));
        root.set("status", json::Value::integer(status));
        root.set("error", json::Value::str(ok ? std::string() : msg));
        json::Value arr = json::Value::array();
        for (std::size_t i = 0; i < models.size(); ++i) arr.push(json::Value::str(models[i]));
        root.set("models", arr);
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        logLine("拉取模型列表 " + baseUrl + " -> " + (ok ? std::to_string(models.size()) + " 个" : msg));
        return;
    }

    if (p == "/api/test" && req.method == "POST") {
        json::Value body;
        std::string err;
        if (!json::Value::parse(req.body, body, &err)) {
            jsonError(s, 400, "请求格式错误", "invalid_request_error");
            return;
        }
        std::string baseUrl = body.get("baseUrl").asString();
        std::string apiKey = body.get("apiKey").asString();
        int status = 0;
        std::string info;
        bool ok = testUpstream(baseUrl, apiKey, status, info);
        json::Value root = json::Value::object();
        root.set("ok", json::Value::boolean(ok));
        root.set("status", json::Value::integer(status));
        root.set("info", json::Value::str(info));
        sendResponse(s, 200, "application/json; charset=utf-8", root.dump(0));
        return;
    }

    notFound(s);
}

void handleConn(SOCKET s, Store* store, const std::string& ip) {
    struct Guard {
        SOCKET s;
        ~Guard() { closesocket(s); }
    } guard{s};

    DWORD tv = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    // 黑名单：不读取请求、不返回任何内容
    if (store->isBlacklisted(ip)) {
        shutdown(s, SD_BOTH);
        return;
    }

    HttpRequest req;
    std::string err;
    if (!readRequest(s, req, err)) {
        return;
    }
    req.remoteIp = ip;

    Config cfg = store->snapshot();
    bool isV1 = (req.path == "/v1" || util::startsWith(req.path, "/v1/"));
    bool isFavicon = (req.path == "/favicon.ico");
    if (!isV1 && !isFavicon && !panelAllowed(req, cfg)) {
        notFound(s);
        return;
    }

    if (req.path == "/" || req.path == "/index.html") {
        bool authed = adminOk(req);
        sendResponse(s, 200, "text/html; charset=utf-8",
                     authed ? adminHtml() : loginHtml());
        return;
    }
    if (req.path == "/favicon.ico") {
        sendResponse(s, 204, "", "");
        return;
    }
    if (req.path == "/login.js") {
        sendResponse(s, 200, "application/javascript; charset=utf-8", loginJs());
        return;
    }
    if (req.path == "/hash.js") {
        sendResponse(s, 200, "application/javascript; charset=utf-8", hashJs());
        return;
    }
    if (req.path == "/admin.js") {
        if (!adminOk(req)) {
            notFound(s);
            return;
        }
        sendResponse(s, 200, "application/javascript; charset=utf-8", adminJs());
        return;
    }
    if (util::startsWith(req.path, "/api/")) {
        handleApi(s, *store, req);
        return;
    }
    if (isV1) {
        handleV1(s, *store, req);
        return;
    }
    notFound(s);
}

} // namespace

void logLine(const std::string& msg) {
    std::string line = "[" + util::timeString(util::nowSeconds()) + "] " + msg;
    {
        std::lock_guard<std::mutex> lock(g_logMu);
        g_logs.push_back(line);
        while (g_logs.size() > kMaxLogs) g_logs.pop_front();
    }
    std::string out = line + "\n";
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD ft = (h && h != INVALID_HANDLE_VALUE) ? GetFileType(h) : FILE_TYPE_UNKNOWN;
    if (ft == FILE_TYPE_CHAR) {
        DWORD written = 0;
        WriteConsoleA(h, out.data(), (DWORD)out.size(), &written, nullptr);
    } else {
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }
    static const std::string logPath = util::exeDir() + "\\relay.log";
    FILE* fp = nullptr;
    if (fopen_s(&fp, logPath.c_str(), "ab") == 0 && fp) {
        std::fwrite(out.data(), 1, out.size(), fp);
        std::fclose(fp);
    }
}

void serverRequestRebind() { g_rebind.store(true); }

void serverRequestStop() { g_stop.store(true); }

void serverStop() { g_stop.store(true); }

void serverRun(Store& store) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logLine("网络初始化失败");
        return;
    }

    while (!g_stop.load()) {
        int port = store.port();
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls == INVALID_SOCKET) {
            logLine("创建监听套接字失败");
            break;
        }
        BOOL reuse = TRUE;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((u_short)port);

        if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, SOMAXCONN) != 0) {
            logLine("端口 " + std::to_string(port) + " 监听失败，请检查端口是否被占用");
            closesocket(ls);
            if (!g_rebind.load()) {
                Sleep(3000);
                continue;
            }
            g_rebind.store(false);
            continue;
        }

        logLine("服务已启动： http://127.0.0.1:" + std::to_string(port) + "/  (API 入口 /v1)");

        while (!g_stop.load() && !g_rebind.load()) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(ls, &fds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 300000;
            int rv = select(0, &fds, nullptr, nullptr, &tv);
            if (rv <= 0) continue;
            sockaddr_in peer;
            int peerLen = (int)sizeof(peer);
            SOCKET c = accept(ls, (sockaddr*)&peer, &peerLen);
            if (c == INVALID_SOCKET) continue;
            char ipBuf[64] = {0};
            inet_ntop(AF_INET, &peer.sin_addr, ipBuf, sizeof(ipBuf));
            std::string peerIp(ipBuf);
            g_active.fetch_add(1);
            std::thread t([c, &store, peerIp]() {
                handleConn(c, &store, peerIp);
                g_active.fetch_sub(1);
            });
            t.detach();
        }

        closesocket(ls);
        if (g_rebind.load()) {
            g_rebind.store(false);
            logLine("端口已变更，正在重新监听...");
        }
    }

    WSACleanup();
    logLine("服务已停止");
}