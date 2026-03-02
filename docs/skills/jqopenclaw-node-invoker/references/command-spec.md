# JQOpenClawNode 调用规范

## 1. 通用调用骨架

统一走 `node.invoke`：

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
- 每次请求使用新 UUID 作为 `idempotencyKey`。

## 2. file.read

用途：读取文件内容、目录列表，或执行 `rg` 搜索。

`params`：
- `path`：字符串，必填。
- `operation`：字符串，可选，默认 `read`。可选值：`read` / `list` / `rg`。
- `read` 模式参数：
  - `encoding`：字符串，可选，`utf8`（默认）或 `base64`。
  - `maxBytes`：数字，可选，默认 `1048576`，范围 `[1, 20971520]`。
- `list` 模式参数：
  - `includeEntries`：布尔，可选，默认 `true`。是否返回目录项列表。
  - `maxEntries`：数字，可选，默认 `200`，范围 `[1, 5000]`。仅在 `includeEntries=true` 时生效。
- `rg` 模式参数：
  - `pattern`：字符串，必填。
  - `maxMatches`：数字，可选，默认 `200`，范围 `[1, 5000]`。
  - `caseSensitive`：布尔，可选，默认 `false`。
  - `includeHidden`：布尔，可选，默认 `false`。
  - `literal`：布尔，可选，默认 `false`（固定字符串匹配）。

示例：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "file.read",
    "params": {
      "operation": "read",
      "path": "C:/Windows/win.ini",
      "encoding": "utf8",
      "maxBytes": 8192
    },
    "timeoutMs": 15000,
    "idempotencyKey": "<uuid>"
  }
}
```

示例（list 模式）：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "file.read",
    "params": {
      "operation": "list",
      "path": "C:/Temp",
      "includeEntries": true,
      "maxEntries": 200
    },
    "timeoutMs": 15000,
    "idempotencyKey": "<uuid>"
  }
}
```

示例（rg 模式）：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "file.read",
    "params": {
      "operation": "rg",
      "path": "C:/Repos/project",
      "pattern": "TODO",
      "maxMatches": 200
    },
    "timeoutMs": 30000,
    "idempotencyKey": "<uuid>"
  }
}
```

返回重点（payload）：
- 公共字段：
  - `path`
  - `operation`
  - `targetType`：`file` 或 `directory`
- `read` 模式字段：
  - `encoding`
  - `sizeBytes`
  - `readBytes`
  - `truncated`
  - `content`
- `list` 模式字段：
  - `directoryCount`
  - `fileCount`
  - `otherCount`
  - `totalCount`
  - `includeEntries`
  - `maxEntries`（仅 `includeEntries=true` 返回）
  - `truncated`（仅 `includeEntries=true` 返回）
  - `entries`（仅 `includeEntries=true` 返回，元素字段：`name`、`path`、`type`、`isSymLink`、`sizeBytes`[文件项才有]）
- `rg` 模式字段：
  - `pattern`
  - `caseSensitive`
  - `includeHidden`
  - `literal`
  - `maxMatches`
  - `matchCount`
  - `fileCount`
  - `truncated`
  - `rgExitCode`
  - `stderr`（可选）
  - `matches`（数组元素字段：`path`、`lineNumber`、`columnStart`、`columnEnd`、`lineText`、`matchText`）

## 3. file.write

用途：写入文件内容，或执行移动（剪切）与删除操作。

`params`：
- `path`：字符串，必填。
- `allowWrite`：布尔，可选，默认 `false`。必须显式传 `true` 才允许执行 `file.write`。
- `operation`：字符串，可选，默认 `write`。可选值：`write` / `move`（或 `cut`）/ `delete`（或 `remove`）。
- `write` 模式参数：
  - `content`：字符串，必填。
  - `encoding`：字符串，可选，`utf8`（默认）或 `base64`。
  - `append`：布尔，可选，默认 `false`。
  - `createDirs`：布尔，可选，默认 `true`。
- `move` 模式参数：
  - `destinationPath` 或 `toPath`：字符串，必填（目标路径）。
  - `overwrite`：布尔，可选，默认 `false`（目标存在时是否覆盖）。
  - `createDirs`：布尔，可选，默认 `true`（自动创建目标父目录）。
- `delete` 模式参数：
  - 无额外必填参数。删除行为固定为移动到回收站（`QFile::moveToTrash`）。

示例：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "file.write",
    "params": {
      "allowWrite": true,
      "operation": "write",
      "path": "C:/Temp/jqopenclaw-output.txt",
      "content": "hello from node",
      "encoding": "utf8",
      "append": false,
      "createDirs": true
    },
    "timeoutMs": 15000,
    "idempotencyKey": "<uuid>"
  }
}
```

