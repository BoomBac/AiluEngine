# Editor and Runtime Boundary

## Purpose

本文档描述当前 Editor 与 Engine runtime 的依赖边界。

主要入口：

- `Editor/Inc/EditorApp.h`
- `Editor/Src`
- `Engine/Inc`
- `Player`

## Dependency direction

基本原则：

```text
Editor
  ↓
Engine
```

而不是：

```text
Engine
  ↓
Editor
```

Engine 中的 runtime subsystem 不应 include 或调用 Editor concrete implementation。

## EditorApp

`EditorApp` 继承 `Application`。

因此 Editor 使用与 Player 相同的基础 runtime lifecycle，再增加：

```text
EditorLayer
InputLayer
SceneLayer
Scene camera/controller
FileWatchService
ResourceReloadService
EditorAutomationService
AutomationPipeServer
Editor config
```

Editor-specific lifecycle 应在 Editor 层接入，而不是让 EngineRuntime 知道 Editor UI。

## Editor-only data

以下类型的信息通常应位于 Editor：

- dock/window state；
- selection；
- inspector state；
- timeline UI；
- asset browser UI；
- scene view camera control；
- editor automation presentation；
- drag/drop UI；
- editor-only render pass。

如果某功能在 Player 运行时不需要，应先问是否真的应该放 Engine。

## Runtime-facing extension

Editor 经常需要驱动 Engine，例如：

- mutate Scene；
- import/reload Asset；
- request render capture；
- add Editor render feature/pass；
- control preview camera；
- inspect reflection data。

正确方向是 Engine 暴露足够的稳定 API/extension point，Editor 调用这些 API。

不要为了 Editor 方便在 Engine 中加入对具体 Editor class 的反向引用。

## File watching and reload

EditorApp 当前拥有 file watcher/reload integration。

这意味着“检测外部文件变化”是 Editor-side concern，而 runtime asset publish/reload core 可以继续位于 Engine。

保持：

```text
Editor detects / coordinates
→ Engine asset system applies runtime update
```

比让 ResourceMgr 直接依赖 Editor watcher 更清晰。

## Editor rendering

Editor-specific rendering，例如 picking/debug overlay，可通过 Engine rendering data 和 feature/pass extension 接入。

Engine Renderer 不应硬编码 Editor type。

## Automation

Editor 当前已有 automation service/pipe。

新的外部工具、AI、Live Link 或 DCC integration 需要先判断：

```text
Is this editor integration?
Is this reusable runtime transport?
Is this plugin infrastructure?
```

例如 Blender Live Link 可以由：

```text
Engine/plugin infrastructure
+ Editor-side plugin/bridge
```

共同组成，而不必把所有协议和 UI 都放进 Engine core。

## Shutdown

Editor-owned service shutdown 应早于其依赖的 Engine subsystem 完全销毁。

特别检查：

- watcher callback；
- automation pipe；
- background tasks；
- render callbacks；
- plugin callback；
- Scene pointer；
- ResourceMgr callback。

## Future plugin system constraint

Current plugin hosting is implemented by Engine `PluginManager` and owned by `EditorApp`. The manager is
initialized after successful base Application initialization, ticked from an Editor-owned before-update delegate,
and shut down/destroyed after unsubscribing that delegate and before other Editor service/Application teardown.
`GetPluginManager()` permits explicit manual loading. Editor scans Engine and Project Plugins roots, then loads
only manifests marked autoload. There is no plugin UI. Engine-owned registration records release before
OnUnload/Destroy; concrete Editor menu/panel/callback extension services remain follow-up work.

若动态插件同时支持 runtime 和 Editor：

建议从一开始区分 capability：

```text
Runtime plugin
Editor plugin
Rendering extension
Importer/tool extension
```

Engine core 的 plugin manager 可以负责 DLL/lifecycle，但 Editor-specific registration 应通过 Editor 提供的 host API 完成。

不要让 Engine plugin ABI 直接依赖 Editor concrete class。
