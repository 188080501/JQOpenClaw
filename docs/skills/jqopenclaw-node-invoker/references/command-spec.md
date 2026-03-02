# JQOpenClawNode 调用规范

## 1. 通用调用骨架

使用网关方法 `node.invoke`：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "<command-name>",
    "params": {},
    "timeoutMs": 30000,
    "idempotencyKey": "<uuid>"
  }
}
```

约束：
- `nodeId`、`command`、`idempotencyKey` 必填。
- `params` 可省略，省略时等价空对象。
- `idempotencyKey` 每次请求使用新 UUID。

## 2. system.info

用途：采集系统基础信息。

调用：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "system.info",
    "params": {},
    "timeoutMs": 30000,
    "idempotencyKey": "<uuid>"
  }
}
```

返回重点（payload）：
- `cpuName`
- `cpuCores`
- `cpuThreads`
- `computerName`
- `memory`
- `gpuNames`
- `ip`
- `disks`

## 3. system.screenshot

用途：采集屏幕截图并返回上传后的图片 URL 信息。

调用：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "system.screenshot",
    "params": {},
    "timeoutMs": 60000,
    "idempotencyKey": "<uuid>"
  }
}
```

返回重点（payload 数组）：
- `format`
- `mimeType`
- `url`
- `width`
- `height`
- `screenIndex`
- `screenName`（可选）

## 4. process.exec

用途：远程执行进程命令（QProcess 实现）。

参数（二选一）：
- `command`：字符串。使用 `cmd.exe /C <command>` 运行。
- `program` + `arguments`：推荐方式，避免 shell 拼接风险。

其他参数：
- `workingDirectory`：字符串，可选。
- `stdin`：字符串，可选。
- `timeoutMs`：数字，可选，范围 `[100, 300000]`，默认 `30000`。
- `inheritEnvironment`：布尔，可选，默认 `true`。
- `environment`：对象，可选，键和值都必须是字符串。
- `mergeChannels`：布尔，可选，默认 `false`。

调用示例（program 模式）：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "process.exec",
    "params": {
      "program": "ping",
      "arguments": ["127.0.0.1", "-n", "2"],
      "timeoutMs": 15000
    },
    "timeoutMs": 20000,
    "idempotencyKey": "<uuid>"
  }
}
```

调用示例（command 模式）：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "process.exec",
    "params": {
      "command": "ipconfig /all",
      "timeoutMs": 20000
    },
    "timeoutMs": 25000,
    "idempotencyKey": "<uuid>"
  }
}
```

返回重点（payload）：
- `program`
- `arguments`
- `workingDirectory`
- `timeoutMs`
- `elapsedMs`
- `timedOut`
- `exitCode`
- `exitStatus`（`normal` / `crash`）
- `stdout`
- `stderr`
- `processError`
- `processErrorString`

## 5. 常见错误与处理

- `INVALID_PARAMS`
  - 原因：参数缺失、类型不符。
  - 处理：按字段修正后重试。

- `TIMEOUT`
  - 原因：节点执行超时。
  - 处理：增加 `timeoutMs` 或缩小执行范围。

- `COMMAND_NOT_SUPPORTED`
  - 原因：节点未实现该命令。
  - 处理：查看 `node.describe.commands`。

- `command not allowlisted`
  - 原因：网关策略拦截。
  - 处理：在网关配置 `gateway.nodes.allowCommands` 增加目标命令（如 `process.exec`）。

