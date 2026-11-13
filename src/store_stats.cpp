#include "relay.h"

#include <cstdio>
#include <set>
#include <algorithm>

namespace {

long long dayNumberFromKey(const std::string& s) {
    int y = 1970, m = 1, d = 1;
    std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d);
    long long yy = y;
    yy -= (m <= 2) ? 1 : 0;
    long long era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

std::string uniqueId(std::set<std::string>& used, const std::string& prefix) {
    for (int i = 0; i < 100; ++i) {
        std::string id = prefix + util::randomHex(6);
        if (used.find(id) == used.end()) {
            used.insert(id);
            return id;
        }
    }
    return prefix + util::randomHex(12);
}

std::string cleanUrl(const std::string& url) {
    std::string s = util::trim(url);
    while (!s.empty() && s[s.size() - 1] == '/') s.erase(s.size() - 1);
    return s;
}

} // namespace

bool Store::apply(const json::Value& in, std::string& err) {
    if (!in.isObject()) {
        err = "请求内容不是有效的 JSON 对象";
        return false;
    }

    Config next;
    {
        std::lock_guard<std::mutex> lock(mu_);
        next.settings = cfg_.settings;
    }
    const json::Value& st = in.get("settings");
    if (st.isObject()) {
        long long p = st.get("port").asInt(3344);
        if (p < 1 || p > 65535) {
            err = "端口号必须在 1 - 65535 之间";
            return false;
        }
        next.settings.port = (int)p;
        std::string sn = util::trim(st.get("siteName").asString());
        next.settings.siteName = sn.empty() ? std::string("AI Relay") : sn;
        if (st.has("panelPublic")) next.settings.panelPublic = st.get("panelPublic").asBool(true);
        if (st.has("failLimit")) {
            long long fl = st.get("failLimit").asInt(kDefaultFailLimit);
            if (fl < 0) fl = 0;
            if (fl > 99) fl = 99;
            next.settings.failLimit = (int)fl;
        }
        if (st.has("resetOffsetMinutes")) {
            long long off = st.get("resetOffsetMinutes").asInt(0);
            if (off < -720) off = -720;
            if (off > 720) off = 720;
            next.settings.resetOffsetMinutes = (int)off;
        }
        std::string nh = util::trim(st.get("adminPasswordHash").asString());
        if (!nh.empty()) {
            if (nh.size() != 64) {
                err = "密码哈希格式不合法";
                return false;
            }
            next.settings.adminHash = nh;
        }
        if (st.has("ipWhitelist")) {
            next.settings.ipWhitelist.clear();
            std::set<std::string> seen;
            const json::Value& wl = st.get("ipWhitelist");
            for (std::size_t i = 0; i < wl.size(); ++i) {
                std::string v = util::trim(wl.at(i).asString());
                if (!v.empty() && seen.insert(v).second) next.settings.ipWhitelist.push_back(v);
            }
        }
        if (st.has("ipBlacklist")) {
            next.settings.ipBlacklist.clear();
            std::set<std::string> seen;
            const json::Value& bl = st.get("ipBlacklist");
            for (std::size_t i = 0; i < bl.size(); ++i) {
                std::string v = util::trim(bl.at(i).asString());
                if (!v.empty() && seen.insert(v).second) next.settings.ipBlacklist.push_back(v);
            }
        }
    }
    if (next.settings.adminSalt.empty()) {
        next.settings.adminSalt = util::randomHex(16);
        next.settings.adminHash = util::kdfHash(next.settings.adminSalt, "admin");
    }

    std::set<std::string> groupIds;
    const json::Value& groups = in.get("groups");
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const json::Value& gj = groups.at(i);
        Group g;
        g.name = util::trim(gj.get("name").asString());
        if (g.name.empty()) {
            err = "分组名称不能为空";
            return false;
        }
        g.id = util::trim(gj.get("id").asString());
        if (g.id.empty() || groupIds.count(g.id)) g.id = uniqueId(groupIds, "g");
        else groupIds.insert(g.id);
        g.prefix = util::trim(gj.get("prefix").asString());
        if (g.prefix.empty()) g.prefix = g.name + "/";
        if (g.prefix[g.prefix.size() - 1] != '/') g.prefix += "/";

        std::set<std::string> apiIds;
        const json::Value& apis = gj.get("apis");
        for (std::size_t k = 0; k < apis.size(); ++k) {
            const json::Value& aj = apis.at(k);
            ApiEndpoint a;
            a.name = util::trim(aj.get("name").asString());
            a.baseUrl = cleanUrl(aj.get("baseUrl").asString());
            a.apiKey = util::trim(aj.get("apiKey").asString());
            if (a.baseUrl.empty()) {
                err = "分组「" + g.name + "」中存在空的 API 地址";
                return false;
            }
            if (a.name.empty()) a.name = a.baseUrl;
            a.id = util::trim(aj.get("id").asString());
            if (a.id.empty() || apiIds.count(a.id)) a.id = uniqueId(apiIds, "a");
            else apiIds.insert(a.id);
            const json::Value& ms = aj.get("models");
            std::set<std::string> selected;
            for (std::size_t m = 0; m < ms.size(); ++m) {
                std::string name = util::trim(ms.at(m).asString());
                if (!name.empty() && selected.insert(name).second) a.models.push_back(name);
            }
            const json::Value& av = aj.get("available");
            std::set<std::string> seen;
            for (std::size_t m = 0; m < av.size(); ++m) {
                std::string name = util::trim(av.at(m).asString());
                if (!name.empty() && seen.insert(name).second) a.available.push_back(name);
            }
            for (std::size_t m = 0; m < a.models.size(); ++m) {
                if (seen.insert(a.models[m]).second) a.available.push_back(a.models[m]);
            }
            const json::Value& al = aj.get("alias");
            if (al.isObject()) {
                const std::vector<std::pair<std::string, json::Value>>& items = al.items();
                for (std::size_t m = 0; m < items.size(); ++m) {
                    const std::string& up = items[m].first;
                    std::string as = util::trim(items[m].second.asString());
                    if (as.empty() || as == up) continue;
                    bool listed = false;
                    for (std::size_t q = 0; q < a.models.size(); ++q) {
                        if (a.models[q] == up) { listed = true; break; }
                    }
                    if (listed) a.alias[up] = as;
                }
            }
            double lim = aj.get("dailyLimitM").asNumber(0.0);
            if (lim > 0) a.dailyLimitM = lim;
            g.apis.push_back(a);
        }
        next.groups.push_back(g);
    }

    std::set<std::string> keys;
    std::set<std::string> userIds;
    const json::Value& users = in.get("users");
    for (std::size_t i = 0; i < users.size(); ++i) {
        const json::Value& uj = users.at(i);
        User u;
        u.name = util::trim(uj.get("name").asString());
        if (u.name.empty()) {
            err = "用户名不能为空";
            return false;
        }
        u.id = util::trim(uj.get("id").asString());
        if (u.id.empty() || userIds.count(u.id)) u.id = uniqueId(userIds, "u");
        else userIds.insert(u.id);
        u.apiKey = util::trim(uj.get("apiKey").asString());
        if (u.apiKey.empty()) u.apiKey = util::randomKey();
        if (keys.count(u.apiKey)) {
            err = "用户「" + u.name + "」的 APIKey 与其他用户重复";
            return false;
        }
        keys.insert(u.apiKey);
        u.note = uj.get("note").asString();
        u.unlimited = uj.get("unlimited").asBool(false);
        u.enabled = uj.get("enabled").asBool(true);
        u.quota = uj.get("quota").asInt(0);
        if (u.quota < 0) u.quota = 0;
        u.dailyQuota = uj.get("dailyQuota").asInt(0);
        if (u.dailyQuota < 0) u.dailyQuota = 0;
        u.created = uj.get("created").asInt(0);
        if (u.created <= 0) u.created = util::nowSeconds();
        const json::Value& gs = uj.get("groupIds");
        for (std::size_t k = 0; k < gs.size(); ++k) {
            std::string gid = gs.at(k).asString();
            if (groupIds.count(gid)) u.groupIds.push_back(gid);
        }
        next.users.push_back(u);
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg_ = next;
        // 删除用户时同步删除其统计，避免残留数据
        std::set<std::string> keep;
        for (std::size_t i = 0; i < cfg_.users.size(); ++i) keep.insert(cfg_.users[i].id);
        for (std::map<std::string, UsageBucket>::iterator it = usage_.begin(); it != usage_.end();) {
            if (keep.find(it->second.userId) == keep.end()) it = usage_.erase(it);
            else ++it;
        }
        // 删除 API 时同步删除其每日用量
        std::set<std::string> liveApis;
        for (std::size_t i = 0; i < cfg_.groups.size(); ++i) {
            for (std::size_t k = 0; k < cfg_.groups[i].apis.size(); ++k) {
                liveApis.insert(cfg_.groups[i].apis[k].id);
            }
        }
        for (std::map<std::string, long long>::iterator it = apiUsage_.begin(); it != apiUsage_.end();) {
            std::size_t bar = it->first.find('|');
            std::string apiId = bar == std::string::npos ? it->first : it->first.substr(0, bar);
            if (liveApis.find(apiId) == liveApis.end()) it = apiUsage_.erase(it);
            else ++it;
        }
        // 删除 API 时同步删除其异常计数与失败统计
        for (std::map<std::string, int>::iterator it = apiFails_.begin(); it != apiFails_.end();) {
            if (liveApis.find(it->first) == liveApis.end()) it = apiFails_.erase(it);
            else ++it;
        }
        for (std::map<std::string, long long>::iterator it = failStats_.begin(); it != failStats_.end();) {
            std::size_t p1 = it->first.find('|');
            std::size_t p2 = p1 == std::string::npos ? std::string::npos : it->first.find('|', p1 + 1);
            std::string apiId = (p1 == std::string::npos || p2 == std::string::npos)
                                    ? std::string()
                                    : it->first.substr(p1 + 1, p2 - p1 - 1);
            if (apiId.empty() || liveApis.find(apiId) == liveApis.end()) it = failStats_.erase(it);
            else ++it;
        }
        dirty_ = true;
    }
    return true;
}

