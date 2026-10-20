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

## Entity Inspector selection and edits

`WorldOutline` publishes its multi-selection to Editor `Selection`. `ObjectDetail` filters valid entities,
shows the intersection of their registered components, and rebuilds on selection, active scene or scene
structure changes. `ComponentEditorContext::_entities` is the selected target snapshot; `_entity` remains
available to single-entity editors. `ComponentEditorRegistry::CreateEditor()` retains editors with native
multi-edit support and otherwise uses `MultiComponentEditor`; existing single-selection editors remain.

The multi-editor uses explicit schemas for custom components and reflection bindings for generic components.
It reads every selected entity before showing a field, indicates mixed boolean/enum/asset/scalar values,
and writes absolute values only to that field (one axis for vectors). Script properties intersect by asset,
name and value type; Collider2D shapes and material slots intersect by index and compatible field type.
Subtype-specific fields require compatibility on every target. Component enabled state supports multi-edit;
persistent identity and hierarchy relationships remain read-only. Schema signatures detect dynamic changes
without recreating all bindings every frame. Popup callbacks reject changed selections and expired panels.

Transform supports per-axis mixed values and absolute local-coordinate multi-edit. Its commands retain
weak Scene ownership and entity GUIDs, resolve components for each application, and use existing setters
plus interpolation reset and scene dirty tracking. Live input changes become one command on Return,
focus loss, drag release or panel destruction. `CommandManager::BeforeHistoryChange()` lets an active
edit commit before Undo/Redo. Generic property commands also retain weak Scene ownership and entity GUIDs,
and own value snapshots (including coupled animator state and dynamic arrays), never component addresses.
Material parameter snapshots retain their original asset so reassignment cannot redirect Undo. UI input
constraints do not clamp restored snapshots. Serialized changes mark the scene/material asset dirty;
runtime probe/bake/spawn actions stay transient. Existing component serialization coverage remains unchanged.
Raw SceneMgr-owned play-mode copies retain direct editing without runtime pointers in Undo. Multi-selection
component add/remove, entity enabled state and Prefab actions are currently unavailable.

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

In edit mode, the active GameView selects the active scene's main camera for the existing primary render output;
returning to SceneView restores the editor scene camera. GameView displays the no-main-camera message in either mode.
Hidden SceneView panels unregister their selected-camera preview so it cannot redirect the GameView camera into a preview RT.
Play mode retains runtime scene/camera selection and simulation behavior.

Selected-camera previews retain their output RT between renders. RenderPipeline schedules the preview only after
selection/target changes or a change in the active Scene ID/EditRevision; the main SceneView camera remains per-frame.
This uses editing revisions, not the persistent unsaved flag; changes that bypass MarkEdited/TouchStructure do not refresh
the preview. Preview temporal effects therefore advance only on requested refreshes.

UI layout invalidation propagates upward even when the source or an intermediate ancestor is already dirty.
Hidden elements can retain local dirty state while visible ancestors complete layout; local dirtiness is not evidence
that ancestors were notified. Explicit downward invalidation visits the full subtree independently. Visibility changes
use the same hierarchy/layout invalidation path, so newly shown children are arranged before normal painting.

## Widget paint and texture caches

Editor Widgets retain the Engine UI paint invalidation chain and per-frame geometry buffers. Geometry remains the default;
optional texture caching changes draw submission, not Dock/business Tick or Scene rendering.
DockTab synchronizes its active item's rect before Update, including nested tabs whose outer node rect is unchanged;
hidden tabs defer business updates and receive current bounds when active. The experimental Editor switch
opts in toolbar/status, Dock title/tab bars and selected WorldOutline/Inspector/AssetBrowser content Widgets. It does not opt in
SceneView/GameView, Popup or arbitrary plugin content.

`Widget::SetTextureCacheMode(kTexture)` requests a persistent local sRGB RT. UIRenderer validates the actual DrawNodes and
falls back to Geometry for untracked RenderTexture/backdrop reads, unknown materials, untracked image dependencies, depth
outputs or invalid/oversized bounds. Standard UI/color/image geometry and the established font shader are supported; images
require an owner revision contract through Widget::SetTextureCacheImageRevision. AssetBrowser supplies PreviewRevision for
published static preview RTs; it still rebuilds content when previews change. Upload revisions additionally invalidate cached
pixels after texture/font reuploads. Live RenderTextures without an audited owner revision still fall back to Geometry.
Existing `InvalidatePaint`, size/position and output changes dirty the texture; moving or changing its composite material also
forces repaint. Prepare owns resource selection and submission snapshots; Record uses the snapshotted origin/material mode
and updates renderer-owned validity. RenderPipeline waits for its UI recording job before submission/frame advancement.

