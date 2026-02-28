# 文件上传下载服务说明

## 一、系统架构说明

本功能依赖 **Nginx 服务器** 提供 HTTP 文件存储能力。

整体结构如下：

客户端  ->  HTTPS(10038)  ->  Nginx  ->  本地文件系统 `/srv/openclaw/data/`

说明：

* Nginx 负责：
  * HTTPS 证书
  * Token 鉴权
  * PUT 文件落盘（WebDAV）
  * GET 文件读取
* 不依赖数据库
* 不依赖后端应用程序
* 所有文件直接存储在服务器本地目录

---

## 二、服务信息

* 协议：HTTPS
* 端口：10038
* 鉴权方式：上传接口需 Header Token（`GET /files/*` 不鉴权）
* 存储目录：`/srv/openclaw/data/`

---

## 三、鉴权机制（必须携带）

上传请求（`PUT /upload/*`）必须在 Header 中携带：

```
X-Token: CHANGE_ME_TOKEN
```

否则返回：

| 状态码 | 含义 |
| --- | --- |
| 401 | 上传未提供 Token |
| 403 | 上传 Token 错误 |

说明：

* 仅支持通过 HTTP Header 传递 Token
* 不支持 Cookie
* 不支持 Query 方式

---

## 四、文件上传

### 1. 请求方式

```
PUT /upload/<filename>
```

### 2. 完整示例

```
https://<server_ip>:10038/upload/test.bin
```

### 3. 请求要求

* Method: PUT
* Header: 必须包含 X-Token
* Body: 文件二进制数据

### 4. curl 示例

```powershell
curl -k -X PUT ^
  -H "X-Token: CHANGE_ME_TOKEN" ^
  --data-binary @test.bin ^
  https://127.0.0.1:10038/upload/test.bin
```

### 5. 存储位置

上传成功后文件保存为：

```
/srv/openclaw/data/test.bin
```

### 6. 覆盖规则

* 如果文件已存在，将直接覆盖
* 不自动重命名
* 文件名由客户端决定

---

## 五、文件下载

### 1. 请求方式

```
GET /files/<filename>
```

### 2. 完整示例

```
https://<server_ip>:10038/files/test.bin
```

### 3. curl 示例（无需 Token）

```powershell
curl -k ^
  https://127.0.0.1:10038/files/test.bin ^
  -o test.bin
```

---

## 六、支持的状态码

| 状态码 | 说明 |
| --- | --- |
| 200 | 下载成功 |
| 201 / 204 | 上传成功 |
| 400 | 非法路径（包含 `..`） |
| 401 | 上传未提供 Token |
| 403 | 上传 Token 错误 |
| 404 | 文件不存在 |
| 405 | 请求方法不允许 |

---

## 七、文件命名建议

文件名由客户端决定。

建议使用：

* 时间戳
* UUID
* 业务唯一 ID

示例：

```
20260301_171500.bin
550e8400-e29b-41d4-a716-446655440000.bin
```

注意：

* 不允许包含 `..`
* 不建议使用特殊字符

---

## 八、上传大小限制

当前 nginx 配置：

```
client_max_body_size 0;
```

表示不限制上传大小（实际受服务器磁盘容量限制）。

---

## 九、快速测试流程

```powershell
# 上传
curl -k -X PUT ^
  -H "X-Token: CHANGE_ME_TOKEN" ^
  --data-binary @a.bin ^
  https://127.0.0.1:10038/upload/a.bin

# 下载
curl -k ^
  https://127.0.0.1:10038/files/a.bin ^
  -o a.bin
```

---

## 十、注意事项

1. 该服务依赖 Nginx 正常运行。
2. 若 PUT 不生效，请确认 nginx 已启用 WebDAV 模块。
3. 服务器不会做文件去重、版本管理或日志记录。
4. 同名文件再次上传会覆盖原文件。
5. 生产环境建议正确配置 HTTPS 证书校验。
