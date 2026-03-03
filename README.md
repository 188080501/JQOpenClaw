# JQOpenClaw

JQOpenClaw 是一个基于 Qt/C++ 的 OpenClaw Windows 原生 Node，实现与 OpenClaw Gateway 的 Node WebSocket 协议对接。

适用于希望在 Windows 上以独立可执行程序接入 OpenClaw 的场景，无需在目标机器额外安装 Node.js 与 OpenClaw CLI。

## 项目定位

- 运行平台：Windows，后期扩展Mac和Linux。
- 运行形态：单可执行程序（`JQOpenClawNode.exe`）接入 Gateway。
- 协议兼容：对接 OpenClaw Gateway 的 `node.invoke` 调用链路。
- 能力范围：文件、进程、系统信息、截图与输入控制（详见下文“节点能力与命令”）。
- 开发基线：`Qt 6.5.3 + MSVC`。
- 连接能力：通过指定 Gateway IP/端口/token 连接 OpenClaw Gateway WebSocket Node 模式。
- 注册与识别：支持设备注册、设备身份识别与基础链路建立。

## 节点能力与命令

| 能力分类 | 命令 | 能力说明 |
| --- | --- | --- |
| `file` | `file.read` | 支持 `operation=read/lines/list/rg/stat`：文件读取（含 `offsetBytes` 分块）、按行区间读取（`startLine~endLine`）、目录遍历（含 `recursive/glob`）与元信息查询（owner/权限/时间戳）。 |
| `file` | `file.write` | 支持写入/移动（剪切）/删除（回收站）/目录创建/目录删除，以及 `operation=write/move/delete/mkdir/rmdir`、`createDirs/overwrite` 参数。 |
| `process` | `process.manage` | 进程管理能力，支持 `operation=list/search/kill`：进程列表、按关键字或 PID 搜索、按 PID 终止进程。 |
| `process` | `process.exec` | 基于 QProcess 远程执行进程命令，返回 `exitCode/stdout/stderr` 等结果。 |
| `system` | `system.screenshot` | 采集桌面截图并返回图片信息（JPG）。 |
| `system` | `system.info` | 采集系统基础信息（CPU 名称+核心/线程、计算机名/主机名、系统名称/版本、用户名、内存、GPU、IP、硬盘容量）。 |
| `system` | `system.input` | 输入控制能力，支持动作列表混排：`mouse.move`（绝对/相对）、`mouse.click`（左/右键）、`keyboard.down/up/tap`、`keyboard.text`（文本输入）、`delay`（毫秒延迟）；请求会异步入队并立即返回，若有更新请求到达会取消旧请求剩余动作（latest-wins）。 |

## 调用约定

- 统一通过 Gateway `node.invoke` 调用节点命令。
- 节点侧接收 `node.invoke.request` 时仅解析 `paramsJSON`，且 `paramsJSON` 必须为对象 JSON。
- 参数缺失、类型不匹配、超出范围等参数校验失败统一返回 `INVALID_PARAMS`。
- `process.manage` 当前仅支持 Windows；非 Windows 环境会返回 `PROCESS_MANAGE_FAILED`。
- `process.manage(operation=kill)` 默认拒绝终止关键进程（critical process）；仅当目标 PID 为当前节点进程时允许。

## 运行参数

| 参数 | 说明 | 必填 |
| --- | --- | --- |
| `--host <ip-or-host>` | Gateway 地址 | 是 |
| `--port <port>` | Gateway 端口 | 是 |
| `--token <gateway-token>` | Gateway 认证令牌 | 是 |
| `--tls` | 启用 TLS 连接 | 否 |
| `--tls-fingerprint <sha256>` | TLS 证书指纹固定 | 否 |
| `--display-name <name>` | 节点显示名 | 否 |
| `--node-id <id>` | 节点标识 | 否 |
| `--model-identifier <model>` | 节点实现标识（上报到 `connect.client.modelIdentifier`） | 否 |
| `--file-server-uri <uri>` | 文件服务器基础地址 | 否（截图上传时必需） |
| `--file-server-token <token>` | 文件服务器鉴权 token（`X-Token`） | 否（截图上传时必需） |
| `--config <path>` | 配置文件路径 | 否 |

## 技术选型

- 框架：Qt 6.5.3（Core / QML / Network / WebSockets）
- 编译器：MSVC
- 构建系统：qmake（`.pro` / `.pri`）

## Node 整体运行流程

1. 解析启动参数（host/port/token/tls 等）。
2. 初始化 OpenSSL。
3. 加载或生成本机设备身份（私钥、公钥、deviceId）。
4. 建立 WebSocket 连接并等待 challenge。
5. 使用 challenge nonce 生成签名并发送 `connect`。
6. 连接成功后上报能力清单。
7. 进入主循环，处理 Gateway 下发调用并返回结果。

## 目录结构

```text
JQOpenClaw
├─ apps/JQOpenClawNode/          # Node 应用入口与命令分发
├─ modules/openclawprotocol/     # 网关握手与 caps/commands/permissions 声明
├─ modules/capabilities/file/    # file 能力实现（file.read / file.write：写入/移动/删除/目录增删）
├─ modules/capabilities/process/ # process 能力实现（process.manage / process.exec）
├─ modules/capabilities/system/  # system 能力实现（system.screenshot / system.info / system.input）
├─ modules/crypto/               # 设备身份、签名与加解密相关能力
└─ docs/                         # 项目依赖与部署文档
```

## 第三方依赖

- OpenSSL 依赖说明：[docs/OpenSSL依赖.md](docs/OpenSSL依赖.md)
- Nginx 依赖说明：[docs/Nginx依赖.md](docs/Nginx依赖.md)
- Nginx 配置模板：[docs/data.conf](docs/data.conf)
