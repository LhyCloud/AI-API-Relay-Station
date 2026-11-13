#include "relay.h"

#include <shellapi.h>

#include <cstdio>
#include <cstring>

namespace {

const char* kRepo = "LhyCloud/AI-API-Relay-Station";
const char* kApiUrl = "https://api.github.com/repos/LhyCloud/AI-API-Relay-Station/releases/latest";

struct Url {
    bool secure = true;
    std::string host;
    int port = 443;
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
    u.path = slash == std::string::npos ? std::string("/") : s.substr(slash);
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

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((std::size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((std::size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// 简单的 HTTPS GET：整段读进内存（GitHub API 与 exe 都在几十 MB 以内）
bool httpGet(const std::string& url, const std::string& accept, std::string& body,
             std::string& err, bool toFile = false, const std::string& filePath = std::string(),
             HANDLE* outFile = nullptr) {
    Url u;
    if (!parseUrl(url, u)) {
        err = "更新地址不合法";
        return false;
    }
    std::wstring agent = L"ai-relay/" + toWide(RELAY_VERSION);
    HINTERNET ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        err = "初始化 WinHTTP 失败";
        return false;
    }
    WinHttpSetTimeouts(ses, 15000, 15000, 60000, 300000);
    HINTERNET con = WinHttpConnect(ses, toWide(u.host).c_str(), (INTERNET_PORT)u.port, 0);
    if (!con) {
        WinHttpCloseHandle(ses);
        err = "无法连接更新服务器";
        return false;
    }
    std::wstring wpath = toWide(u.path);
    DWORD flags = u.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        err = "无法创建更新请求";
        return false;
    }
    std::wstring waccept = toWide(accept);
    std::wstring hdrs = L"Accept: " + waccept + L"\r\nUser-Agent: ai-relay/" +
                        toWide(RELAY_VERSION) + L"\r\nAccept-Encoding: identity\r\n";
    BOOL ok = WinHttpSendRequest(req, hdrs.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    DWORD status = 0;
    DWORD len = sizeof(status);
    if (ok) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    }
    if (!ok) {
        err = "更新请求失败（" + std::to_string(GetLastError()) + "）";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return false;
    }
    if (status != 200) {
        err = "更新接口返回 HTTP " + std::to_string(status);
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return false;
    }

    HANDLE f = INVALID_HANDLE_VALUE;
    if (toFile) {
        f = CreateFileW(toWide(filePath).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            err = "无法写入下载文件";
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return false;
        }
    }
    std::size_t total = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            err = "读取更新数据失败";
            break;
        }
        if (avail == 0) break;
        std::vector<char> buf((std::size_t)avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &got)) {
            err = "读取更新数据失败";
            break;
        }
        if (got == 0) break;
        total += got;
        if (total > 128u * 1024u * 1024u) {
            err = "更新包过大";
            break;
        }
        if (toFile) {
            DWORD written = 0;
            if (!WriteFile(f, buf.data(), got, &written, nullptr) || written != got) {
                err = "写入下载文件失败";
                break;
            }
        } else {
            body.append(buf.data(), (std::size_t)got);
        }
    }
    if (f != INVALID_HANDLE_VALUE) {
        if (outFile) *outFile = f;
        else CloseHandle(f);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return err.empty();
}

// 版本号比较：忽略前缀 v 与非数字后缀，按 dot 分段比大小
std::vector<int> versionParts(const std::string& v) {
    std::vector<int> out;
    std::string cur;
    for (std::size_t i = 0; i < v.size(); ++i) {
        char c = v[i];
        if (c == 'v' || c == 'V') continue;
        if (c >= '0' && c <= '9') {
            cur.push_back(c);
        } else if (c == '.') {
            out.push_back(cur.empty() ? 0 : std::atoi(cur.c_str()));
            cur.clear();
        } else {
            break;
        }
    }
    out.push_back(cur.empty() ? 0 : std::atoi(cur.c_str()));
    while (out.size() < 3) out.push_back(0);
    return out;
}

int compareVersion(const std::string& a, const std::string& b) {
    std::vector<int> x = versionParts(a), y = versionParts(b);
    for (std::size_t i = 0; i < 3; ++i) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

struct ReleaseInfo {
    std::string tag;
    std::string version;
    std::string notes;
    std::string publishedAt;
    std::string assetName;
    std::string assetUrl;
    long long assetSize = 0;
    bool hasExeAsset = false;
};

bool fetchLatest(ReleaseInfo& info, std::string& err) {
    std::string body;
    if (!httpGet(kApiUrl, "application/vnd.github+json", body, err)) return false;
    json::Value v;
    if (!json::Value::parse(body, v, &err)) {
        err = "更新信息解析失败";
        return false;
    }
    if (v.has("message") && !v.get("tag_name").isString()) {
        err = "仓库暂无 Release（" + v.get("message").asString() + "）";
        return false;
    }
    info.tag = v.get("tag_name").asString();
    info.version = info.tag;
    info.notes = v.get("body").asString();
    if (info.notes.size() > 2000) info.notes = info.notes.substr(0, 2000) + "…";
    info.publishedAt = v.get("published_at").asString();
    if (info.version.empty()) {
        err = "Release 缺少版本号";
        return false;
    }
    // 找 ai-relay.exe：优先同名资源，其次包含它的压缩包
    const json::Value& assets = v.get("assets");
    std::string zipUrl, zipName;
    long long zipSize = 0;
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const json::Value& a = assets.at(i);
        std::string name = a.get("name").asString();
        std::string low = util::toLower(name);
        if (util::iequals(name, "ai-relay.exe")) {
            info.assetName = name;
            info.assetUrl = a.get("browser_download_url").asString();
            info.assetSize = a.get("size").asInt(0);
            info.hasExeAsset = true;
            return true;
        }
        if (zipUrl.empty() && util::endsWith(low, ".zip") && low.find("ai-relay") != std::string::npos) {
            zipUrl = a.get("browser_download_url").asString();
            zipName = name;
            zipSize = a.get("size").asInt(0);
        }
    }
    if (!zipUrl.empty()) {
        info.assetName = zipName;
        info.assetUrl = zipUrl;
        info.assetSize = zipSize;
        info.hasExeAsset = true;   // 压缩包里应该有，解压后再确认
        return true;
    }
    err = "该 Release 里没有 ai-relay.exe";
    return false;
}

bool fileExists(const std::string& path) {
    DWORD attr = GetFileAttributesW(toWide(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string tempDir() {
    wchar_t buf[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0) return std::string();
    std::string dir = toUtf8(std::wstring(buf, n));
    if (!dir.empty() && dir[dir.size() - 1] == '\\') dir.erase(dir.size() - 1);
    return dir;
}

bool runHidden(const std::wstring& cmd) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof(si));
    std::memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::wstring line = cmd;
    if (!CreateProcessW(nullptr, &line[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// 压缩包解出 ai-relay.exe（用系统自带 PowerShell，不引第三方库）
bool extractExeFromZip(const std::string& zip, const std::string& workDir, std::string& exeOut,
                       std::string& err) {
    std::wstring cmd = L"powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                       L"\"Expand-Archive -LiteralPath '" +
                       toWide(zip) + L"' -DestinationPath '" + toWide(workDir) + L"' -Force\"";
    if (!runHidden(cmd)) {
        err = "解压更新包失败";
        return false;
    }
    std::string direct = workDir + "\\ai-relay.exe";
    if (fileExists(direct)) {
        exeOut = direct;
        return true;
    }
    // 包里可能多一层目录
    WIN32_FIND_DATAW fd;
    std::wstring pattern = toWide(workDir) + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::string sub = workDir + "\\" + toUtf8(fd.cFileName);
            std::string cand = sub + "\\ai-relay.exe";
            if (fileExists(cand)) {
                FindClose(h);
                exeOut = cand;
                return true;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    err = "更新包里找不到 ai-relay.exe";
    return false;
}

bool isMZ(const std::string& path) {
    HANDLE f = CreateFileW(toWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char mz[2] = {0, 0};
    DWORD got = 0;
    bool ok = ReadFile(f, mz, 2, &got, nullptr) && got == 2 && mz[0] == 'M' && mz[1] == 'Z';
    CloseHandle(f);
    return ok;
}

bool copyFileTo(const std::string& from, const std::string& to, std::string& err) {
    if (!CopyFileW(toWide(from).c_str(), toWide(to).c_str(), FALSE)) {
        err = "复制新版本文件失败（错误码 " + std::to_string(GetLastError()) + "）";
        return false;
    }
    return true;
}

std::string exePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (n == 0) return std::string();
    return toUtf8(std::wstring(buf, n));
}

} // namespace

json::Value updateCheck() {
    json::Value out = json::Value::object();
    out.set("current", json::Value::str(RELAY_VERSION));
    ReleaseInfo info;
    std::string err;
    if (!fetchLatest(info, err)) {
        out.set("ok", json::Value::boolean(false));
        out.set("error", json::Value::str(err));
        return out;
    }
    int cmp = compareVersion(info.version, RELAY_VERSION);
    out.set("ok", json::Value::boolean(true));
    out.set("latest", json::Value::str(info.version));
    out.set("tag", json::Value::str(info.tag));
    out.set("notes", json::Value::str(info.notes));
    out.set("publishedAt", json::Value::str(info.publishedAt));
    out.set("hasUpdate", json::Value::boolean(cmp > 0));
    out.set("assetName", json::Value::str(info.assetName));
    out.set("assetSize", json::Value::integer(info.assetSize));
    out.set("canApply", json::Value::boolean(cmp > 0 && info.hasExeAsset));
    out.set("repo", json::Value::str(kRepo));
    return out;
}

bool updateApply(std::string& err, std::string& note) {
    ReleaseInfo info;
    if (!fetchLatest(info, err)) return false;
    if (compareVersion(info.version, RELAY_VERSION) <= 0) {
        err = "当前已是最新版本（" + std::string(RELAY_VERSION) + "）";
        return false;
    }
    if (!info.hasExeAsset || info.assetUrl.empty()) {
        err = "新版本里没有 ai-relay.exe，已放弃更新";
        return false;
    }
    const std::string exe = exePath();
    if (exe.empty()) {
        err = "取不到当前程序路径，已放弃更新";
        return false;
    }
    std::size_t slash = exe.find_last_of("\\/");
    const std::string dir = slash == std::string::npos ? std::string(".") : exe.substr(0, slash);
    const std::string staged = dir + "\\ai-relay.exe.new";
    const std::string backup = dir + "\\ai-relay.exe.old";
    DeleteFileW(toWide(backup).c_str());

    std::string dlErr;
    std::string sink;
    if (!httpGet(info.assetUrl, "application/octet-stream", sink, dlErr, true, staged, nullptr)) {
        err = dlErr.empty() ? "下载更新失败" : dlErr;
        DeleteFileW(toWide(staged).c_str());
        return false;
    }
    // 压缩包就先解出来
    std::string candidate = staged;
    if (!isMZ(candidate)) {
        std::string work = tempDir() + "\\ai-relay-update";
        // 清空旧目录
        std::wstring rm = L"powershell -NoProfile -NonInteractive -Command \"if (Test-Path '" +
                          toWide(work) + L"') { Remove-Item -LiteralPath '" + toWide(work) +
                          L"' -Recurse -Force }\"";
        runHidden(rm);
        std::wstring mk = L"powershell -NoProfile -NonInteractive -Command \"New-Item -ItemType Directory -Force -Path '" +
                          toWide(work) + L"' | Out-Null\"";
        runHidden(mk);
        std::string extracted;
        std::string xerr;
        if (!extractExeFromZip(staged, work, extracted, xerr)) {
            err = "新版本下载包里没有 ai-relay.exe，已放弃更新";
            DeleteFileW(toWide(staged).c_str());
            return false;
        }
        candidate = extracted;
    }
    if (!isMZ(candidate)) {
        err = "下载到的文件不是有效的 ai-relay.exe，已放弃更新";
        DeleteFileW(toWide(staged).c_str());
        return false;
    }
    // 当前 exe 改名为 .old（运行中的 exe 允许改名），再把新文件放到原路径
    if (!MoveFileExW(toWide(exe).c_str(), toWide(backup).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = "无法重命名旧程序（错误码 " + std::to_string(GetLastError()) + "），可能需要管理员权限";
        DeleteFileW(toWide(staged).c_str());
        return false;
    }
    if (!MoveFileExW(toWide(candidate).c_str(), toWide(exe).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // 回滚
        MoveFileExW(toWide(backup).c_str(), toWide(exe).c_str(), MOVEFILE_REPLACE_EXISTING);
        err = "替换 ai-relay.exe 失败（错误码 " + std::to_string(GetLastError()) + "）";
        DeleteFileW(toWide(staged).c_str());
        return false;
    }
    // 先拉起新进程（它会等端口释放），再让旧进程退出，尽量不中断服务
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof(si));
    std::memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    std::wstring cmd = toWide(exe);
    BOOL spawned = CreateProcessW(toWide(exe).c_str(), &cmd[0], nullptr, nullptr, FALSE,
                                  DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                                  toWide(dir).c_str(), &si, &pi);
    if (spawned) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    note = "已下载 v" + info.version + "，新版本正在启动，本进程即将退出";
    if (!spawned) note = "已替换为 v" + info.version + "，但自动启动失败，请手动运行 ai-relay.exe";
    return true;
}
