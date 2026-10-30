# AI Relay

一个用 C++ 编写的 OpenAI 兼容 API 中转站。单文件可执行程序，控制台运行，自带 Web 管理面板。
把上游 OpenAI 兼容服务（可多组、每组多个 Key）汇聚成一个统一入口，按用户发放 APIKey、额度与可用分组，
并记录每个用户的日 / 周 / 总用量、模型占比和占全体比例。

- 服务端：Winsock2 手写 HTTP/1.1 服务
- 上游客户端：WinHTTP（支持 https、流式 SSE 转发）
- 依赖：仅 Windows 系统库（ws2_32、winhttp、shell32），无第三方库
- 构建：MSVC（VS 2019 及以上），Release 静态链接，产物为单个 exe

## 目录结构

```
ai-relay/
  build.bat            编译 Release（输出 dist\ai-relay.exe）
  allow-lan.bat        放行防火墙入站端口（局域网访问用，需管理员）
  src/
    relay.h            公共声明：JSON、工具、数据模型、HTTP 接口
    json.cpp           轻量 JSON 解析 / 序列化
    util.cpp           字符串、时间、文件、随机、SHA-256
    store.cpp          配置与用量的加载 / 保存
    store_stats.cpp    配置校验写入 + 统计聚合
    http.cpp           HTTP 请求解析与响应输出
    server.cpp         监听、路由、管理 API、/v1 鉴权
    upstream.cpp       WinHTTP 转发（含 SSE 流式）
    login.html         登录页（不含站点名/管理接口清单）
    login.js           登录逻辑（只访问 /api/auth 与 /api/login）
    hash.js            SHA-256 / 口令派生（登录页与控制台共用）
    admin.html         控制台外壳（登录后才下发）
    admin.js           控制台逻辑（登录后才下发）
    web.cpp            由上述资源生成的嵌入文件（勿手改）
    main.cpp           控制台程序入口
  tools/gen_web.py     生成 src/web.cpp
  test/smoke_test.py   端到端测试（需 Python 3）
  dist/                编译产物
```

修改了 `src/login.*`、`src/admin.*` 或 `src/hash.js` 之后执行：

```
python tools/gen_web.py
build.bat
```

## 编译

需要安装 Visual Studio 2019/2022（含 C++ 生成工具）。默认路径为 VS 2019 Community，
如安装位置不同，修改 `build.bat` 里的 `VS` 变量。

```
build.bat
```

产物：`build\ai-relay.exe`，编译成功后会自动复制到 `dist\ai-relay.exe`。
如果程序正在运行导致复制失败，先退出程序再把 `build\ai-relay.exe` 覆盖过去即可。
使用 `/MT` 静态链接，运行不依赖 VC 运行库。

## 运行

双击 `dist\ai-relay.exe`，或在控制台执行。首次运行会在同目录生成 `relay-data.json`。

```
AI Relay 1.0.0
  数据文件 : ...\relay-data.json
  管理密码 : admin
  监听端口 : 3344
  管理面板 : http://127.0.0.1:3344/
  API 入口 : http://127.0.0.1:3344/v1
```

控制台命令：

| 命令 | 说明 |
| --- | --- |
| `open` | 用默认浏览器打开管理面板 |
| `status` | 显示端口、分组数、用户数 |
| `quit` | 保存数据并退出 |

数据保存在 exe 同目录的 `relay-data.json`，日志追加写入 `relay.log`（控制台也会输出）。
备份 `relay-data.json` 即可迁移全部配置与用量。

升级时先退出程序，只替换 `ai-relay.exe` 即可，`relay-data.json` 原样保留，
旧配置（分组、用户、历史用量）会自动读取，无需重新配置。

## 初始配置

1. 浏览器打开 `http://127.0.0.1:3344/`，默认密码 `admin`。
2. 进入「设置」修改管理密码和监听端口（保存后立即在新端口重新监听）；
   「访问范围」可关闭公网访问控制面板（关闭后外网打开 / 与 /api 一律 404，/v1 不受影响）。
