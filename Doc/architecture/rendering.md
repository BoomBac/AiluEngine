# Rendering Architecture

## Purpose

本文档描述 AiluEngine 当前主要 rendering data flow，并明确新旧路径。

主要入口：

- `Engine/Inc/Render/RenderPipeline.h`
- `Engine/Inc/Render/Renderer.h`
- `Engine/Inc/Render/RenderingData.h`
- `Engine/Inc/Render/Features/RenderFeature.h`
- `Engine/Inc/Render/RenderGraph`
- `Engine/Inc/Render/GraphicsContext.h`

## Current high-level flow

当前主要路径可以概括为：

```text
Application
  ↓
RenderPipeline
  ↓
Renderer
  ↓
Scene extraction / material / light / view preparation
  ↓
ScenePrimitive / PrimitiveData / ViewRenderData
  ↓
RenderFeature / RenderPass
  ↓
RenderGraph
  ↓
GraphicsContext
  ↓
RHI / DX12
```

`RenderPipeline` 负责 frame/camera-level orchestration。

`Renderer` 负责单个 renderer 的 scene/view preparation 和 pass/feature orchestration。

RenderGraph 负责 pass/resource dependency、资源访问和执行计划。

GraphicsContext/RHI 负责 GPU execution。

## RenderPipeline

当前 `RenderPipeline` 负责：

- renderer ownership；
- camera list；
- frame resources；
- preview camera；
- per-frame setup；
- UI record coordination；
- render thread frame pending state；
- invoking `Renderer::PrepareFrameViews`；
- invoking `Renderer::Render`。

### Legacy/transitional structures

`RenderPipeline` 仍保留：

```text
RenderObject
VisibilityObject
RenderNode
ViewEntity
FramePacket camera data
_render_objs
_visiblity_objs
CollectViews
CullView
UpdateRenderObject
UpdateVisibilityObject
ProcessPendingRenderObjects
```

但当前 `RenderPipeline::Setup()` 中旧的 collect/update job 路径已被注释掉。

当前正常 frame path 会调用：

```text
Renderer::PrepareFrameViews
Renderer::Render
```

因此：

**不要基于上述旧 RenderPipeline scene representation 开发新的 rendering feature。**

它们应视为 legacy/transitional code，除非任务明确是删除、迁移或兼容。

## Renderer

当前 `Renderer` 负责较多职责：

### Scene extraction

包括：

```text
BuildScenePrimitives
PrepareScene
PrepareSceneRenderData
PrepareMaterial
PrepareLight
```

核心 CPU-side scene representation 包括：

```text
ScenePrimitive
PrimitiveData
PrimitiveGIData
SceneRenderData
ViewRenderData
```

`_scene_primitives` 已被多处实际消费，包括 Editor picking、DDGI、SurfelGI 和 ray-tracing scene proxy。

这是当前应优先扩展的 scene rendering representation。

### View preparation and culling

Renderer 负责：

```text
PrepareViews
PrepareFrameViews
DispatchViewCulls
Cull
shadow set culling
```

新的 view/culling 功能优先接入这条路径，而不是旧 `ViewEntity`。

### Systems and passes

Renderer 当前直接拥有：

```text
EnvironmentSystem
StaticDiffuseProbeSystem
ShadowSystem
```

以及大量 concrete pass / feature。

这说明 Renderer 目前仍是较重的 orchestrator。

新增可选功能时优先考虑：

```text
RenderFeature
→ one or more RenderPass
→ RenderGraph record
```

不要无必要继续扩张 `Renderer` 的具体 feature 成员列表。

## RenderFeature

`RenderFeature` 是当前较合理的可选渲染扩展点。

适合：

- SSAO；
- TAA；
- DDGI；
- Surfel GI；
- terrain；
- ray tracing feature；
- sprite rendering 等。

未来 plugin system 若允许渲染插件接入，优先考虑注册/注销 RenderFeature 或更高层 factory，而不是 plugin 直接修改 Renderer 内部容器。

## RenderPass

RenderPass 表达渲染工作单元。

新的 pass 应尽量：

- 显式声明输入/输出；
- 通过 RenderGraph 获取资源；
- 避免隐藏的跨 pass mutable state；
- 只在必要时使用全局 resource binding；
- 保持 record 与 lifetime 可追踪。

## RenderingData

### Volumetric fog composition

`VolumetricFog` owns the injection/integration pass and a separate `VolumetricFog_Composite` pass.
Injection/integration runs after deferred lighting, before sky/composition, and publishes the written RenderGraph handle.
The accumulation texture stores integrated scattering in RGB and transmittance in alpha. Composition runs after sky
and before transparency, samples the current view depth (sky uses the full volume), and blends `scattering +
transmittance * destination`. Deferred lighting does not sample fog. Transparent material fog is not implemented by
this full-screen pass; it requires evaluation at each transparent surface's own depth.

Fog extinction is `0.01 * _fog_density` per meter. Injected lighting is treated as a unit-albedo source function;
integration multiplies it by slice opacity, without dividing by density. A small-optical-depth series avoids loss of
scattering precision near zero density. `_fog_distance` (default 50 meters) bounds the froxel domain independently of
the camera far plane. Generation and composition share the same clamped domain; distant geometry and sky sample the
last slice. A changed domain near/far rejects temporal injection history for the next dispatch.

`RenderingData` 是 pass/feature 与 renderer preparation 之间的重要数据桥梁。

新增共享 per-frame rendering data 时先判断：

```text
Is it scene-level?
Is it view-level?
Is it feature-local?
Is it GPU resource?
```

不要把所有数据都塞进一个全局 RenderingData。

## Frame resources

Renderer/RenderPipeline 使用 frame-slot/frame-resource 结构。

任何 per-frame GPU/CPU 临时对象都必须考虑：

- frames in flight；
- previous/current frame；
- render thread；
- GPU completion。

不能因为 CPU 已进入下一帧就复用 GPU 仍在使用的数据。

## Editor rendering

Editor picking 等 pass 会消费同一套 `RenderingData` / `ScenePrimitive` 数据。

Engine rendering core 不应依赖 Editor pass。

Editor-specific feature/pass 应位于 Editor，并通过 Engine 提供的扩展点接入。

## Current pressure points

### Renderer hard-coded feature ownership

Renderer 当前仍硬编码大量 feature pointer。

长期可以进一步 registry/factory 化，但普通功能任务不要顺手全面重构。

### Duplicate historical rendering representation

RenderPipeline legacy `RenderObject/ViewEntity` 与当前 Renderer scene/view representation 共存。

新代码必须选择当前 Renderer 路径。

### Scene/render coupling

Scene 中仍存在 GPU scene/BVH/BLAS/TLAS 数据。

新 renderer-only cache 应谨慎决定 ownership，不继续无条件放入 Scene。

## Change checklist

修改 rendering 时检查：

- current vs legacy path；
- ScenePrimitive/PrimitiveData consistency；
- view culling；
- material revision/cache；
- feature lifetime；
- RenderGraph resource access；
- frame resource lifetime；
- main/render thread；
- Editor picking/debug paths；
- ray tracing consumers；
- visual validation。