json::Value Store::stats(int days, int offsetDays) const {
    std::lock_guard<std::mutex> lock(mu_);
    int winDays = days;
    if (winDays < 1) winDays = 14;
    if (winDays > 90) winDays = 90;
    int winOff = offsetDays < 0 ? 0 : offsetDays;
    if (winOff > 3650) winOff = 3650;
    long long now = util::nowSeconds();
    std::string today = dayKeyLocked();
    long long todayNum = dayNumberFromKey(today);
    int dayOffset = cfg_.settings.resetOffsetMinutes;

    struct Agg {
        long long today = 0;
        long long week = 0;
        long long total = 0;
        long long requests = 0;
        std::map<std::string, long long> models;
    };
    std::map<std::string, Agg> perUser;
    std::map<std::string, long long> allModels;
    std::map<std::string, long long> allModelRequests;
    long long allTotal = 0;

    struct RangeAgg {
        long long tokens = 0;
        long long requests = 0;
        std::map<std::string, long long> models;
        std::map<std::string, long long> modelRequests;   // 模型 -> 请求次数
    };
    RangeAgg rDay;
    RangeAgg rWeek;
    RangeAgg rAll;
    std::map<std::string, RangeAgg> uDay;
    std::map<std::string, RangeAgg> uWeek;
    std::map<std::string, RangeAgg> uAll;

    std::set<std::string> liveUsers;
    for (std::size_t i = 0; i < cfg_.users.size(); ++i) liveUsers.insert(cfg_.users[i].id);

    for (std::map<std::string, UsageBucket>::const_iterator it = usage_.begin(); it != usage_.end(); ++it) {
        const UsageBucket& b = it->second;
        if (liveUsers.find(b.userId) == liveUsers.end()) continue;
        long long tokens = b.prompt + b.completion;
        if (tokens <= 0 && b.requests <= 0) continue;
        Agg& a = perUser[b.userId];
        a.total += tokens;
        a.requests += b.requests;
        a.models[b.model] += tokens;
        long long dn = dayNumberFromKey(b.day);
        bool inDay = (b.day == today);
        bool inWeek = (todayNum - dn >= 0 && todayNum - dn < 7);
        if (inDay) a.today += tokens;
        if (inWeek) a.week += tokens;
        allModels[b.model] += tokens;
        allModelRequests[b.model] += b.requests;
        allTotal += tokens;

        rAll.tokens += tokens;
        rAll.requests += b.requests;
        rAll.models[b.model] += tokens;
        rAll.modelRequests[b.model] += b.requests;
        RangeAgg& ua = uAll[b.userId];
        ua.tokens += tokens;
        ua.requests += b.requests;
        ua.models[b.model] += tokens;
        if (inWeek) {
            rWeek.tokens += tokens;
            rWeek.requests += b.requests;
            rWeek.models[b.model] += tokens;
            rWeek.modelRequests[b.model] += b.requests;
            RangeAgg& uw = uWeek[b.userId];
            uw.tokens += tokens;
            uw.requests += b.requests;
            uw.models[b.model] += tokens;
        }
        if (inDay) {
            rDay.tokens += tokens;
            rDay.requests += b.requests;
            rDay.models[b.model] += tokens;
            rDay.modelRequests[b.model] += b.requests;
            RangeAgg& ud = uDay[b.userId];
            ud.tokens += tokens;
            ud.requests += b.requests;
            ud.models[b.model] += tokens;
        }
    }

    json::Value out = json::Value::object();
    out.set("time", json::Value::str(util::timeString(now)));
    out.set("today", json::Value::str(today));

    json::Value users = json::Value::array();
    for (std::size_t i = 0; i < cfg_.users.size(); ++i) {
        const User& u = cfg_.users[i];
        Agg a = perUser[u.id];
        json::Value uj = json::Value::object();
        uj.set("id", json::Value::str(u.id));
        uj.set("name", json::Value::str(u.name));
        uj.set("apiKey", json::Value::str(u.apiKey));
        uj.set("enabled", json::Value::boolean(u.enabled));
        uj.set("unlimited", json::Value::boolean(u.unlimited));
        uj.set("quota", json::Value::integer(u.quota));
        uj.set("dailyQuota", json::Value::integer(u.dailyQuota));
        std::string state;
        if (!u.enabled) state = "disabled";
        else if (!u.unlimited && a.total >= u.quota) state = "total";
        else if (u.dailyQuota > 0 && a.today >= u.dailyQuota) state = "daily";
        else state = "ok";
        uj.set("quotaState", json::Value::str(state));
        uj.set("used", json::Value::integer(a.total));
        uj.set("total", json::Value::integer(a.total));
        uj.set("today", json::Value::integer(a.today));
        uj.set("week", json::Value::integer(a.week));
        uj.set("requests", json::Value::integer(a.requests));
        double share = allTotal > 0 ? (double)a.total * 100.0 / (double)allTotal : 0.0;
        uj.set("share", json::Value::number(share));

        json::Value gnames = json::Value::array();
        for (std::size_t k = 0; k < u.groupIds.size(); ++k) {
            for (std::size_t g = 0; g < cfg_.groups.size(); ++g) {
                if (cfg_.groups[g].id == u.groupIds[k]) {
                    gnames.push(json::Value::str(cfg_.groups[g].name));
                    break;
                }
            }
        }
        uj.set("groupNames", gnames);

        json::Value models = json::Value::array();
        for (std::map<std::string, long long>::const_iterator mit = a.models.begin(); mit != a.models.end(); ++mit) {
            json::Value mj = json::Value::object();
            mj.set("model", json::Value::str(mit->first));
            mj.set("tokens", json::Value::integer(mit->second));
            double ms = a.total > 0 ? (double)mit->second * 100.0 / (double)a.total : 0.0;
            mj.set("share", json::Value::number(ms));
            models.push(mj);
        }
        uj.set("models", models);
        users.push(uj);
    }
    out.set("users", users);

    json::Value models = json::Value::array();
    for (std::map<std::string, long long>::const_iterator mit = allModels.begin(); mit != allModels.end(); ++mit) {
        json::Value mj = json::Value::object();
        mj.set("model", json::Value::str(mit->first));
        mj.set("tokens", json::Value::integer(mit->second));
        std::map<std::string, long long>::const_iterator rit = allModelRequests.find(mit->first);
        mj.set("requests", json::Value::integer(rit == allModelRequests.end() ? 0 : rit->second));
        double ms = allTotal > 0 ? (double)mit->second * 100.0 / (double)allTotal : 0.0;
        mj.set("share", json::Value::number(ms));
        models.push(mj);
    }
    out.set("models", models);
    out.set("allTotal", json::Value::integer(allTotal));

    const RangeAgg* totals[3] = { &rDay, &rWeek, &rAll };
    const char* rangeNames[3] = { "day", "week", "all" };
    const std::map<std::string, RangeAgg>* perRangeUsers[3] = { &uDay, &uWeek, &uAll };

    json::Value byRange = json::Value::object();
    for (int ri = 0; ri < 3; ++ri) {
        const RangeAgg& total = *totals[ri];
        const std::map<std::string, RangeAgg>& umap = *perRangeUsers[ri];

        json::Value ro = json::Value::object();
        ro.set("tokens", json::Value::integer(total.tokens));
        ro.set("requests", json::Value::integer(total.requests));

        std::vector<std::pair<long long, std::size_t>> order;
        order.reserve(cfg_.users.size());
        for (std::size_t i = 0; i < cfg_.users.size(); ++i) {
            std::map<std::string, RangeAgg>::const_iterator it = umap.find(cfg_.users[i].id);
            long long t = it == umap.end() ? 0 : it->second.tokens;
            order.push_back(std::make_pair(t, i));
        }
        std::stable_sort(order.begin(), order.end(),
                         [](const std::pair<long long, std::size_t>& a,
                            const std::pair<long long, std::size_t>& b) { return a.first > b.first; });

        json::Value uarr = json::Value::array();
        for (std::size_t k = 0; k < order.size(); ++k) {
            const User& u = cfg_.users[order[k].second];
            std::map<std::string, RangeAgg>::const_iterator it = umap.find(u.id);
            long long t = it == umap.end() ? 0 : it->second.tokens;
            long long req = it == umap.end() ? 0 : it->second.requests;

            json::Value uj = json::Value::object();
            uj.set("id", json::Value::str(u.id));
            uj.set("name", json::Value::str(u.name));
            uj.set("tokens", json::Value::integer(t));
            uj.set("requests", json::Value::integer(req));
            uj.set("quota", json::Value::integer(u.quota));
            uj.set("unlimited", json::Value::boolean(u.unlimited));
            uj.set("enabled", json::Value::boolean(u.enabled));
            double share = total.tokens > 0 ? (double)t * 100.0 / (double)total.tokens : 0.0;
            uj.set("share", json::Value::number(share));

            json::Value ms = json::Value::array();
            if (it != umap.end()) {
                std::vector<std::pair<long long, std::string>> ml;
                ml.reserve(it->second.models.size());
                for (std::map<std::string, long long>::const_iterator mi = it->second.models.begin();
                     mi != it->second.models.end(); ++mi) {
                    ml.push_back(std::make_pair(mi->second, mi->first));
                }
                std::stable_sort(ml.begin(), ml.end(),
                                 [](const std::pair<long long, std::string>& a,
                                    const std::pair<long long, std::string>& b) { return a.first > b.first; });
                for (std::size_t m = 0; m < ml.size(); ++m) {
                    json::Value mj = json::Value::object();
                    mj.set("model", json::Value::str(ml[m].second));
                    mj.set("tokens", json::Value::integer(ml[m].first));
                    double mshare = t > 0 ? (double)ml[m].first * 100.0 / (double)t : 0.0;
                    mj.set("share", json::Value::number(mshare));
                    ms.push(mj);
                }
            }
            uj.set("models", ms);
            uarr.push(uj);
        }
        ro.set("users", uarr);

        std::vector<std::pair<long long, std::string>> ml;
        ml.reserve(total.models.size());
        for (std::map<std::string, long long>::const_iterator mi = total.models.begin();
             mi != total.models.end(); ++mi) {
            ml.push_back(std::make_pair(mi->second, mi->first));
        }
        std::stable_sort(ml.begin(), ml.end(),
                         [](const std::pair<long long, std::string>& a,
                            const std::pair<long long, std::string>& b) { return a.first > b.first; });
        json::Value marr = json::Value::array();
        for (std::size_t m = 0; m < ml.size(); ++m) {
            json::Value mj = json::Value::object();
            mj.set("model", json::Value::str(ml[m].second));
            mj.set("tokens", json::Value::integer(ml[m].first));
            std::map<std::string, long long>::const_iterator mrit =
                total.modelRequests.find(ml[m].second);
            mj.set("requests", json::Value::integer(mrit == total.modelRequests.end() ? 0 : mrit->second));
            double mshare = total.tokens > 0 ? (double)ml[m].first * 100.0 / (double)total.tokens : 0.0;
            mj.set("share", json::Value::number(mshare));
            marr.push(mj);
        }
        ro.set("models", marr);

        byRange.set(rangeNames[ri], ro);
    }
    out.set("byRange", byRange);

    const int kDays = winDays;
    std::vector<std::string> dayLabels((std::size_t)kDays);
    for (int i = 0; i < kDays; ++i) {
        long long back = (long long)winOff + (long long)i;
        dayLabels[(std::size_t)(kDays - 1 - i)] = util::shiftedDayKey(now - back * 86400, dayOffset);
    }
    std::vector<long long> dayTotal((std::size_t)kDays, 0);
    std::map<std::string, std::vector<long long>> userSeries;
    for (std::map<std::string, UsageBucket>::const_iterator it = usage_.begin(); it != usage_.end(); ++it) {
        const UsageBucket& b = it->second;
        if (liveUsers.find(b.userId) == liveUsers.end()) continue;
        long long diff = todayNum - dayNumberFromKey(b.day);
        if (diff < winOff || diff >= (long long)winOff + kDays) continue;
        std::size_t idx = (std::size_t)(kDays - 1 - (int)(diff - winOff));
        long long tokens = b.prompt + b.completion;
        dayTotal[idx] += tokens;
        std::vector<long long>& s = userSeries[b.userId];
        if (s.empty()) s.assign((std::size_t)kDays, 0);
        s[idx] += tokens;
    }

    json::Value daily = json::Value::object();
    json::Value daysArr = json::Value::array();
    for (int i = 0; i < kDays; ++i) daysArr.push(json::Value::str(dayLabels[(std::size_t)i]));
    daily.set("days", daysArr);
    daily.set("windowDays", json::Value::integer(kDays));
    daily.set("windowOffset", json::Value::integer(winOff));
    json::Value totalArr = json::Value::array();
    for (int i = 0; i < kDays; ++i) totalArr.push(json::Value::integer(dayTotal[(std::size_t)i]));
    daily.set("total", totalArr);

    std::vector<std::pair<long long, std::size_t>> uorder;
    for (std::size_t i = 0; i < cfg_.users.size(); ++i) {
        std::map<std::string, std::vector<long long>>::const_iterator it = userSeries.find(cfg_.users[i].id);
        long long sum = 0;
        if (it != userSeries.end()) {
            for (std::size_t k = 0; k < it->second.size(); ++k) sum += it->second[k];
        }
        if (sum > 0) uorder.push_back(std::make_pair(sum, i));
    }
    std::stable_sort(uorder.begin(), uorder.end(),
                     [](const std::pair<long long, std::size_t>& a,
                        const std::pair<long long, std::size_t>& b) { return a.first > b.first; });
    json::Value uarr = json::Value::array();
    for (std::size_t k = 0; k < uorder.size(); ++k) {
        const User& u = cfg_.users[uorder[k].second];
        std::map<std::string, std::vector<long long>>::const_iterator it = userSeries.find(u.id);
        json::Value uj = json::Value::object();
        uj.set("id", json::Value::str(u.id));
        uj.set("name", json::Value::str(u.name));
        json::Value series = json::Value::array();
        for (int d = 0; d < kDays; ++d) {
            long long v = 0;
            if (it != userSeries.end() && (std::size_t)d < it->second.size()) {
                v = it->second[(std::size_t)d];
            }
            series.push(json::Value::integer(v));
        }
        uj.set("series", series);
        uarr.push(uj);
    }
    daily.set("users", uarr);
    out.set("daily", daily);

    /* ---- 上游 / 模型 失败次数 ---- */
    struct FailAgg {
        long long total = 0;
        std::map<std::string, long long> apis;
        std::map<std::string, long long> models;
    };
    FailAgg fDay, fWeek, fAll;
    for (std::map<std::string, long long>::const_iterator it = failStats_.begin();
         it != failStats_.end(); ++it) {
        std::size_t p1 = it->first.find('|');
        if (p1 == std::string::npos) continue;
        std::size_t p2 = it->first.find('|', p1 + 1);
        if (p2 == std::string::npos) continue;
        std::string day = it->first.substr(0, p1);
        std::string apiId = it->first.substr(p1 + 1, p2 - p1 - 1);
        std::string model = it->first.substr(p2 + 1);
        long long n = it->second;
        if (n <= 0) continue;
        long long diff = todayNum - dayNumberFromKey(day);
        bool inDay = (diff == 0);
        bool inWeek = (diff >= 0 && diff < 7);
        fAll.total += n;
        fAll.apis[apiId] += n;
        fAll.models[model] += n;
        if (inWeek) {
            fWeek.total += n;
            fWeek.apis[apiId] += n;
            fWeek.models[model] += n;
        }
        if (inDay) {
            fDay.total += n;
            fDay.apis[apiId] += n;
            fDay.models[model] += n;
        }
    }

    json::Value failures = json::Value::object();
    failures.set("threshold", json::Value::integer(cfg_.settings.failLimit));
    failures.set("offsetMinutes", json::Value::integer(dayOffset));
    const FailAgg* farr[3] = { &fDay, &fWeek, &fAll };
    for (int ri = 0; ri < 3; ++ri) {
        const FailAgg& fa = *farr[ri];
        json::Value ro = json::Value::object();
        ro.set("total", json::Value::integer(fa.total));

        std::vector<std::pair<long long, std::string>> order;
        for (std::map<std::string, long long>::const_iterator it = fa.apis.begin();
             it != fa.apis.end(); ++it) {
            order.push_back(std::make_pair(it->second, it->first));
        }
        std::stable_sort(order.begin(), order.end(),
                         [](const std::pair<long long, std::string>& a,
                            const std::pair<long long, std::string>& b) { return a.first > b.first; });
        json::Value aarr = json::Value::array();
        for (std::size_t k = 0; k < order.size(); ++k) {
            std::string name, group;
            for (std::size_t gi = 0; gi < cfg_.groups.size() && name.empty(); ++gi) {
                for (std::size_t ai = 0; ai < cfg_.groups[gi].apis.size(); ++ai) {
                    if (cfg_.groups[gi].apis[ai].id == order[k].second) {
                        name = cfg_.groups[gi].apis[ai].name;
                        group = cfg_.groups[gi].name;
                        break;
                    }
                }
            }
            if (name.empty()) continue;   // API 已删除
            json::Value aj = json::Value::object();
            aj.set("id", json::Value::str(order[k].second));
            aj.set("name", json::Value::str(name));
            aj.set("group", json::Value::str(group));
            aj.set("count", json::Value::integer(order[k].first));
            double share = fa.total > 0 ? (double)order[k].first * 100.0 / (double)fa.total : 0.0;
            aj.set("share", json::Value::number(share));
            aarr.push(aj);
        }
        ro.set("apis", aarr);

        std::vector<std::pair<long long, std::string>> morder;
        for (std::map<std::string, long long>::const_iterator it = fa.models.begin();
             it != fa.models.end(); ++it) {
            morder.push_back(std::make_pair(it->second, it->first));
        }
        std::stable_sort(morder.begin(), morder.end(),
                         [](const std::pair<long long, std::string>& a,
                            const std::pair<long long, std::string>& b) { return a.first > b.first; });
        json::Value marr = json::Value::array();
        for (std::size_t k = 0; k < morder.size(); ++k) {
            json::Value mj = json::Value::object();
            mj.set("model", json::Value::str(morder[k].second));
            mj.set("count", json::Value::integer(morder[k].first));
            double share = fa.total > 0 ? (double)morder[k].first * 100.0 / (double)fa.total : 0.0;
            mj.set("share", json::Value::number(share));
            marr.push(mj);
        }
        ro.set("models", marr);
        failures.set(rangeNames[ri], ro);
    }
    out.set("failures", failures);

    return out;
}