Cache-only UI/font shaders accumulate alpha with `Src,OneMinusSrc,One,OneMinusSrc`; the transparent RT therefore stores
premultiplied RGB and coverage. Composition uses `One,OneMinusSrc,One,OneMinusSrc`. Shader metadata accepts optional four-factor
Blend syntax (color source/destination, alpha source/destination); existing two-factor shaders preserve their alpha state.
Fractional Dock bounds are enclosed by integer pixel edges to preserve sample centers. Local projection and clamped scissor subtract the cache origin, while the composite quad remains in window coordinates.
UI DrawNode scissors encode `(left, top, right, bottom)` in `Rect.left/top/width/height`, matching DX12 `ToD3DRect`.
Cache translation subtracts its origin from all four endpoints, clamps them to the target and keeps right/bottom as endpoints;
it must not add left/top to the width/height fields or convert the result back to extents. Viewport Rect fields still use extents.
The experimental opt-in is stored in `EngineConfig::_enable_editor_widget_texture_cache` (default false), loaded from
`Editor/EngineConfig.json` before Dock creation and reapplied after layout restoration. The Reflector checkbox edits this
configuration directly; normal Application finalization saves it with the other EngineConfig properties. Individual Widget
modes remain runtime-only. `AILU_EDITOR_UI_TEXTURE_CACHE` is a process-local profiling override (0 = Geometry, 1 = opaque
toolbar only, 2 = expanded texture caches); it does not change the persisted preference.
The historical `kOpaqueTexture` toolbar trial remains available; default Geometry and
unsupported-content fallback remain available for comparison. Live visual/performance acceptance is tracked in the active CPU ExecPlan.

Reflector visibility alone preserves normal Widget update scheduling and texture caches. Its opt-in
`Visualize paint dirty state` mode snapshots paint dirty state, forces visible Widget updates and falls back to Geometry;
the Reflector displays this measurement caveat. Selection highlighting is drawn separately and remains available with caches.
Closing the Reflector disables dirty visualization. These debug flags are runtime-only and are not serialized.

## Automation

Editor 当前已有 automation service/pipe。`EditorAutomationRegistry` 是 automation method 的唯一注册源；Editor Terminal
通过 `AutomationConsoleBridge` 将这些 method 以同名 console command 暴露给本地交互调用，复用原 handler、schema、权限描述和
Editor main-thread 执行边界。Terminal 参数使用 `name=value`，复杂值使用现有 automation JSON 表示；这条桥接不改变 pipe
或远程 caller 的权限策略。

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

## Editor progress

`EditorApp` owns `EditorProgressService`; `EditorLayer` owns the floating `EditorProgressPanel` and unregisters it in OnDetach.
The service is state only and does not schedule work or reference UI. Copyable handles share task ownership, so producers can
outlive registry cleanup without holding a service or Widget pointer. Per-task short locks serialize counters, messages and terminal
transitions; registry locks only protect membership. Terminal updates are immutable and cancellation requests remain distinct
from producer acknowledgement. Completed/cancelled rows expire after two seconds; failures retain their reason until dismissed,
with a maximum of sixteen retained failures. The panel uses three persistent rows, Canvas fill bars, popup ordering and a status
summary while retaining Scene/Selection/FPS information. Cards use the existing backdrop-blur brush with a translucent dark tint
and 10px rounded corners; inset progress tracks/fills use 3px corners. UI mutation stays on the main thread.

The Debug toolbar Menu > Progress Demo dispatches three test producers through JobSystem. EditorApp stops accepting tasks and
requests cancellation before waiting for its demo JobGroup, then proceeds with normal Editor/Engine shutdown. Other producer owners
must stop/wait their own work; a progress task does not own execution and cancelling it cannot kill a worker or synchronous operation.
`AssetBrowser` supplies the EditorApp-owned service to its `AssetImportController`. Each batch retains one progress handle;
appended files increase its total and each queued item preserves its destination. Browser Update ticks the main-thread queue:
a preparation frame precedes each synchronous direct import, and the next file never starts recursively from a dialog callback.
Texture/Mesh confirmation, skipping and external popup closure account for one file exactly once. Group-scoped popup closure and
weak lifetime guards keep callbacks from touching a destroyed Browser/controller. Successful imports notify the Browser once;
null results/exceptions count as failures while remaining files continue. Queue cancellation only stops pending files at a safe point;
it cannot interrupt synchronous ImportResource. Batch summaries report success/skipped/failed totals. The active ExecPlan tracks
M2 runtime acceptance; yielding between files does not make single-file import asynchronous.

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

## Blender Live Link progress reporting

PluginManager implements an optional IPluginProgressHost side interface, discovered through RTTI from IPluginHost.
The existing ABI v1 host vtable and PluginContext layout remain unchanged; compatible C++ toolchains remain required.
EditorApp supplies a host-owned callback that copies strings into EditorProgressHandle state, retaining one handle per title.
The Engine host checks owner-thread affinity and contains callback exceptions. No Editor types or plugin closures cross the ABI.
Blender Full Sync begins one non-cancellable indeterminate task, reports apply stages and packet counts, and terminates on End,
disconnect, overflow, replacement Begin or unload. LiveLinkSession exposes the last Apply result so failed data does not report
successful completion. Begin yields the current Tick; subsequent batches yield after approximately 4ms between commands.
Individual synchronous Apply calls remain non-interruptible. Incremental updates do not create progress cards. The transport
and Blender add-on protocol remain unchanged; this task is not a percentage of Blender bake or network byte completion.
