#include "relay.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

json::Value toJson(const ApiEndpoint& a) {
    json::Value o = json::Value::object();
    o.set("id", json::Value::str(a.id));
    o.set("name", json::Value::str(a.name));
    o.set("baseUrl", json::Value::str(a.baseUrl));
    o.set("apiKey", json::Value::str(a.apiKey));
    json::Value ms = json::Value::array();
    for (std::size_t i = 0; i < a.models.size(); ++i) ms.push(json::Value::str(a.models[i]));
    o.set("models", ms);
    json::Value av = json::Value::array();
    for (std::size_t i = 0; i < a.available.size(); ++i) av.push(json::Value::str(a.available[i]));
    o.set("available", av);
    json::Value al = json::Value::object();
    for (std::map<std::string, std::string>::const_iterator it = a.alias.begin(); it != a.alias.end(); ++it) {
        al.set(it->first, json::Value::str(it->second));
    }
    o.set("alias", al);
    o.set("dailyLimitM", json::Value::number(a.dailyLimitM));
    return o;
}

json::Value toJson(const Group& g) {
    json::Value o = json::Value::object();
    o.set("id", json::Value::str(g.id));
    o.set("name", json::Value::str(g.name));
    o.set("prefix", json::Value::str(g.prefix));
    json::Value apis = json::Value::array();
    for (std::size_t i = 0; i < g.apis.size(); ++i) apis.push(toJson(g.apis[i]));
    o.set("apis", apis);
    return o;
}

json::Value toJson(const User& u) {
    json::Value o = json::Value::object();
    o.set("id", json::Value::str(u.id));
    o.set("name", json::Value::str(u.name));
    o.set("apiKey", json::Value::str(u.apiKey));
    o.set("note", json::Value::str(u.note));
    o.set("unlimited", json::Value::boolean(u.unlimited));
    o.set("enabled", json::Value::boolean(u.enabled));
    o.set("quota", json::Value::integer(u.quota));
    json::Value gs = json::Value::array();
    for (std::size_t i = 0; i < u.groupIds.size(); ++i) gs.push(json::Value::str(u.groupIds[i]));
    o.set("groupIds", gs);
    o.set("created", json::Value::integer(u.created));
    return o;
}

ApiEndpoint apiFromJson(const json::Value& o) {
    ApiEndpoint a;
    a.id = o.get("id").asString();
    a.name = o.get("name").asString();
    a.baseUrl = o.get("baseUrl").asString();
    a.apiKey = o.get("apiKey").asString();
    const json::Value& ms = o.get("models");
    for (std::size_t i = 0; i < ms.size(); ++i) a.models.push_back(ms.at(i).asString());
    const json::Value& av = o.get("available");
    for (std::size_t i = 0; i < av.size(); ++i) a.available.push_back(av.at(i).asString());
    const json::Value& al = o.get("alias");
    if (al.isObject()) {
        const std::vector<std::pair<std::string, json::Value>>& items = al.items();
        for (std::size_t i = 0; i < items.size(); ++i) {
            std::string v = items[i].second.asString();
            if (!v.empty()) a.alias[items[i].first] = v;
        }
    }
    a.dailyLimitM = o.get("dailyLimitM").asNumber(0.0);
    if (a.dailyLimitM < 0) a.dailyLimitM = 0;
    return a;
}

Group groupFromJson(const json::Value& o) {
    Group g;
    g.id = o.get("id").asString();
    g.name = o.get("name").asString();
    g.prefix = o.get("prefix").asString();
    const json::Value& apis = o.get("apis");
    for (std::size_t i = 0; i < apis.size(); ++i) g.apis.push_back(apiFromJson(apis.at(i)));
    return g;
}

User userFromJson(const json::Value& o) {
    User u;
    u.id = o.get("id").asString();
    u.name = o.get("name").asString();
    u.apiKey = o.get("apiKey").asString();
    u.note = o.get("note").asString();
    u.unlimited = o.get("unlimited").asBool(false);
    u.enabled = o.get("enabled").asBool(true);
    u.quota = o.get("quota").asInt(0);
    const json::Value& gs = o.get("groupIds");
    for (std::size_t i = 0; i < gs.size(); ++i) u.groupIds.push_back(gs.at(i).asString());
    u.created = o.get("created").asInt(0);
    return u;
}

std::string makeId(const std::string& prefix) { return prefix + util::randomHex(6); }

} // namespace