3. 进入「API 分组」新建分组，填写：
   - 分组名称，例如 `DeepSeek`
   - 模型前缀，例如 `ds/`（留空则用「分组名/」）
   - 一个或多个上游 API：名称、基础地址（如 `https://api.deepseek.com/v1`）、密钥
   - 可选「每日限额（M tokens）」，填 0 为不限；北京时间 0 点重置，超限自动切到组内下一个 API
   - 点该 API 右上角的「获取模型列表」，程序会用该地址和密钥请求上游 `/models`，
     返回的模型以复选框列出，勾选需要的即可（支持搜索筛选）；上游隐藏的模型可在下方手动添加
   - 每个已选模型右侧可填写「对外名称」，用于同一上游模型对外改名为其他名字
4. 进入「用户」新建用户：用户名、APIKey（可自动生成）、额度（tokens，或勾选无限）、可调用的分组。

> 只有勾选的模型才会出现在 `/v1/models` 中，也只有它们能被调用。若填写了对外名称，
> 列表与调用都使用该名称，转发给上游时自动换回真实模型名。拉取到的完整列表会一并保存在
> `relay-data.json` 的 `available` 字段里，方便下次打开时继续勾选。

## 调用方式

对外模型名 = 分组前缀 + 上游模型名，例如分组前缀 `ds/`、上游模型 `deepseek-chat`，
则客户端使用 `ds/deepseek-chat`。中转时会自动去掉前缀再转发给上游。
若给该模型设置了对外名称（如 `chat`），则对外名称为 `ds/chat`，转发时仍还原为 `deepseek-chat`。

```bash
# 模型列表（需有效 Key）
curl http://127.0.0.1:3344/v1/models \
  -H "Authorization: Bearer sk-用户Key"

# 对话
curl http://127.0.0.1:3344/v1/chat/completions \
  -H "Authorization: Bearer sk-用户Key" \
  -H "Content-Type: application/json" \
  -d '{"model":"ds/deepseek-chat","messages":[{"role":"user","content":"你好"}]}'
```

Python SDK 只需改 `base_url` 与 `api_key`：

```python
from openai import OpenAI
client = OpenAI(base_url="http://127.0.0.1:3344/v1", api_key="sk-用户Key")
print(client.chat.completions.create(
    model="ds/deepseek-chat",
    messages=[{"role": "user", "content": "你好"}]))
```

### 鉴权与 404 规则

`/v1` 下的任何路径（包括 `/v1/models`）都必须携带有效用户 APIKey：

- `Authorization: Bearer <key>`
- 或 `x-api-key: <key>`

Key 缺失、错误或用户被停用时，返回与「路径不存在」完全一致的 `404 Not Found`（纯文本），
不暴露该路径是否存在。用户被停用后其 Key 立即失效。

额度用尽返回 `429` 与 OpenAI 风格的错误体；模型不存在或无权访问返回 `404` JSON 错误。

管理接口同样按此规则：未登录访问 `/api/*`、登录口令错误、登录被限速/锁定，一律返回 404 纯文本，
避免被探测出接口是否存在。

### 登录安全

- 浏览器先用 `GET /api/auth` 取本次的 `salt` 与一次性 `nonce`；
  口令在本地做加盐 KDF（2001 轮 SHA-256），再以 `SHA256(nonce + hash)` 作为凭据提交，
  明文口令既不上网也不落盘。服务端只保存 `adminSalt` 与 `adminHash`。
- `nonce` 2 分钟失效且只能用一次，抓包得到的凭据无法重放。
- 连续失败按 1s、2s、4s、8s、16s 递增延迟；累计延迟超过 1 小时则全局锁定登录。
  每次失败都会记录 IP 到 `relay.log`，例如 `登录失败：密码错误 IP=203.0.113.5`。
- **本机 127.0.0.1 完全豁免**：不计数、不延迟、不受全局锁定影响，避免把自己锁在门外。
- **IP 白名单**：不受 1 小时全局锁定限制，但仍有相同的 1/2/4/8/16 秒递增延迟。
- **IP 黑名单**：命中后连接直接断开，任何路径（含网页、静态资源、`/v1`）都不返回内容。
- 锁定不再只能靠重启：控制面板「安全」页可查看失败名单（IP 与密码错误次数）、
  解除锁定、清除单个/全部记录、拉黑或解除拉黑、维护白名单。黑名单与白名单写入配置，重启后保留。
- 页面分离：未登录只下发 `login.html` + `login.js` + `hash.js`，登录页标题固定为 `Console`，
  不包含站点名称，也不包含任何管理接口（只有 `/api/auth`、`/api/login`）。
  控制台外壳 `admin.html` 与 `admin.js` 仅在携带有效会话 Cookie 时返回，否则按不存在路径处理（404），
  因此未登录无法从静态资源里读出 `/api/config`、`/api/logs` 等控制接口。

