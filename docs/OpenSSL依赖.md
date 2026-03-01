# OpenSSL 依赖说明

本文档说明 `JQOpenClaw` 在 Windows + qmake 场景下的 OpenSSL 依赖与集成方式。

## 1. 依赖用途

- 用于设备签名与鉴权相关加密能力（`Ed25519`、`SHA-256`、编码流程等）。
- 当前工程仅链接 `libcrypto`，不依赖 `libssl`。

## 2. 关联文件

- 构建配置：`modules/openssl.pri`
- 引入入口：`modules/crypto.pri`
- 业务代码：`modules/crypto/*`

## 3. 环境准备

1. 安装可用的 OpenSSL for Windows（例如 WinUniversal 发行包）。
2. 设置环境变量 `OPENSSL_ROOT` 指向安装目录。
3. 若未设置 `OPENSSL_ROOT`，默认使用 `C:/Develop/OpenSSL`。

示例：

```powershell
setx OPENSSL_ROOT "C:\Develop\OpenSSL"
```

## 4. 支持的目录布局

`openssl.pri` 当前支持两类布局：

1. 分层布局（tiered）

```text
<OPENSSL_ROOT>/<arch>/<Debug|Release>/<v142|v143>/<static|dynamic>/libcrypto*.lib
```

2. WinUniversal 布局

```text
<OPENSSL_ROOT>/lib/VC/<arch>/<MD|MDd|MT|MTd>/libcrypto*.lib
```

头文件路径支持：

```text
<OPENSSL_ROOT>/include/openssl
<OPENSSL_ROOT>/include/<arch>/openssl
```

## 5. 自动探测规则

### 5.1 链接方式

- 默认 `dynamic`
- 若 Qt 为静态构建（`contains(QT_CONFIG, static)`），默认切换 `static`
- 可通过环境变量或 qmake 变量覆盖：`OPENSSL_LINKAGE=static|dynamic`

### 5.2 架构

从 `QT_ARCH` / `QMAKE_TARGET.arch` / `QMAKE_HOST.arch` 依次推断：

- `x86_64/amd64` -> `x64`
- `i386/i686/x86` -> `x86`
- `arm64/aarch64` -> `arm64`

### 5.3 编译配置与运行时

- `Debug` / `Release` 来自 qmake 构建配置
- `MD/MDd/MT/MTd` 由编译选项自动推断（优先匹配当前 CFLAGS）

### 5.4 查找顺序

1. 先尝试分层布局（按 toolset 候选）
2. 找不到再尝试 WinUniversal 布局（按 runtime 候选）
3. 都找不到则中断构建并报错

### 5.5 动态库复制

当 `OPENSSL_LINKAGE=dynamic` 时：

- 自动搜索 `libcrypto-3-*.dll` / `libcrypto.dll`
- 找到后在 `post link` 阶段复制到 `DESTDIR`
- 未找到则给出 warning（运行时可能需要手动配置 PATH）

## 6. 常见问题

1. 报错 `headers not found`  
检查 `OPENSSL_ROOT` 与 `include/openssl/crypto.h` 是否存在。

2. 报错 `libcrypto not found`  
检查安装包布局是否在支持范围内，或手动调整目录结构。

3. 运行时报缺少 `libcrypto*.dll`  
确认动态库已复制到输出目录，或将 DLL 所在目录加入系统 `PATH`。

## 7. 建议

- 团队内统一 `OPENSSL_ROOT` 目录规范，减少环境差异。
- 生产环境尽量固定版本，避免升级引入 ABI 或目录变化。
- 若需要新增布局支持，优先改 `modules/openssl.pri` 并同步更新本文档。
