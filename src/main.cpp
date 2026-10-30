#include "relay.h"

#include <cstdio>
#include <thread>
#include <iostream>
#include <atomic>

#include <shellapi.h>

namespace {

Store* g_store = nullptr;
std::atomic<bool> g_running(true);

BOOL WINAPI onConsoleEvent(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_SHUTDOWN_EVENT || type == CTRL_LOGOFF_EVENT) {
        if (g_store) g_store->save();
        serverStop();
        return TRUE;
    }
    return FALSE;
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleA("AI Relay");

    std::string dir = util::exeDir();
    std::string cfgPath = dir + "\\relay-data.json";

    Store store;
    g_store = &store;
    bool loaded = store.load(cfgPath);
    if (!loaded) {
        std::printf("未找到配置文件，正在创建默认配置...\n");
        if (!store.save()) {
            std::printf("警告：无法写入 %s，请检查目录权限。\n", cfgPath.c_str());
        }
    } else if (store.isDirty()) {
        store.save();
    }

    Config cfg = store.snapshot();
    std::printf("\n");
    std::printf("  AI Relay %s\n", RELAY_VERSION);
    std::printf("  ----------------------------------------\n");
    std::printf("  数据文件 : %s\n", cfgPath.c_str());
    std::printf("  管理面板 : http://127.0.0.1:%d/\n", cfg.settings.port);
    std::printf("  API 入口 : http://127.0.0.1:%d/v1\n", cfg.settings.port);
    std::printf("  面板访问 : %s\n", cfg.settings.panelPublic ? "允许公网" : "仅本机");
    std::printf("  ----------------------------------------\n");
    std::printf("  首次运行的管理密码为 admin，请登录后立即修改\n");
    std::printf("  命令: open 打开面板 | status 状态 | quit 退出\n\n");

    SetConsoleCtrlHandler(onConsoleEvent, TRUE);

    std::thread server([&store]() { serverRun(store); });
    server.detach();

    std::thread saver([]() {
        while (g_running.load()) {
            for (int i = 0; i < 60 && g_running.load(); ++i) Sleep(500);
            if (!g_running.load()) break;
            if (g_store) g_store->save();
        }
    });
    saver.detach();

    while (g_running.load()) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            Sleep(200);
            continue;
        }
        std::string cmd = util::toLower(util::trim(line));
        if (cmd.empty()) continue;
        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            g_running.store(false);
        } else if (cmd == "open") {
            int port = store.port();
            std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (cmd == "status") {
            Config c = store.snapshot();
            std::printf("  端口 %d | 分组 %d | 用户 %d | 数据文件 %s\n",
                        c.settings.port, (int)c.groups.size(), (int)c.users.size(), cfgPath.c_str());
        } else if (cmd == "help" || cmd == "?") {
            std::printf("  open   在浏览器中打开管理面板\n");
            std::printf("  status 显示当前状态\n");
            std::printf("  quit   停止服务并退出\n");
        } else {
            std::printf("  未知命令: %s\n", cmd.c_str());
        }
    }

    serverStop();
    store.save();
    std::printf("\n已退出。\n");
    Sleep(400);
    return 0;
}
