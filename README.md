# JQOpenClaw

基于 Qt/C++ 的 OpenClaw 原生 Node（当前以 Windows Headless 节点为主）。

## 功能说明

- 运行平台：Windows；运行形态：命令行进程（Headless）。
- 开发基线：`Qt 6.5.3 + MSVC`。
- 连接能力：通过指定 Gateway IP/端口/token 连接 OpenClaw Gateway WebSocket Node 模式。
- 注册与识别：支持设备注册、设备身份识别与基础链路建立。

## 节点能力与命令

| 能力分类 | 命令 | 能力说明 |
| --- | --- | --- |
| `system` | `system.screenshot` | 采集桌面截图并返回图片信息（JPG）。 |
| `system` | `system.info` | 采集系统基础信息（如 CPU 名称、计算机名称）。 |

## 运行参数

| 参数 | 说明 | 必填 |
| --- | --- | --- |
| `--host <ip-or-host>` | Gateway 地址 | 是 |
| `--port <port>` | Gateway 端口 | 是 |
| `--token <gateway-token>` | Gateway 认证令牌 | 是 |
| `--tls` | 启用 TLS 连接 | 否 |
| `--tls-fingerprint <sha256>` | TLS 证书指纹固定 | 否 |
| `--display-name <name>` | 节点展示名 | 否 |
| `--node-id <id>` | 节点标识 | 否 |
| `--file-server-uri <uri>` | 文件服务器基础地址 | 否（截图上传时必需） |
| `--file-server-token <token>` | 文件服务器鉴权 token（`X-Token`） | 否（截图上传时必需） |
| `--config <path>` | 配置文件路径 | 否 |

## 技术选型

- 框架：Qt 6.5.3（Core / Network / WebSockets）
- 编译器：MSVC
- 构建系统：qmake（`.pro` / `.pri`）
- 序列化：Qt JSON（QJsonDocument / QJsonObject）

## Node整体运行流程

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
├─ modules/capabilities/system/  # system 能力实现（system.screenshot / system.info）
├─ modules/crypto/               # 设备身份、签名与加解密相关能力
└─ docs/                         # 项目依赖与部署文档
```

## 第三方依赖

- OpenSSL 依赖说明：[docs/OpenSSL依赖.md](docs/OpenSSL依赖.md)
- Nginx 依赖说明：[docs/Nginx依赖.md](docs/Nginx依赖.md)
- Nginx 配置模板：[docs/data.conf](docs/data.conf)
