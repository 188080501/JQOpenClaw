---
name: jqopenclaw-node-invoker
description: 统一通过 Gateway 的 node.invoke 调用 JQOpenClawNode 能力（system.info、system.screenshot、process.exec）。当用户需要远程采集系统信息、抓取截图、执行进程命令、排查节点命令可用性或修复 node.invoke 参数错误时使用。
---

# JQOpenClaw Node Invoker

## 快速流程

1. 确定目标 `nodeId`（用户给定优先）。
2. 先调用 `node.describe` 检查节点是否在线、是否声明目标命令。
3. 若命令未声明或被网关拦截，先输出阻断原因，再给修复建议。
4. 按 [references/command-spec.md](references/command-spec.md) 构造 `node.invoke` 请求。
5. 对 `idempotencyKey` 使用新的 UUID。
6. 返回结果时，先给结论，再给关键字段（不要只贴原始 JSON）。

## 命令映射

- 系统基础信息: `system.info`
- 屏幕截图: `system.screenshot`
- 远程进程执行: `process.exec`

## 调用规则

- 统一使用 `node.invoke`，不要绕过网关直接假设本地执行。
- `params` 必须是 JSON 对象；字段类型必须严格匹配。
- `timeoutMs` 要和任务复杂度匹配：
  - `system.info`: 30000
  - `system.screenshot`: 60000
  - `process.exec`: 5000-120000（按命令复杂度）
- `process.exec` 优先使用 `program + arguments`；只在必须依赖 shell 时使用 `command`。

## 网关阻断处理

- 若报错包含 `command not allowlisted`：
  - 说明这是 Gateway 策略拦截。
  - 提示管理员在 Gateway 配置添加 `gateway.nodes.allowCommands`（如 `process.exec`）。
- 若报错包含 `command not declared by node` 或 `node did not declare commands`：
  - 提示先看 `node.describe` 返回的 `commands`。
  - 要求节点端先声明命令再调用。

## 错误处理规范

- `INVALID_PARAMS`: 明确指出字段名和类型问题，给可执行修正版本。
- `TIMEOUT`: 建议增大 `timeoutMs` 或缩小命令范围。
- `PROCESS_EXEC_FAILED`: 输出节点返回的失败信息，并给下一步排查建议（路径、权限、程序名）。
- `COMMAND_NOT_SUPPORTED`: 提示改用已声明命令或升级节点版本。

## 输出规范

- 成功时：
  - 先一句话结论。
  - 再列关键字段（例如 `exitCode`、`timedOut`、`stdout` 摘要）。
- 失败时：
  - 先给 `error.code` 和 `error.message`。
  - 再给一条可直接执行的下一步（不是泛泛建议）。

## 安全边界

- `process.exec` 默认按“最小必要”原则执行。
- 对可能破坏系统状态的命令（删除、覆盖、重置、停服务）先征得用户明确确认。
- 不自行提升权限，不绕过网关策略。

## 参考

- [references/command-spec.md](references/command-spec.md)
