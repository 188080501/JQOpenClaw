---
name: jqopenclaw-node-invoker
description: 统一通过 Gateway 的 node.invoke 调用 JQOpenClawNode 能力（file.read、file.write、process.exec、system.info、system.screenshot）。当用户需要远程文件读写、文件移动/删除、远程进程执行、系统信息采集、截图采集、节点命令可用性排查或修复 node.invoke 参数错误时使用。
---

# JQOpenClaw Node Invoker

## 快速流程

1. 确定目标 `nodeId`（用户给定优先）。
2. 调用 `node.describe` 检查节点在线状态，并按“节点识别规则”确认是否为 JQOpenClawNode。
3. 若命令未声明或被网关策略拦截，先输出阻断原因，再给修复建议。
4. 按 [references/command-spec.md](references/command-spec.md) 构造 `node.invoke` 请求。
5. 每次调用使用新的 `idempotencyKey`（UUID）。
6. 输出结果时先给结论，再给关键字段，不直接堆原始 JSON。

## 节点识别规则

- 先读 `node.describe` 返回的 `modelIdentifier`、`commands`、`displayName`、`nodeId`。
- 强匹配（可直接判定为 JQOpenClawNode）：
  - `modelIdentifier` 非空，且满足以下任一条件：
    - 等于 `JQOpenClawNode`。
    - 以 `JQOpenClawNode` 开头（如 `JQOpenClawNode(Qt/C++)`）。
- 弱匹配（仅在 `modelIdentifier` 为空时使用）：
  - `commands` 同时包含：`file.read`、`file.write`、`process.exec`、`system.info`、`system.screenshot`。
  - 且 `displayName` 或 `nodeId` 包含 `JQOpenClaw`。
- 拒绝匹配：
  - `modelIdentifier` 明确存在但不匹配 `JQOpenClawNode*`，即使命令集合相似也不按本技能处理。
- 不确定处理：
  - 若仅满足部分条件，先明确告知“节点类型不确定”，并要求用户指定目标 `nodeId` 或修正节点 `modelIdentifier` 后再执行。

## 命令映射

- 文件读取：`file.read`
- 文件写入/移动/删除：`file.write`
- 远程进程执行：`process.exec`
- 系统基础信息：`system.info`
- 屏幕截图：`system.screenshot`

## 调用规则

- 统一使用 `node.invoke`。
- `params` 必须是对象，字段类型严格匹配。
- 节点侧仅接受 `node.invoke.request.payload.paramsJSON`，且 `paramsJSON` 必须解析为对象。
- `paramsJSON` 缺失或 `null` 时按空对象处理；若存在但不是字符串、为空字符串、或解析后不是对象，按 `INVALID_PARAMS` 处理。
- `file.write` 必须显式传 `allowWrite=true` 才允许执行；未显式授权时应返回阻断提示。
- `timeoutMs` 需按任务复杂度设置：
  - `file.read` / `file.write`：5000-30000
  - `process.exec`：5000-120000
  - `system.info`：30000
  - `system.screenshot`：60000
- `node.invoke.timeoutMs` 可省略；若传入，必须为正整数（毫秒），否则按 `INVALID_PARAMS` 处理。
- `node.invoke.timeoutMs` 是请求总预算；节点会将 `process.exec.params.timeoutMs` 与 `file.read(operation=rg)` 的内部超时裁剪到该预算内（取更小值）。
- `process.exec` 支持 `program + arguments` 模式。
- `file.read` 支持 `operation=read/list/rg`，建议显式传 `operation`；文本检索 `operation=rg` 需提供 `pattern`。
- `file.write` 默认禁用；需显式 `allowWrite=true`。开启后默认 `operation=write`；移动用 `operation=move`（配 `destinationPath`/`toPath`）；删除用 `operation=delete`（走回收站删除）。

## 网关阻断处理

- `command not allowlisted`：
  - 说明这是 Gateway 策略拦截。
  - 提示管理员在 Gateway 配置添加 `gateway.nodes.allowCommands`（如 `file.read`、`file.write`）。
- `command not declared by node` / `node did not declare commands`：
  - 先看 `node.describe.commands`。
  - 要求节点端先声明命令再调用。

## 错误处理规范

- `INVALID_PARAMS`：参数缺失、类型不匹配或超出范围（含 `file.read` / `file.write` / `process.exec` 的参数校验失败）。指出具体字段问题并给出可直接重试的参数。
- `TIMEOUT`：建议增大 `timeoutMs` 或缩小任务范围。
- `FILE_READ_FAILED` / `FILE_WRITE_FAILED`：用于非参数类失败。输出失败原因并给路径、权限、目录存在性、回收站可用性等排查建议。
- `PROCESS_EXEC_FAILED`：用于非参数类失败（程序不存在、权限不足、启动失败等无法产出结构化执行结果）。输出节点返回错误并给程序路径、工作目录、权限、目标进程状态排查建议。
- `SYSTEM_INFO_FAILED`：系统信息采集失败。建议检查节点系统命令可用性与权限。
- `SCREENSHOT_CAPTURE_FAILED` / `SCREENSHOT_UPLOAD_FAILED`：截图采集或上传失败。建议检查显示环境、`file-server-uri`、`file-server-token` 与网络连通性。
- `COMMAND_NOT_SUPPORTED`：改用已声明命令或升级节点版本。

## 输出规范

- 成功时：
  - 先一句话结论。
  - 再列关键字段（例如 `bytesWritten`、`exitCode`、`timedOut`、`url`）。
  - 对 `process.exec`，若 `timedOut=true` 或 `resultClass=timeout`，按“命令超时”给出失败结论与重试建议（即使 `node.invoke` 本身返回成功结构）。
- 失败时：
  - 先给 `error.code`、`error.message`。
  - 再给一条可执行的下一步操作。

## 安全边界

- `file.write` 与 `process.exec` 默认按最小必要原则执行。
- 对可能破坏状态的操作（删除、覆盖、重置、停服务）先征得用户明确确认。
- 不自行提升权限，不绕过网关策略。

## 参考

- [references/command-spec.md](references/command-spec.md)
