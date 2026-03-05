# JQOpenClaw

JQOpenClaw 是一个基于 Qt/C++ 开发的 OpenClaw Node，实现与 OpenClaw Gateway 的 Node WebSocket 协议对接。

适用于希望在 Windows 上以独立可执行程序接入 OpenClaw 的场景，无需额外安装 Node.js 与 OpenClaw CLI。

注意，本项目不是代替OpenClaw Gateway，是实现的OpenClaw Node。

Gateway 是中枢/控制平面，Node 是执行端/能力提供者。

一般来说只需要一个 Gateway，然后可以部署多个 Node，然后用 Gateway 统一调度。

## 项目定位

- 运行平台：Windows，后期扩展Mac和Linux。
- 运行形态：免安装，单可执行程序（`JQOpenClawNode.exe`）接入 Gateway。
- 能力范围：文件、进程、系统信息、截图与输入控制（详见下文“节点能力与命令”）。
- 开发套件：`Qt 6.5.3 + MSVC`。
- 协议兼容：对接 OpenClaw Gateway 的 `node.invoke` 调用链路。

## 首次启动

首次启动后，需要手动配置参数，当参数配置完成后，点击```保存```按钮，保存参数后会自动发起WebSocket连接，等待配对。

等待配对时，需要用户在Gateway中批准本节点，才能完成配对。

![参数配置](docs/images/JQOpenClawNode界面.jpg)

## 参数说明

| 参数名 | 配置键 | 是否必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| 网关URL | gatewayUrl | 是 | `ws://127.0.0.1:18789` | Gateway WebSocket 地址，只支持 `ws://` 或 `wss://`。 |
| 网关Token | token | 是 | 空 | Gateway token，留空会导致连接失败。 |
| 显示名称 | displayName | 否 | 自动生成（如 `JQOpenClawNode-1234`） | 节点显示名称。 |
| 实例ID | nodeId | 否 | 首次启动自动生成 UUID | 节点实例 ID。 |
| 文件服务URL | fileServerUrl | 否 | 空 | 文件服务基础 URL（必须是 Nginx 对外入口），用于截图上传。 |
| 文件服务Token | fileServerToken | 条件必填 | 空 | 使用 `system.screenshot` 上传时必填。 |

### URL 配置注意事项

- `网关URL` 必须是合法 URL，且带 `ws`/`wss` 协议和主机名。
- 文件服务必须经过 Nginx 中转，Node 会向 Nginx 执行上传并返回访问链接。
- `文件服务URL` 请填写 Nginx 对外“基础地址”，不要手动加 `/upload` 或 `/files`，也不要填写磁盘目录或对象存储内部地址。
- 程序会在截图上传时自动拼接：
  - 上传地址：`<文件服务URL>/upload/<随机文件名>.jpg`
  - 访问地址：`<文件服务URL>/files/<随机文件名>.jpg`

### 配对成功后在 OpenClaw 中的显示位置

Node 配对成功后，会在 OpenClaw 中显示在如下位置，便于快速确认节点是否已成功接入：

![Node 配对成功后在 OpenClaw 中的显示位置](docs/images/OpenClaw节点位置.jpg)

## Skill 导入

为保证 Gateway 按正确方式调用 Node 能力，请先导入配套 Skill。

导入入口文件（相对仓库根目录）：
`docs/skills/jqopenclaw-node-invoker/SKILL.md`

### 官方 `openclaw.json` 白名单说明（必读）

当前 JQOpenClawNode 提供了较多命令，但这些命令默认不在官方 OpenClaw 的 `openclaw.json` 白名单中。

即使已经导入 Skill，如果网关未放行对应命令，调用仍会被拦截，并出现 `command not allowlisted`。

请在 Gateway 所在机器的 `~/.openclaw/openclaw.json` 中，手动将本节点命令加入 `gateway.nodes.allowCommands`，至少包含你实际要使用的命令，例如：

```json5
{
  gateway: {
    nodes: {
      allowCommands: [
        "file.read",
        "file.write",
        "process.manage",
        "process.exec",
        "process.which",
        "system.info",
        "system.screenshot",
        "system.notify",
        "system.clipboard",
        "system.input",
        "node.selfUpdate"
      ]
    }
  }
}
```

修改配置后，请重启 Gateway（或执行配置重载）再重试调用。

## 节点能力与命令

| 能力分类 | 命令 | 能力说明 |
| --- | --- | --- |
| file | file.read | 支持 `operation=read/lines/list/rg/stat/md5`：文件读取（含 `offsetBytes` 分块）、按行区间读取（`startLine~endLine`）、目录遍历（含 `recursive/glob`）、元信息查询（owner/权限/时间戳）与文件 MD5 计算。 |
| file | file.write | 支持写入/移动（剪切）/删除（回收站）/目录创建/目录删除，以及 `operation=write/move/delete/mkdir/rmdir`、`createDirs/overwrite` 参数。 |
| process | process.manage | 进程管理能力，支持 `operation=list/search/kill`：进程列表、按关键字或 PID 搜索、按 PID 终止进程。 |
| process | process.exec | 基于 QProcess 远程执行进程命令，返回 `exitCode/stdout/stderr` 等结果。 |
| process | process.which | 可执行命令探测能力，支持单个 `program` 或批量 `programs`，返回是否存在与可执行路径。 |
| system | system.screenshot | 采集桌面截图并返回图片信息（JPG）。 |
| system | system.info | 采集系统基础信息（CPU 名称+核心/线程、计算机名/主机名、系统名称/版本、用户名、内存、GPU、IP、硬盘容量）。 |
| system | system.notify | 系统弹窗能力，弹出消息提示框（`message` 必填，`title` 可选）。 |
| system | system.clipboard | 系统剪贴板能力，支持 `operation=read/write`：读取当前剪贴板文本，或写入文本到剪贴板。 |
| system | system.input | 输入控制能力，支持动作列表混排：`mouse.move`（绝对/相对）、`mouse.click`（左/右键）、`mouse.scroll`（滚轮，`delta/deltaY` 与可选 `deltaX`）、`mouse.drag`（按键拖拽至目标坐标）、`keyboard.down/up/tap`、`keyboard.text`（文本输入）、`delay`（毫秒延迟）；请求会异步入队并立即返回，若有更新请求到达会取消旧请求剩余动作（latest-wins）。 |
| node | node.selfUpdate | 节点自更新能力：支持下载新版本程序、可选 MD5 校验、生成临时更新脚本并在回包后延迟退出，完成替换与重启。 |

## Node 整体运行流程

1. 手动启动或者开机自启动
2. 加载并标准化本地配置（`config.json`）。
3. 初始化 OpenSSL，加载或生成本机设备身份（私钥、公钥、deviceId）。
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
├─ modules/capabilities/system/  # system 能力实现（system.screenshot / system.info / system.notify / system.clipboard / system.input）
├─ modules/crypto/               # 设备身份、签名与加解密相关能力
└─ docs/                         # 项目依赖与部署文档
```

## 第三方依赖

- OpenSSL 依赖说明：[docs/OpenSSL依赖.md](docs/OpenSSL依赖.md)
- Nginx 依赖说明：[docs/Nginx依赖.md](docs/Nginx依赖.md)
- Nginx 配置模板：[docs/data.conf](docs/data.conf)