bool Store::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    path_ = path;
    bool ok = false;
    std::string text = util::readFile(path, &ok);
    if (!ok || text.empty()) {
        cfg_.settings.adminSalt = util::randomHex(16);
        cfg_.settings.adminHash = util::kdfHash(cfg_.settings.adminSalt, "admin");
        dirty_ = true;
        return false;
    }
    json::Value root;
    std::string err;
    if (!json::Value::parse(text, root, &err)) {
        return false;
    }
    const json::Value& st = root.get("settings");
    bool needRehash = false;
    if (st.isObject()) {
        int p = (int)st.get("port").asInt(3344);
        if (p > 0 && p < 65536) cfg_.settings.port = p;
        std::string sn = st.get("siteName").asString();
        if (!sn.empty()) cfg_.settings.siteName = sn;
        cfg_.settings.panelPublic = st.get("panelPublic").asBool(true);
        cfg_.settings.adminSalt = util::trim(st.get("adminSalt").asString());
        cfg_.settings.adminHash = util::trim(st.get("adminHash").asString());
        cfg_.settings.ipWhitelist.clear();
        const json::Value& wl = st.get("ipWhitelist");
        for (std::size_t i = 0; i < wl.size(); ++i) {
            std::string v = util::trim(wl.at(i).asString());
            if (!v.empty()) cfg_.settings.ipWhitelist.push_back(v);
        }
        cfg_.settings.ipBlacklist.clear();
        const json::Value& bl = st.get("ipBlacklist");
        for (std::size_t i = 0; i < bl.size(); ++i) {
            std::string v = util::trim(bl.at(i).asString());
            if (!v.empty()) cfg_.settings.ipBlacklist.push_back(v);
        }
        std::string legacy = st.get("adminPassword").asString();
        if (cfg_.settings.adminSalt.empty() || cfg_.settings.adminHash.empty()) {
            if (cfg_.settings.adminSalt.empty()) cfg_.settings.adminSalt = util::randomHex(16);
            cfg_.settings.adminHash = util::kdfHash(cfg_.settings.adminSalt,
                                                    legacy.empty() ? std::string("admin") : legacy);
            needRehash = true;
        }
    } else {
        cfg_.settings.adminSalt = util::randomHex(16);
        cfg_.settings.adminHash = util::kdfHash(cfg_.settings.adminSalt, "admin");
        needRehash = true;
    }
    if (needRehash) dirty_ = true;
    const json::Value& groups = root.get("groups");
    cfg_.groups.clear();
    for (std::size_t i = 0; i < groups.size(); ++i) cfg_.groups.push_back(groupFromJson(groups.at(i)));
    const json::Value& users = root.get("users");
    cfg_.users.clear();
    for (std::size_t i = 0; i < users.size(); ++i) cfg_.users.push_back(userFromJson(users.at(i)));
    const json::Value& usage = root.get("usage");
    usage_.clear();
    for (std::size_t i = 0; i < usage.size(); ++i) {
        const json::Value& b = usage.at(i);
        UsageBucket ub;
        ub.userId = b.get("userId").asString();
        ub.day = b.get("day").asString();
        ub.model = b.get("model").asString();
        ub.prompt = b.get("prompt").asInt(0);
        ub.completion = b.get("completion").asInt(0);
        ub.requests = b.get("requests").asInt(0);
        if (!ub.userId.empty()) {
            usage_[ub.userId + "|" + ub.day + "|" + ub.model] = ub;
        }
    }
    const json::Value& apiUsage = root.get("apiUsage");
    apiUsage_.clear();
    for (std::size_t i = 0; i < apiUsage.size(); ++i) {
        const json::Value& b = apiUsage.at(i);
        std::string apiId = b.get("apiId").asString();
        std::string day = b.get("day").asString();
        long long tokens = b.get("tokens").asInt(0);
        if (!apiId.empty() && !day.empty()) apiUsage_[apiId + "|" + day] = tokens;
    }
    return true;
}

