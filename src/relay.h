#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#define RELAY_VERSION "1.0.0"

/* ------------------------------------------------------------------ */
/* json                                                               */
/* ------------------------------------------------------------------ */
namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value();

    static Value null();
    static Value boolean(bool b);
    static Value number(double d);
    static Value integer(long long v);
    static Value str(const std::string& s);
    static Value array();
    static Value object();

    Type type() const;
    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    bool asBool(bool def = false) const;
    double asNumber(double def = 0.0) const;
    long long asInt(long long def = 0) const;
    std::string asString(const std::string& def = std::string()) const;

    std::size_t size() const;
    Value& at(std::size_t i);
    const Value& at(std::size_t i) const;
    void push(Value v);

    bool has(const std::string& key) const;
    Value& operator[](const std::string& key);
    const Value& get(const std::string& key) const;
    void set(const std::string& key, Value v);
    void remove(const std::string& key);
    std::vector<std::string> keys() const;
    const std::vector<std::pair<std::string, Value>>& items() const;

    std::string dump(int indent = 2) const;
    static bool parse(const std::string& text, Value& out, std::string* err = nullptr);

private:
    void dumpTo(std::string& out, int indent, int level) const;

    Type type_;
    bool b_;
    double n_;
    std::string s_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

} // namespace json

/* ------------------------------------------------------------------ */
/* util                                                               */
/* ------------------------------------------------------------------ */
namespace util {

std::string trim(const std::string& s);
std::string toLower(const std::string& s);
bool iequals(const std::string& a, const std::string& b);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& v, const std::string& sep);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
std::string randomHex(std::size_t bytes);
std::string randomKey();

long long nowSeconds();
std::string dateKey(long long ts);
std::string dateKeyToday();
std::string timeString(long long ts);

std::string exeDir();
std::string readFile(const std::string& path, bool* ok = nullptr);
bool writeFile(const std::string& path, const std::string& data);
std::string urlDecode(const std::string& s);

long long estimateTokens(const std::string& text);
std::string reasonPhrase(int code);
std::string formatCount(long long n);

std::string sha256Hex(const std::string& data);
std::string kdfHash(const std::string& saltHex, const std::string& password);
bool secureEquals(const std::string& a, const std::string& b);
std::string beijingDayKey(long long ts);
std::string shiftedDayKey(long long ts, int offsetMinutes);
bool ipMatches(const std::string& pattern, const std::string& ip);
bool isLoopbackIp(const std::string& ip);

} // namespace util

/* ------------------------------------------------------------------ */
/* config model                                                       */
/* ------------------------------------------------------------------ */

// 上游连续异常熔断阈值的默认值（可在面板设置；0 表示不熔断、失败立即顺延重试一次）
const int kDefaultFailLimit = 3;

struct ApiEndpoint {
    std::string id;
    std::string name;
    std::string baseUrl;
    std::string apiKey;
    std::vector<std::string> models;     // 已勾选（对外可用）的模型（上游名）
    std::vector<std::string> available;  // 上次从上游拉取到的完整模型列表
    std::map<std::string, std::string> alias;  // 上游模型名 -> 对外名称（不含分组前缀）
    double dailyLimitM = 0;              // 每日限额，单位 M tokens，0 表示不限
};

struct Group {
    std::string id;
    std::string name;
    std::string prefix;
    std::vector<ApiEndpoint> apis;
};

struct User {
    std::string id;
    std::string name;
    std::string apiKey;
    std::string note;
    bool unlimited = false;
    bool enabled = true;
    long long quota = 0;
    long long dailyQuota = 0;   // 每日额度（tokens），0 表示不限
    std::vector<std::string> groupIds;
    long long created = 0;
};

struct Settings {
    int port = 3344;
    std::string siteName = "AI Relay";
    bool panelPublic = true;      // 控制面板是否允许非本机访问
    std::string adminSalt;        // 登录口令盐（hex）
    std::string adminHash;        // KDF(盐, 口令)（hex），不保存明文
    std::vector<std::string> ipWhitelist;  // 不受全局锁定，但仍有递增延迟
    std::vector<std::string> ipBlacklist;  // 直接断开，不返回任何内容
    int failLimit = kDefaultFailLimit;     // 连续异常熔断阈值，0 = 不熔断、失败立即重试
    int resetOffsetMinutes = 0;            // 每日额度/异常的重置时间偏移（相对北京时间 0 点）
};