示例（move 模式）：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "file.write",
    "params": {
      "allowWrite": true,
      "operation": "move",
      "path": "C:/Temp/a.txt",
      "destinationPath": "C:/Temp/archive/a.txt",
      "overwrite": true
    },
    "timeoutMs": 15000,
    "idempotencyKey": "<uuid>"
  }
}
```

示例（delete 模式）：

```json
{
  "method": "node.invoke",
  "params": {
    "nodeId": "<node-id>",
    "command": "file.write",
    "params": {
      "allowWrite": true,
      "operation": "delete",
      "path": "C:/Temp/old-data"
    },
    "timeoutMs": 15000,
    "idempotencyKey": "<uuid>"
  }
}
```

返回重点（payload）：
- 公共字段：
  - `operation`
  - `path`
- `write` 模式字段：
  - `encoding`
  - `appended`
  - `bytesWritten`
  - `sizeBytes`
- `move` 模式字段：
  - `fromPath`
  - `toPath`
  - `targetType`：`file` 或 `directory`
  - `overwritten`
  - `moved`
- `delete` 模式字段：
  - `targetType`：`file` 或 `directory`
  - `deleted`
  - `deleteMode`：固定 `trash`

## 4. process.exec

用途：远程执行进程命令（QProcess）。

`params`：
- `command`：字符串，可选。存在时走 `cmd.exe /C <command>`。
- `program`：字符串，可选。`command` 未提供时必填。
- `arguments`：字符串数组，可选。
- `workingDirectory`：字符串，可选。
- `stdin`：字符串，可选。
- `timeoutMs`：数字，可选，默认 `30000`，范围 `[100, 300000]`。
- `inheritEnvironment`：布尔，可选，默认 `true`。
- `environment`：对象，可选，键和值都必须是字符串。
- `mergeChannels`：布尔，可选，默认 `false`。

示例（program 模式）：

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

返回重点（payload）：
- `program`
- `arguments`
- `workingDirectory`
- `timeoutMs`
- `elapsedMs`
- `timedOut`
- `exitCode`
- `exitStatus`
- `stdout`
- `stderr`
- `ok`：布尔。是否为正常完成且 `exitCode == 0`。
- `resultClass`：字符串。`ok` / `non_zero_exit` / `crash` / `timeout`。
- `processError`：数字，可选。仅在存在进程级错误时返回。
- `processErrorName`：字符串，可选。仅在存在进程级错误时返回，取值：`failed_to_start` / `crashed` / `timed_out` / `read_error` / `write_error`。
- `processErrorString`：字符串，可选。仅在存在进程级错误时返回。

判定建议：
- 优先使用 `ok` 与 `resultClass` 判断执行结果。
- 无进程级错误时，不返回 `processError*` 字段。

## 5. system.screenshot

用途：采集全部屏幕截图并返回上传后的 URL 信息。

返回重点（payload 数组元素）：
- `format`
- `mimeType`
- `url`
- `width`
- `height`
- `screenIndex`
- `screenName`（可选）

## 6. system.info

用途：采集系统基础信息。

返回重点（payload）：
- `cpuName`
- `cpuCores`
- `cpuThreads`
- `computerName`
- `memory`
- `gpuNames`
- `ip`
- `disks`

## 7. 常见错误与处理

- `INVALID_PARAMS`
  - 参数缺失或类型不匹配。
  - 修正字段后重试。

- `FILE_READ_FAILED` / `FILE_WRITE_FAILED`
  - 常见原因：路径错误、权限不足、父目录不存在、内容编码不合法、移动目标已存在、系统回收站不可用或拒绝接收目标。
  - 优先检查 `path`、`operation`、`encoding`、`pattern`、`maxMatches`、`allowWrite`、`createDirs`、`overwrite`。

- `PROCESS_EXEC_FAILED`
  - 常见原因：程序不存在、参数错误、权限不足、超时。
  - 优先检查 `program`、`arguments`、`workingDirectory`、`timeoutMs`。

- `TIMEOUT`
  - 节点执行超时。
  - 增大 `timeoutMs` 或缩小执行范围。

- `COMMAND_NOT_SUPPORTED`
  - 节点未实现该命令。
  - 检查 `node.describe.commands`。

- `command not allowlisted`
  - 网关策略拦截。
  - 在网关配置 `gateway.nodes.allowCommands` 增加目标命令（如 `file.read`、`file.write`）。
