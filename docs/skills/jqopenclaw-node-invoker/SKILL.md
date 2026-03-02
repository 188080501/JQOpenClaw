---
name: jqopenclaw-node-invoker
description: 统一通过 Gateway 的 node.invoke 调用 JQOpenClawNode 能力（file.read、file.write、process.exec、system.info、system.screenshot）。当用户需要远程文件读写、文件移动/删除、远程进程执行、系统信息采集、截图采集、节点命令可用性排查或修复 node.invoke 参数错误时使用。
---

# JQOpenClaw Node Invoker

## 快速流程

1. 确定目标 `nodeId`（用户给定优先）。
2. 调用 `node.describe` 检查节点在线状态和 `commands` 声明。
3. 若命令未声明或被网关策略拦截，先输出阻断原因，再给修复建议。
4. 按 [references/command-spec.md](references/command-spec.md) 构造 `node.invoke` 请求。
5. 每次调用使用新的 `idempotencyKey`（UUID）。
6. 输出结果时先给结论，再给关键字段，不直接堆原始 JSON。

## 命令映射

- 文件读取：`file.read`
- 文件写入/移动/删除：`file.write`
- 远程进程执行：`process.exec`
- 系统基础信息：`system.info`
- 屏幕截图：`system.screenshot`

## 调用规则

- 统一使用 `node.invoke`。
- `params` 必须是对象，字段类型严格匹配。
- `file.write` 必须显式传 `allowWrite=true` 才允许执行；未显式授权时应返回阻断提示。
- `timeoutMs` 需按任务复杂度设置：
  - `file.read` / `file.write`：5000-30000
  - `process.exec`：5000-120000
  - `system.info`：30000
  - `system.screenshot`：60000
- `process.exec` 优先使用 `program + arguments`，仅在必须依赖 shell 时使用 `command`。
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

- `INVALID_PARAMS`：指出具体字段和类型问题，给出可直接重试的参数。
- `TIMEOUT`：建议增大 `timeoutMs` 或缩小任务范围。
- `FILE_READ_FAILED` / `FILE_WRITE_FAILED`：输出失败原因并给 `allowWrite`、路径、权限、目录存在性、`operation` 参数（如 `pattern`、`destinationPath`、`overwrite`）及回收站可用性排查建议。
- `PROCESS_EXEC_FAILED`：输出节点返回错误并给程序路径/参数/权限排查建议。
- `COMMAND_NOT_SUPPORTED`：改用已声明命令或升级节点版本。

## 输出规范

- 成功时：
  - 先一句话结论。
  - 再列关键字段（例如 `bytesWritten`、`exitCode`、`timedOut`、`url`）。
- 失败时：
  - 先给 `error.code`、`error.message`。
  - 再给一条可执行的下一步操作。

## 安全边界

- `file.write` 与 `process.exec` 默认按最小必要原则执行。
- 对可能破坏状态的操作（删除、覆盖、重置、停服务）先征得用户明确确认。
- 不自行提升权限，不绕过网关策略。

## 参考

- [references/command-spec.md](references/command-spec.md)
