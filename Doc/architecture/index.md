# AiluEngine Architecture Index

本文档是 AiluEngine 长期架构文档的入口。

这里不保存具体任务状态，也不描述尚未实现的目标架构。当前实现以源码为最终依据。

## Architecture map

| Area | Document | Primary code |
| --- | --- | --- |
| Runtime lifecycle | `runtime-lifecycle.md` | `Application`, `EngineRuntime` |
| Plugin foundation | `plugin-system.md` | `DynamicLibrary`, `PluginApi`, standalone plugin smoke tests |
| Threading | `threading.md` | `JobSystem`, `ThreadPool`, Application render sync |
| Scene / ECS | `scene-ecs.md` | `Engine/Inc/Scene`, ECS components/register |
| Assets | `asset-system.md` | `ResourceMgr`, `AssetRegistry`, asset documents/artifacts |
| Rendering | `rendering.md` | `RenderPipeline`, `Renderer`, `RenderingData` |
| RenderGraph | `render-graph.md` | `Engine/Inc/Render/RenderGraph` |
| RHI / DX12 | `rhi-dx12.md` | `GraphicsContext`, `Engine/*/RHI/DX12` |
| Reflection/codegen | `reflection-codegen.md` | `AiluHeadTool`, reflection macros, generated files |
| Editor/runtime boundary | `editor-runtime-boundary.md` | `EditorApp`, Editor modules, Engine public APIs |

## High-level dependency direction

```text
Editor / Player
      ↓
    Engine
      ↓
Framework / Scene / Assets / Render
      ↓
GraphicsContext
      ↓
RHI / DX12
```

这是设计约束而不是“所有 include 都已完全遵守的形式化模块图”。

原则：

- Engine 不应依赖 Editor。
- Editor 可以通过 Engine 的公开/内部接口驱动 Scene、Asset、Render 等功能。
- Render 上层代码优先依赖 `GraphicsContext`、`GpuResource`、RenderGraph 等抽象，而不是直接散布 DX12 API。
- DX12 backend 实现放在 `RHI/DX12`。
- 长期 subsystem 的 owner 和 shutdown 顺序由 runtime lifecycle 决定。

## Source-of-truth policy

文档分为三类：

### Architecture

`Doc/architecture/`

描述当前长期有效的事实：

- ownership；
- data flow；
- dependency direction；
- threading；
- lifetime；
- extensibility point；
- 当前已知的 transitional/legacy 边界。

### ExecPlan

`Doc/exec-plans/active/`

描述当前任务：

- 为什么做；
- 准备怎么改；
- 当前做到哪里；
- 做过哪些决定；
- 发现了什么；
- 如何验证。

### Historical design/task documents

当前 `Doc/` 根目录仍存在大量历史设计稿和任务包。

这些文档可作为设计背景，但除非已被 architecture 文档明确引用，否则不能自动视为当前实现。

## Reading strategy for agents

不要启动任务就扫描整个仓库和全部 `Doc/`。

推荐顺序：

```text
AGENTS.md
   ↓
architecture/index.md
   ↓
1~2 个相关 architecture 文档
   ↓
对应源码入口
   ↓
active ExecPlan（若存在）
   ↓
实际修改
```

上下文压缩后恢复任务时：

```text
AGENTS.md
→ active ExecPlan
→ 相关 architecture 文档
→ git diff / 当前源码
```

不需要重新读取全部历史聊天。

## Known architectural pressure points

这些是当前实现中需要知道的事实，不表示必须立即重构。

### ResourceMgr is broad

`ResourceMgr` 当前同时涉及：

- asset path/mount；
- asset database；
- import；
- reload；
- file watch integration；
- runtime resource loading；
- sub-assets；
- async/sync resource tasks；
- `AssetRegistry` facade。

新增逻辑前应先判断它是否真的属于 `ResourceMgr`。

### Scene is broad

`Scene` 当前同时涉及：

- entity ownership；
- ECS register；
- hierarchy；
- persistent GUID；
- prefab；
- script/update hooks；
- render transform/bounds；
- GPU scene；
- BVH/BLAS/TLAS data。

不要继续无条件扩大 `Scene`。

### Renderer is broad

`Renderer` 当前同时负责：

- scene extraction；
- material/light preparation；
- view/culling；
- RenderFeature ownership；
- concrete render passes；
- GI/shadow/environment systems；
- RenderGraph recording。

新增可选能力优先寻找 `RenderFeature`/pass registration 边界。

### RenderPipeline contains legacy rendering structures

`RenderPipeline` 仍保留 `RenderObject`、`VisibilityObject`、`ViewEntity` 等旧路径代码。

当前主要 scene preparation/culling 已由 `Renderer` 的 `ScenePrimitive` / `ViewRenderData` 路径承担。

不要将旧路径当作新功能扩展点。
