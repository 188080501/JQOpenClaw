# JQOpenClaw

基于 Qt/C++ 的 OpenClaw 原生 Node（当前以 Windows Headless 节点为主）。

## 项目目标

- 基于 `Qt 6.5.3 + MSVC` 开发。
- 第一版先跑通完整链路：
  - 能连接 Gateway
  - 能完成设备注册与识别
  - 能响应基础能力调用
- 截图能力通过文件服务返回 URL，不走 base64。

## V0.1 范围

- 平台：Windows
- 运行形态：命令行进程（Headless）
- 基础能力：
  - 桌面截图（JPG）
  - 系统资源信息（当前已实现 CPU 名称、计算机名）
- 连接方式：
  - 指定 Gateway IP/端口/token
  - 协议对齐 OpenClaw Gateway WebSocket Node 模式

## 技术选型

- 框架：Qt 6.5.3（Core / Network / WebSockets）
- 编译器：MSVC
- 构建系统：qmake（`.pro` / `.pri`）
- 序列化：Qt JSON（QJsonDocument / QJsonObject）

## 与 OpenClaw 协议对齐

1. 连接 Gateway WebSocket。
2. 接收 `connect.challenge`（包含 `nonce`）。
3. 构建设备签名载荷。
4. 发送 `connect` 请求（携带 `caps/commands/permissions`）。
5. 通过后进入事件循环，处理 `node.invoke` 请求。

## 整体流程

1. 解析启动参数（host/port/token/tls 等）。
2. 初始化 OpenSSL。
3. 加载或生成本机设备身份（私钥、公钥、deviceId）。
4. 建立 WebSocket 连接并等待 challenge。
5. 使用 challenge nonce 生成签名并发送 `connect`。
6. 连接成功后上报能力清单。
7. 进入主循环，处理 Gateway 下发调用并返回结果。

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

## 目标目录

```text
JQOpenClaw/
  apps/
    JQOpenClawNode/
  modules/
    openclawprotocol/
    capabilities/
      screenshot/
      systemresource/
    crypto/
      deviceidentity/
      signing/
  docs/
```

## 第三方依赖

- OpenSSL 依赖说明：[docs/OpenSSL依赖.md](docs/OpenSSL依赖.md)
- Nginx 依赖说明：[docs/Nginx依赖.md](docs/Nginx依赖.md)
- Nginx 配置模板：[docs/data.conf](docs/data.conf)
