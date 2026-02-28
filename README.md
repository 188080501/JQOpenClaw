# JQOpenClaw

基于 Qt/C++ 的 OpenClaw 原生 Node（首版先做 Windows 无界面版）。

## 项目目标

- 基于 `Qt 6.5.3 + MSVC` 开发。
- 第一版仅做 Headless（不带 GUI），先跑通完整链路：
  - 能连接 Gateway
  - 能完成设备注册/识别
  - 能响应基础能力调用
- 加密相关使用 `libsodium`，其他能力优先使用 Qt 原生实现。

## V0.1 范围（首版）

- 平台：Windows
- 运行形态：命令行进程（Headless）
- 基础能力：
  - 桌面截图
  - CPU 使用率查询
  - GPU 使用率查询
  - 网络信息查询
- 连接方式：
  - 指定 Gateway IP/端口/token 连接
  - 协议对齐 OpenClaw Gateway WebSocket Node 模式

## 技术选型

- 框架：Qt 6.5.3（Core / Network / WebSockets）
- 编译器：MSVC
- 构建系统：qmake（`.pro` / `.pri`）
- 加密库：libsodium（预编译库集成）
- 序列化：Qt JSON（QJsonDocument / QJsonObject）

## 与 OpenClaw 协议对齐

首版按 OpenClaw 现有 Node 握手约束实现：

1. 连接 Gateway WebSocket。
2. 接收 `connect.challenge`（包含 `nonce`）。
3. 构建设备签名载荷（推荐 `v3` 语义）。
4. 发送 `connect` 请求，`role: "node"`，并带上 `caps/commands/permissions`。
5. 通过后进入事件循环，处理 `node.invoke` 等请求。

### 设备签名实现约定

- 签名算法：`Ed25519`（libsodium）
- 设备公钥：使用 32 字节原始公钥，编码为 `base64url`
- `deviceId`：对原始公钥做 `SHA-256`（hex）
- `signature`：对签名载荷 UTF-8 字节做 detached signature，再 `base64url`
- `nonce`：必须使用服务器下发的 challenge nonce，原样回传

## 整体流程

1. 解析启动参数（host/port/token/tls 等）。
2. 初始化 libsodium。
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
| `--port <port>` | Gateway 端口 | `18789` | 否 | 暂定 |
| `--token <gateway-token>` | Gateway 认证令牌 | 无 | 视网关配置 | 待确认 |
| `--tls` | 启用 TLS 连接 | 关闭 | 否 | 暂定 |
| `--tls-fingerprint <sha256>` | TLS 证书指纹固定 | 无 | 否 | 待确认 |
| `--display-name <name>` | 节点展示名 | 无 | 否 | 待确认 |
| `--node-id <id>` | 节点标识 | 自动生成或配置文件提供 | 否 | 待确认 |
| `--config <path>` | 配置文件路径 | 无 | 否 | 暂定 |

## libsodium 预编译库集成

建议使用环境变量指定路径（示例）：

`LIBSODIUM_ROOT=<your-path-to-libsodium>`

例如（Windows）：

`LIBSODIUM_ROOT=C:\Develop\libsodium`

### VS 版本与工具集映射

- VS2019：使用 `v142`
- VS2022：使用 `v143`

### x64 静态库路径

- VS2019 Release：`x64\Release\v142\static\libsodium.lib`
- VS2019 Debug：`x64\Debug\v142\static\libsodium.lib`
- VS2022 Release：`x64\Release\v143\static\libsodium.lib`
- VS2022 Debug：`x64\Debug\v143\static\libsodium.lib`

头文件路径：

- `include\`

### 关键注意事项

1. 静态链接时必须定义宏：`SODIUM_STATIC`
2. 工程平台与库平台保持一致（`x64` 对 `x64`）
3. Debug/Release 分别链接对应目录，避免运行时不一致

## 目标目录（拟）

目录按“主工程 + 构建片段 + 业务源码 + 能力模块”拆分。`qmake` 入口在根目录，编译配置放在 `project`，协议与能力实现放在 `src`。

```text
JQOpenClaw/
  JQOpenClaw.pro
  project/
    libsodium.pri
    sources.pri
  src/
    app/
    gateway/
    node/
    capabilities/
      screenshot/
      cpu/
      gpu/
      network/
    crypto/
      device_identity/
      signing/
  third_party/
  docs/
  README.md
```

说明：

- `JQOpenClaw.pro`：qmake 主入口，统一引入各模块配置。
- `project/libsodium.pri`：libsodium 头文件、库路径与宏定义。
- `project/sources.pri`：源码与头文件清单。
- `src/gateway`：Gateway WebSocket 协议与连接状态管理。
- `src/node`：Node 生命周期与请求分发。
- `src/capabilities/*`：截图、CPU、GPU、网络等能力实现。
- `src/crypto/*`：设备身份、签名、编码相关实现。
- `docs`：协议笔记、联调记录与设计文档。