bool Store::save() {
    static std::mutex saveMu;
    std::lock_guard<std::mutex> saveLock(saveMu);
    Config cfg;
    std::map<std::string, UsageBucket> usage;
    std::map<std::string, long long> apiUsage;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg = cfg_;
        usage = usage_;
        apiUsage = apiUsage_;
        path = path_;
        dirty_ = false;
    }
    json::Value root = json::Value::object();
    json::Value st = json::Value::object();
    st.set("port", json::Value::integer(cfg.settings.port));
    st.set("siteName", json::Value::str(cfg.settings.siteName));
    st.set("panelPublic", json::Value::boolean(cfg.settings.panelPublic));
    st.set("adminSalt", json::Value::str(cfg.settings.adminSalt));
    st.set("adminHash", json::Value::str(cfg.settings.adminHash));
    json::Value wl = json::Value::array();
    for (std::size_t i = 0; i < cfg.settings.ipWhitelist.size(); ++i) {
        wl.push(json::Value::str(cfg.settings.ipWhitelist[i]));
    }
    st.set("ipWhitelist", wl);
    json::Value bl = json::Value::array();
    for (std::size_t i = 0; i < cfg.settings.ipBlacklist.size(); ++i) {
        bl.push(json::Value::str(cfg.settings.ipBlacklist[i]));
    }
    st.set("ipBlacklist", bl);
    root.set("settings", st);
    json::Value groups = json::Value::array();
    for (std::size_t i = 0; i < cfg.groups.size(); ++i) groups.push(toJson(cfg.groups[i]));
    root.set("groups", groups);
    json::Value users = json::Value::array();
    for (std::size_t i = 0; i < cfg.users.size(); ++i) users.push(toJson(cfg.users[i]));
    root.set("users", users);
    json::Value ua = json::Value::array();
    for (std::map<std::string, UsageBucket>::const_iterator it = usage.begin(); it != usage.end(); ++it) {
        json::Value b = json::Value::object();
        b.set("userId", json::Value::str(it->second.userId));
        b.set("day", json::Value::str(it->second.day));
        b.set("model", json::Value::str(it->second.model));
        b.set("prompt", json::Value::integer(it->second.prompt));
        b.set("completion", json::Value::integer(it->second.completion));
        b.set("requests", json::Value::integer(it->second.requests));
        ua.push(b);
    }
    root.set("usage", ua);
    json::Value aa = json::Value::array();
    for (std::map<std::string, long long>::const_iterator it = apiUsage.begin(); it != apiUsage.end(); ++it) {
        std::size_t bar = it->first.find('|');
        if (bar == std::string::npos) continue;
        json::Value b = json::Value::object();
        b.set("apiId", json::Value::str(it->first.substr(0, bar)));
        b.set("day", json::Value::str(it->first.substr(bar + 1)));
        b.set("tokens", json::Value::integer(it->second));
        aa.push(b);
    }
    root.set("apiUsage", aa);
    std::string text = root.dump(2);
    bool ok = util::writeFile(path, text);
    if (ok) {
        std::lock_guard<std::mutex> lock(mu_);
        dirty_ = false;
    }
    return ok;
}

void Store::flush() { save(); }

Config Store::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return cfg_;
}

int Store::port() const {
    std::lock_guard<std::mutex> lock(mu_);
    return cfg_.settings.port;
}

bool Store::isDirty() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dirty_;
}

bool Store::isBlacklisted(const std::string& ip) const {
    if (ip.empty()) return false;
    std::lock_guard<std::mutex> lock(mu_);
    for (std::size_t i = 0; i < cfg_.settings.ipBlacklist.size(); ++i) {
        if (util::ipMatches(cfg_.settings.ipBlacklist[i], ip)) return true;
    }
    return false;
}

void Store::setIpLists(const std::vector<std::string>& whitelist,
                       const std::vector<std::string>& blacklist) {
    std::lock_guard<std::mutex> lock(mu_);
    cfg_.settings.ipWhitelist = whitelist;
    cfg_.settings.ipBlacklist = blacklist;
    dirty_ = true;
}

bool Store::findUserByKey(const std::string& key, User& out) const {
    if (key.empty()) return false;
    std::lock_guard<std::mutex> lock(mu_);
    for (std::size_t i = 0; i < cfg_.users.size(); ++i) {
        if (cfg_.users[i].apiKey == key) {
            out = cfg_.users[i];
            return true;
        }
    }
    return false;
}

long long Store::usedTokens(const std::string& userId) const {
    std::lock_guard<std::mutex> lock(mu_);
    long long total = 0;
    for (std::map<std::string, UsageBucket>::const_iterator it = usage_.begin(); it != usage_.end(); ++it) {
        if (it->second.userId == userId) total += it->second.prompt + it->second.completion;
    }
    return total;
}

void Store::recordUsage(const std::string& userId, const std::string& model,
                        long long prompt, long long completion) {
    if (userId.empty()) return;
    std::string day = util::dateKeyToday();
    std::string m = model.empty() ? "unknown" : model;
    std::lock_guard<std::mutex> lock(mu_);
    UsageBucket& b = usage_[userId + "|" + day + "|" + m];
    b.userId = userId;
    b.day = day;
    b.model = m;
    b.prompt += prompt;
    b.completion += completion;
    b.requests += 1;
    dirty_ = true;
}

void Store::recordApiUsage(const std::string& apiId, long long tokens) {
    if (apiId.empty() || tokens <= 0) return;
    std::string key = apiId + "|" + util::beijingDayKey(util::nowSeconds());
    std::lock_guard<std::mutex> lock(mu_);
    apiUsage_[key] += tokens;
    dirty_ = true;
}

long long Store::apiUsedToday(const std::string& apiId) const {
    std::string key = apiId + "|" + util::beijingDayKey(util::nowSeconds());
    std::lock_guard<std::mutex> lock(mu_);
    std::map<std::string, long long>::const_iterator it = apiUsage_.find(key);
    return it == apiUsage_.end() ? 0 : it->second;
}