## 统计口径

- 用量单位是 tokens，取上游响应的 `usage`（`prompt_tokens` / `completion_tokens`，
  兼容 `input_tokens` / `output_tokens`）。
- 流式请求若上游未返回 usage（例如未携带 `stream_options.include_usage`），按字符数估算
  （约 4 字节 / token），面板中同样计入。
- 「今日」按本机日期；「本周」为最近 7 天滚动窗口；「总使用量」为全部历史。
- 概览与统计页的表格旁有「今日 / 本周 / 总计」切换，只显示所选范围的 token 值与占比；
  接口一次返回三档数据（`usage.byRange`），切换时不重新请求。
- 统计页顶部有折线图：全体每日用量、每人每日用量（近 14 天），后者点击图例可隐藏/显示某个用户。
- 面板中的 token 数以 K / M / B / T 缩写显示（如 `12.3K`、`1.23M`），鼠标悬停可见精确数值。
- 统计里的模型名带分组前缀（如 `ds/deepseek-chat`），因此不同分组下的同名模型分别统计。
- 模型占比 = 该用户某模型 tokens / 该用户该范围 tokens；占全体比 = 该用户 tokens / 该范围全体 tokens。
- 额度按累计 tokens 校验，用尽后该用户所有请求返回 429，可随时在面板调整额度或改为无限。
- 用量统计不可清空；删除用户时会同时删除其全部历史用量，避免残留。

## 实现说明

- 每个连接一个线程，请求处理完后关闭连接（`Connection: close`），对 SDK 透明。
- 上游通过 WinHTTP 调用，支持 https 与系统代理；流式响应以 chunked 编码实时转发。
- 模型勾选：管理接口 `POST /api/models` 用指定地址与 Key 请求上游 `/models` 并返回模型名，
  前端勾选结果写入分组的 `models`，拉取到的全集写入 `available`；对外名称写入 `alias`。
- 每日限额按 API 记录当日用量（`apiUsage`，北京时间日期），超限时按组内顺序尝试下一个
  能提供同名（对外名称一致）模型的 API，全部用尽返回 429。
- 配置与用量在保存时原子写盘（临时文件 + 替换），后台每 30 秒兜底保存一次。
- 管理面板使用 Cookie 会话（内存中保存，7 天有效），重启程序后需重新登录。
- 「允许公网访问控制面板」关闭时，仅 127.0.0.1 / ::1 能访问 `/` 与 `/api/*`，其余来源按不存在处理。

## 常见问题

**本机能打开，同一网段的其他设备打不开**

程序监听的是 `0.0.0.0`（所有网卡），通常是被 Windows 防火墙拦了入站连接。
首次运行时系统会弹出"允许访问"提示，若当时点了取消，会生成一条针对该 exe 的**阻止**规则。

处理方式（任选其一）：

1. 右键以管理员身份运行 `allow-lan.bat`（会自动请求 UAC），它会删除名为 `ai-relay` 的旧规则，
   并放行入站 TCP 3344（所有网络配置文件）；
2. Windows 安全中心 → 防火墙和网络保护 → 允许应用通过防火墙 → 更改为允许 →
   找到 `ai-relay.exe`，勾选"专用"和"公用"；
3. 管理员 PowerShell：

```powershell
Remove-NetFirewallRule -DisplayName "ai-relay"
New-NetFirewallRule -DisplayName "AI Relay 3344" -Direction Inbound -Action Allow `
  -Protocol TCP -LocalPort 3344 -Profile Any
```

验证：在另一台设备上执行 `Test-NetConnection 10.10.18.103 -Port 3344`（或直接浏览器打开）。
若改过监听端口，规则里的端口要同步修改。

如果连接能建立但页面返回 404，检查设置里的「允许公网访问控制面板」是否被关闭；
关闭后只有本机可访问面板，`/v1` 仍然正常。

## 已知限制

- 仅支持 Windows。
- 只代理 `/v1` 下的 JSON 接口；文件上传等 multipart 接口未做特殊处理。
- 不支持 WebSocket / Realtime API。
- 上游 API 密钥仍以明文保存在 `relay-data.json`（调用上游必须用到），请自行控制该文件权限；
  管理口令本身只保存加盐哈希。
