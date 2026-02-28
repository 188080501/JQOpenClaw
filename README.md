# JQOpenClaw

基于 Qt/C++ 的 OpenClaw 原生 Node（首版先做 Windows 无界面版）。

## 项目目标

- 基于 `Qt 6.5.3 + MSVC` 开发。
- 第一版仅做 Headless（不带 GUI），先跑通完整链路：
  - 能连接 Gateway
  - 能完成设备注册/识别
  - 能响应基础能力调用
- 加密相关使用 `OpenSSL`，其他能力优先使用 Qt 原生实现。

## V0.1 范围（首版）

- 平台：Windows
- 运行形态：命令行进程（Headless）
- 基础能力：
  - 桌面截图（输出 JPG，缩放为 480x320，`Qt::KeepAspectRatioByExpanding`）
  - 系统资源信息（规划：CPU、GPU、硬盘、内存；当前已实现：CPU 名称、计算机名）
- 连接方式：
  - 指定 Gateway IP/端口/token 连接
  - 协议对齐 OpenClaw Gateway WebSocket Node 模式

## 技术选型

- 框架：Qt 6.5.3（Core / Network / WebSockets）
- 编译器：MSVC
- 构建系统：qmake（`.pro` / `.pri`）
- 加密库：OpenSSL（预编译库集成）
- 序列化：Qt JSON（QJsonDocument / QJsonObject）

## 与 OpenClaw 协议对齐

首版按 OpenClaw 现有 Node 握手约束实现：

1. 连接 Gateway WebSocket。
2. 接收 `connect.challenge`（包含 `nonce`）。
3. 构建设备签名载荷（推荐 `v3` 语义）。
4. 发送 `connect` 请求，`role: "node"`，并带上 `caps/commands/permissions`。
5. 通过后进入事件循环，处理 `node.invoke` 等请求。

### 设备签名实现约定

- 签名算法：`Ed25519`（OpenSSL EVP）
- 设备公钥：使用 32 字节原始公钥，编码为 `base64url`
- `deviceId`：对原始公钥做 `SHA-256`（hex）
- `signature`：对签名载荷 UTF-8 字节做 detached signature，再 `base64url`
- `nonce`：必须使用服务器下发的 challenge nonce，原样回传

## 整体流程

1. 解析启动参数（host/port/token/tls 等）。
2. 初始化 OpenSSL。
3. 加载或生成本机设备身份（私钥、公钥、deviceId）。
4. 建立 WebSocket 连接并等待 challenge。
5. 使用 challenge nonce 生成签名并发送 `connect`。
6. 连接成功后上报能力清单（caps/commands/permissions）。
7. 进入主循环，处理 Gateway 下发调用并返回结果。
8. 断线重连与状态恢复（后续增强）。

## CLI 参数规划（首版，暂定）

| 参数 | 含义 | 默认值 | 必填 | 状态 |
| --- | --- | --- | --- | --- |
| `--host <ip-or-host>` | Gateway 地址 | 无 | 是 | 暂定 |
| `--port <port>` | Gateway 端口 | 无 | 是 | 暂定 |
| `--token <gateway-token>` | Gateway 认证令牌 | 无 | 是 | 暂定 |
| `--tls` | 启用 TLS 连接 | 关闭 | 否 | 暂定 |
| `--tls-fingerprint <sha256>` | TLS 证书指纹固定 | 无 | 否 | 待确认 |
| `--display-name <name>` | 节点展示名（完整名称） | `JQOpenClawNode-XXXX`（`XXXX` 为随机 4 位数字） | 否 | 暂定 |
| `--node-id <id>` | 节点标识 | 自动生成或配置文件提供 | 否 | 待确认 |
| `--file-server-uri <uri>` | 截图文件服务器基础地址（用于上传与拼接访问地址） | 无 | 否 | 暂定 |
| `--file-server-token <token>` | 截图文件服务器鉴权 token（请求头 `X-Token`） | 无 | 否 | 暂定 |
| `--config <path>` | 配置文件路径 | 无 | 否 | 暂定 |

### 启动命令示例

使用ip

```bash
--host 10.0.1.225 --port 18789 --token <gateway_token> --display-name JQOpenClawNode-9527
```

使用域名+TLS

```bash
--host <your_domain> --port 18789 --tls --token <gateway_token> --display-name JQOpenClawNode-9527
```

## 截图文件服务器说明

- `screenshot.capture` 会上传图片到文件服务器。
- Node 会返回可访问的图片 `url` 给 gateway。
- 上传地址约定：`<fileServerUri>/upload/<filename>.jpg`
- 访问地址约定：`<fileServerUri>/files/<filename>.jpg`
- 鉴权请求头：`X-Token: <fileServerToken>`

配置文件（`--config` 指向的 JSON）包含字段：

```json
{
  "fileServerUri": "https://files.example.com:10038",
  "fileServerToken": "CHANGE_ME_TO_STRONG_TOKEN"
}
```

Nginx 相关文档：

- `docs/Nginx.md`
- `docs/data.conf`

## OpenSSL 预编译库集成

- 本工程使用 OpenSSL（`libcrypto`）提供签名与加密能力。
- 通过环境变量 `OPENSSL_ROOT` 指定本地安装目录（例如：`C:\Develop\OpenSSL`）。
- 链接方式（静态/动态）与库目录由工程自动识别，无需手工指定。
- 当前使用来源：slproweb WinUniversal OpenSSL v3.6.1（2026-02-28）。

## 目标目录（拟）

目录按“主工程 + 应用入口 + 构建片段 + 业务源码 + 能力模块”拆分。`qmake` 入口在根目录，应用入口放在 `apps`，共享协议与能力实现放在 `modules`。

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

说明：

- `apps/JQOpenClawNode`：当前 Node 应用入口与生命周期编排代码。
- `modules`：业务源码与 qmake 工程片段（`capabilities.pri`/`openclawprotocol.pri`/`crypto.pri`，其中 `crypto.pri` 引入 `openssl.pri`）。
- `modules/openclawprotocol`：OpenClaw 协议相关代码（Gateway WebSocket、Node 生命周期与请求分发）。
- `modules/capabilities/*`：截图（当前输出 JPG，480x320，`Qt::KeepAspectRatioByExpanding`）与系统资源信息（CPU、GPU、硬盘、内存；当前已实现 CPU 名称、计算机名）等能力实现。
- `modules/crypto/*`：设备身份、签名、编码相关实现。
- `docs`：协议笔记、联调记录与设计文档。
