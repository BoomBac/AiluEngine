# Runtime Lifecycle

## Purpose

本文档描述 AiluEngine 当前应用和 runtime subsystem 的 ownership、初始化、frame orchestration 与 shutdown 边界。

主要入口：

- `Engine/Inc/Framework/Common/Application.h`
- `Engine/Inc/Framework/Common/EngineRuntime.h`
- `Editor/Inc/EditorApp.h`
- `Engine/Inc/Render/GraphicsContext.h`

## Ownership overview

当前核心关系可概括为：

```text
Application
├── Window
├── InputSystem
├── LayerStack
├── RenderPipeline
└── EngineRuntime
    ├── Core::ThreadPool
    ├── JobSystem
    ├── ResourceSystem / ResourceMgr
    ├── ScriptSystem
    ├── SceneMgr
    └── GraphicsContext reference/lifecycle integration
```

`Application` 是应用层 orchestrator。

`EngineRuntime` 集中管理主要 runtime subsystem 的生命周期。

Editor 通过 `EditorApp : Application` 在基础 runtime 上增加 Editor-only 服务，例如：

- file watcher / reload integration；
- scene camera；
- editor layers；
- automation service；
- automation pipe server。

## Application responsibilities

`Application` 当前负责：

- application initialization；
- window；
- input/events；
- layer update；
- resource update；
- scene update；
- render preparation；
- rendering/present；
- main/render thread synchronization；
- frame/fixed update state；
- shutdown orchestration。

这意味着 `Application` 是“调度者”，不应成为任意 subsystem 业务逻辑的存放位置。

新增系统时，优先问：

1. 谁拥有它？
2. 谁创建它？
3. 谁 tick 它？
4. shutdown 前需要 drain 什么？
5. 是否需要 main thread？
6. 是否依赖 GraphicsContext/Scene/ResourceMgr？
7. Editor 和 Player 是否都需要？

## EngineRuntime responsibilities

`EngineRuntime` 当前拥有或管理：

```text
Core::ThreadPool
JobSystem
ResourceSystem
ScriptSystem
SceneMgr
GraphicsContext
audio / gizmo / UI initialization state
```

其生命周期接口包括：

```text
Initialize
BeginShutdown
Drain
Shutdown
```

这是新增长期 runtime subsystem 时优先考虑的 owner。

不要仅因为已有某些 subsystem 提供静态 `Get()`，就绕过 runtime ownership 再创建新的独立 singleton。

静态访问接口和实际 ownership 是两个问题。

## Frame orchestration

`Application` 的 frame loop 被拆成多个阶段，包括：

```text
BeginFrame
UpdateInputAndEvents
UpdateResources
UpdateLayers
UpdateScenes
PrepareRender
RenderFrame
RenderEditor
PresentFrame
EndFrame
```

具体调用顺序应以 `Application.cpp` 当前实现为准。

新增帧级同步点时，必须先明确它属于：

- 输入/事件；
- resource publish/reload；
- scene mutation；
- render preparation；
- render submission；
- frame cleanup。

不要把“任何需要每帧执行的工作”都塞入一个通用 Tick。

## Main thread and render thread

`Application` 暴露：

```text
WaitForRender
WaitForMain
NotifyMain
NotifyRender
SetMultiThreadRendering
```

`RenderPipeline` 也通过 frame pending 状态与 Application 协调。

因此修改以下内容时必须检查 render thread 同步：

- Scene lifetime；
- runtime scene retirement；
- GPU resource destruction；
- render command ownership；
- frame resource reuse；
- Editor 切换场景/play mode。

## Shutdown model

当前存在明确的 staged shutdown 概念：

```text
BeginShutdown
Drain
Shutdown
```

新增 subsystem 不应只有析构函数兜底。

如果 subsystem 可能拥有：

- worker jobs；
- async IO；
- callbacks；
- queued main-thread operations；
- GPU resources；
- external connection；
- plugin DLL code；

则应设计明确的：

```text
stop accepting new work
→ drain/cancel outstanding work
→ detach callbacks
→ release dependent resources
→ destroy subsystem
```

插件系统尤其必须遵守这一点：不能在仍有 callback/job/RenderFeature 指向 DLL 代码时卸载动态库。

## New subsystem checklist

新增长期 runtime subsystem 前至少回答：

```text
Owner:
Initialize after:
Initialize before:
Tick phase:
Thread affinity:
Dependencies:
Shutdown before:
Drain requirements:
Editor-only or runtime:
Static access required:
```

这些信息应写入 architecture 文档或对应 ExecPlan。

## Current limitation

当前 runtime 仍混合使用：

- explicit owner；
- static `Get()` / singleton access；
- raw pointer to global service；
- subsystem-internal global state。

架构文档不假设这些问题已经被完全解决。

新增代码的目标是不要继续扩大隐式 ownership。