struct Config {
    Settings settings;
    std::vector<Group> groups;
    std::vector<User> users;
};

struct UsageBucket {
    std::string userId;
    std::string day;
    std::string model;
    long long prompt = 0;
    long long completion = 0;
    long long requests = 0;
};

class Store {
public:
    bool load(const std::string& path);
    bool save();
    void flush();

    Config snapshot() const;
    bool apply(const json::Value& in, std::string& err);
    json::Value stats() const;

    void recordUsage(const std::string& userId, const std::string& model,
                     long long prompt, long long completion);
    void recordApiUsage(const std::string& apiId, long long tokens);
    long long apiUsedToday(const std::string& apiId) const;
    long long usedTokens(const std::string& userId) const;
    bool findUserByKey(const std::string& key, User& out) const;

    // 上游异常熔断：连续失败计数（按重置日界，跨天清零）
    std::map<std::string, int> apiFailMap();
    int apiFailCount(const std::string& apiId);
    int recordApiFailure(const std::string& apiId, const std::string& model);
    void recordApiSuccess(const std::string& apiId);
    void resetApiFails(const std::string& apiId = std::string());

    // 重置日界（北京时间 + 偏移）下的日期键与统计
    std::string dayKeyNow() const;
    long long userUsedToday(const std::string& userId) const;

    int port() const;
    bool isDirty() const;
    bool isBlacklisted(const std::string& ip) const;
    void setIpLists(const std::vector<std::string>& whitelist,
                    const std::vector<std::string>& blacklist);

private:
    void rollFailDayLocked();
    std::string dayKeyLocked() const;

    mutable std::mutex mu_;
    Config cfg_;
    std::map<std::string, UsageBucket> usage_;
    std::map<std::string, long long> apiUsage_;  // "apiId|dayKey" -> tokens
    std::map<std::string, long long> failStats_; // "dayKey|apiId|model" -> 失败次数
    std::map<std::string, int> apiFails_;        // apiId -> 当日连续异常次数
    std::string apiFailDay_;                     // 计数所属日界
    std::string path_;
    bool dirty_ = false;
};

/* ------------------------------------------------------------------ */
/* http                                                               */
/* ------------------------------------------------------------------ */
struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;
    std::string body;
    std::string remoteIp;
    std::map<std::string, std::string> headers;

    std::string header(const std::string& name) const;
};

std::string httpReason(int code);
bool sendAll(SOCKET s, const void* data, std::size_t len);
bool sendAll(SOCKET s, const std::string& text);
void sendResponse(SOCKET s, int code, const std::string& contentType, const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& extra =
                      std::vector<std::pair<std::string, std::string>>());
bool readRequest(SOCKET s, HttpRequest& req, std::string& err);

void serverRun(Store& store);
void serverRequestRebind();
void serverStop();

struct RelayOutcome {
    bool started = false;
    bool ok = false;
    bool deferred = false;   // 已拿到上游响应但还没发给客户端（便于失败后改投下一个上游）
    int status = 0;
    long long promptTokens = 0;
    long long completionTokens = 0;
    bool usageFound = false;
    std::string error;
    std::string body;        // deferred 时的响应体
    std::string contentType;
};

RelayOutcome relayUpstream(SOCKET client, const std::string& method, const std::string& targetPath,
                           const std::string& query, const std::string& body,
                           const ApiEndpoint& api, bool streamMode,
                           bool deferSend = false);

bool testUpstream(const std::string& baseUrl, const std::string& apiKey, int& status, std::string& info);

// 拉取上游 /models 列表；失败时 err 给出原因
bool fetchModels(const std::string& baseUrl, const std::string& apiKey,
                 std::vector<std::string>& out, int& status, std::string& err);

const std::string& loginHtml();
const std::string& loginJs();
const std::string& hashJs();
const std::string& adminHtml();
const std::string& adminJs();
void logLine(const std::string& msg);
