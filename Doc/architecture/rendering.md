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

当前 `VolumetricFog` temporal light history 按 `Camera::HashCode()` 保存在 feature 实例内，每个常规相机有独立
的双缓冲 3D light texture 和上一帧相机参数。相机切换、cut、投影/深度范围变化及 temporal 重新启用会使 history
失效。Cubemap face 使用独立 scratch texture，并明确关闭 temporal reuse。

Froxel XY dimensions are derived from the current renderer viewport using the feature's configurable pixel tile size;
Z slicing uses the runtime depth of the froxel textures. A viewport or quality layout change recreates the light,
accumulation, and per-camera history textures, which resets temporal history.

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
transmittance * destination`. Deferred lighting does not sample fog. Transparent materials sample the same
accumulation texture at each fragment's screen UV and positive view depth. The shared
`volumetric_fog_common.hlsli` helper uses the camera near plane and fog far plane to map that depth to the froxel
boundary, matching opaque composition. Straight-alpha shaders apply fog to straight RGB before blending;
premultiplied-alpha shaders scale scattering by layer opacity. The RenderGraph imports the persistent fog
accumulation texture and declares reads for the transparent geometry and sprite passes. The opaque/sky full-screen
composite remains before transparency, so each layer is fogged once at its own depth and is not included in a later
full-screen fog composite.

Fog extinction is evaluated per froxel as
`0.01 * _fog_density * exp(clamp(-(world_y - _fog_height) * _height_falloff, -10, 10))` per meter and stored in
`_VolumetricLight.a`. `_height_falloff = 0` gives uniform fog. Injected lighting is treated as a unit-albedo source
function; integration multiplies it by slice opacity, without dividing by extinction. A small-optical-depth series
avoids loss of scattering precision near zero extinction. `_fog_distance` (default 50 meters) bounds the froxel domain
independently of the camera far plane. Generation and composition share the same clamped domain; distant geometry and
sky sample the last slice. A changed domain near/far rejects temporal injection history for the next dispatch.
The feature's `_debug_extinction` option visualizes the stored alpha through the existing voxel debug draw path.

Light injection adds low-frequency environment in-scattering from the environment diffuse SH L0 coefficient to
the directional-light source term. It uses the environment IBL enable/intensity and the independent
`_ambient_intensity` feature property. The environment source is isotropic and enters before froxel extinction and
integration, so `_ambient_intensity = 0` restores the directional-only source path.

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

## Volumetric fog and punctual lighting

`VolumetricFog` owns froxel textures, structured local-volume upload and per-camera lighting history.
`LocalFogVolumeComponent` stays scene-owned and uses `TransformComponent::GetRenderWorldMatrix()`.
The first 8 valid enabled Box/Sphere volumes contribute additive extinction and RGB scattering coefficients.
Box coordinates are [-.5,.5], Sphere radius is 1, and edge fade is in normalized local coordinates.
Zero-scale volumes are skipped. Global height fog and local coefficients are evaluated once per froxel.

`VolumetricFog_LightInject` writes RGBA16F medium (RGB sigma_s, A sigma_t) and lighting
(RGB albedo-weighted source function, A current extinction). Local dispatches reuse Renderer per-scene Point/Spot
arrays and shadow textures, changing only lighting RGB. Bounds use a conservative position/range sphere,
projected view AABB, logarithmic depth and jitter guard slices. Near intersections cover full XY.
The default budget is 8 local lights within the existing Renderer limit of 4 Point + 4 Spot.
Punctual fog has an independent `_local_light_intensity` gain (default 0.25), in addition to the existing
lighting intensity. This does not change surface attenuation, directional/environment lighting or extinction.
`_local_light_supersampling` defaults to fixed 2 x 2 x 2 stratified spatial quadrature within each froxel;
depth samples are uniform in linear view depth. It avoids relying on temporal history to integrate narrow
Spot cones near the source. Disable it for the original single jittered sample reference/cheaper mode.
The default depth resolution is 192 slices, with an unchanged 8-pixel XY tile; both remain authorable.
Each light has a dispatch snapshot and an explicit lighting UAV barrier; medium writes also have a UAV barrier.
A full-volume reference path remains available through `_bounded_local_lights=false`.

Temporal filtering runs after local-light accumulation. Medium changes, light-data changes, quality/configuration
changes and camera cuts invalidate history; extinction is always current-frame data. Integration reads lighting
as a UAV and writes S/T accumulation; composition stays after sky and before transparency.
Point shadow sampling separates the light slot (bias data) from the cube-array shadow slot.
Default compute cube-array bindings expose the full main SRV. Surface shadow entry points keep their signature.

Debug authoring provides medium selection/extinction, transformed volume bounds and per-light froxel bounds.
`_jitter_enabled=false` is a deterministic validation setting; production defaults to enabled.
`VolumetricFogSmoke` verifies conservative jittered bounds, optional JSON compatibility, disabled entity cloning,
prefab property overrides/revert and native volume DDS capture. GPU timings include `VolumetricFog_LocalLights` separately from total injection.
