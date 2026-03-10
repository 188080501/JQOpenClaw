# 一只小龙虾带一窝节点：JQOpenClaw 多 Node 架构接入 OpenClaw Gateway

GitHub 地址：<https://github.com/188080501/JQOpenClaw>

在很多分布式系统中，我们都会遇到一个典型问题：如何用一个中心节点统一管理大量服务节点？

OpenClaw 的解决方案非常直观：

```text
Gateway
   │
   ├── Node
   ├── Node
   ├── Node
   └── Node ...
```

如果用一个形象的比喻来说：

> Gateway 就像小龙虾的身体，而 Node 就像一只只爪子。

一个 Gateway 可以同时控制、管理和调度多个 Node。这也是本文标题所说的：```一只小龙虾带一窝节点```

注意，Gateway 既可以单独运行，作为系统入口和调度中心；也可以在需要分布式扩展能力时接入多个 Node。大部分教程，都是只安装了一个Gateway，其功能也可以满足日常使用。

本文将介绍：

- OpenClaw Gateway 与 Node 的关系
- Node 的官方能力
- 为什么需要 JQOpenClawNode
- 如何使用 Qt/C++ 一键运行 Node

## 一、OpenClaw 架构：Gateway + Node

在 OpenClaw 的体系中，系统分为两个核心角色。

### 1. Gateway

Gateway 是整个系统的**核心控制节点**，主要职责包括：

- 维护 Node 连接
- 统一调度服务
- 提供外部 API
- 管理节点状态

可以理解为：**系统控制中心**。

```text
           Gateway
              │
   ┌──────────┼──────────┐
   │          │          │
 Node A     Node B     Node C
```

所有 Node 都必须连接到某个 Gateway。也就是说：

> 没有 Gateway，就不存在 Node。

### 2. Node

Node 是 Gateway 的下属执行节点，作用通常包括：

- 提供计算能力
- 提供业务服务
- 执行任务
- 处理数据

因此可以总结为：

```text
Gateway = 管理者
Node    = 执行者
```

## 二、OpenClaw 官方 Node 实现

OpenClaw 官方已经提供了 Node 的原生能力。通常官方 Node 的运行方式是：

```text
Node.js 环境
   │
安装依赖
   │
运行 Node 服务
```

这种方式对于 Web 开发者 和Mac/Linxu 用户非常友好，但在 C++、工业软件、桌面软件场景中，会有一些问题。

### 1. 依赖 Node.js 环境

需要：

```bash
安装 Node.js
npm install
```

### 2. 依赖WLS

对很多部署环境来说并不方便，例如：

- 工业设备
- Windows 工控机
- 内网环境

这对于**希望直接运行可执行文件的软件环境**来说，并不理想。

## 三、JQOpenClawNode：Qt/C++ 原生 Node

为了解决这些问题，我实现了 `JQOpenClawNode`。

这是一个**基于 Qt/C++ 的 OpenClaw Node**实现，核心目标只有一个：

> 不依赖 Node.js，不需要安装，直接双击运行。

JQOpenClawNode 使用 Qt/C++ 实现 OpenClaw Node 协议。

核心优势：

- 无 Node.js
- 无 npm
- 无依赖安装

下载exe后双击运行即可连接 Gateway。

这对于以下场景非常友好：

- Windows 工控机
- 内网服务器
- 工业软件

### 3. 完全原生 C++ 实现

JQOpenClawNode 的所有逻辑都是原生 Qt/C++ 实现：

- 网络通信
- 节点注册
- 消息处理
- 任务执行

因此它具备：

- 高性能
- 低资源占用
- 易于嵌入现有系统

## 四、JQOpenClawNode 的架构

整体结构如下：

```text
                 OpenClaw Gateway
                        │
           ┌────────────┼────────────┐
           │            │            │
     JQOpenClawNode  JQOpenClawNode  JQOpenClawNode
           │            │            │
       Service A     Service B     Service C
```

Gateway 负责：

- 管理 Node
- 调度任务
- 转发请求

Node 负责：

- 提供能力
- 执行任务
- 返回结果

## 五、小龙虾架构

最后回到文章标题。

为什么叫“一只小龙虾带一窝节点”？

因为 OpenClaw 的结构就像这样：

```text
        Gateway
        /  |  \
      Node Node Node
```

如果想象成一只小龙虾：

```text
        (Gateway)
        /  /  /  /
       爪  爪  爪  爪
```

每一只爪子就是一个 Node，而 Gateway 就是中枢控制。

这就是 OpenClaw 的核心思想：

> 一个 Gateway，可以驱动无数 Node。

## 六、场景举例

下面这两个场景，都是可以直接用 node.invoke 落地的日常需求。

### 场景 1：夜间多机巡检 + 异常进程止损

典型背景：

- 1 台 Gateway 管理多台 Windows 工控机/产线机 Node
- 每晚需要统一做健康巡检和故障止损

推荐调用链路：

1. `system.info`：采集每台机器的 CPU/内存/磁盘/网络等基础信息。
2. `process.which`：先确认关键命令是否存在（例如 python、ffmpeg、cmd）。
3. `file.read`（operation=rg）：在日志目录批量检索 ERROR/FATAL。
4. `process.manage`（search/kill）：定位并处理卡死进程。
5. `file.write`（move/delete）：归档或清理历史日志（注意要显式传 allowWrite=true）。

这个场景的价值是：不用远程桌面逐台排查，Gateway 可以统一调度多 Node，快速完成“发现问题 -> 定位问题 -> 止损处理”。

### 场景 2：远程值守协助（截图取证 + 提示用户 + 自动化输入恢复）

典型背景：

- 1 台 Gateway 对接多个现场 Node，值班人员不方便频繁远程桌面
- 现场人员描述“卡住了/点不动了”，需要先看现场画面，再做最小动作恢复

推荐调用链路：

1. `system.screenshot`：先抓取当前屏幕状态做取证（需提前配置 `fileServerUrl` 和 `fileServerToken`，用于返回可访问图片 URL）。
2. `system.notify`：在现场机器弹出操作提示，通知用户先不要继续点击。
3. `system.clipboard`（read/write）：读取或写入关键文本（如临时口令、命令片段），避免人工口述出错。
4. `system.input`：按动作序列执行“移动鼠标/点击/输入/延时”等恢复步骤（当前为 Windows 能力）。
5. `system.screenshot`：再次抓图确认恢复结果并留档。

这个场景的价值是：把“电话沟通式排障”升级为“可视化、可追踪、可复现”的标准流程，特别适合多 Node 的远程值守和一线支持。
