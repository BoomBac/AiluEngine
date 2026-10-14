# Rendering Architecture

## Purpose

本文档描述 AiluEngine 当前主要 rendering data flow，并明确新旧路径。

### PathTracing denoiser contract

`GIPass` owns an instance-local `PathTracingDenoiserHost`. The independent reflected `EPathTracingDenoiser` setting
defaults to None; missing properties in existing assets retain that constructor default. Backend/mode switches,
history reset, target recreation and shader asset revision changes invalidate its generation. Input/output contain
GPU graph handles, generation, backend identity, dimensions and SPP; no CPU image contract is required.
The None path creates no SDK resources. The optional OIDN CUDA backend initializes on the background task pool,
packs current Reference HDR to shared buffers and publishes a separate double-buffered HDR preview only after
CUDA and DX12 Unpack complete. Unsupported/OFF configurations retain the original source. Debug GI captures
one selected handle for both setup Read and execute Blit;
temporal Debug1-5 keep priority over a backend output. Reference history is never overwritten by this host.
Quality and SPP cadence are reflected settings. Diagnostics are copied under a feature-local mutex for UI readers.
Feature activation is virtual; RayTraceGI sends an atomic activation notification and revision to invalidate work
without mutating the render-owned backend from UI. Sessions retire through the DX12 context before graphics shutdown.
The standalone DX12/OIDN interop and synthetic RenderGraph smoke executables are separate validation paths.

主要入口：

- `Engine/Inc/Render/RenderPipeline.h`
- `Engine/Inc/Render/Renderer.h`
- `Engine/Inc/Render/RenderingData.h`
- `Engine/Inc/Render/Features/RenderFeature.h`
- `Engine/Inc/Render/RenderGraph`
- `Engine/Inc/Render/GraphicsContext.h`

## Current high-level flow

### Raster thin glass

Standard Lit transparent materials with `_Transmission > 0` use straight screen-space background transmission
in `forwardlit.hlsl`. Live Link routes transmissive materials to the transparent queue independently of alpha;
manually authored materials must use the Transparent surface type. Coverage alpha remains independent of transmission.

When a visible transparent material has transmission, `ForwardPass::OnRecordRenderGraph` records a
`ThinGlassSceneColor` HDR snapshot immediately before `TransparentPass`. The graph owns this per-view transient;
the forward pass explicitly reads it and binds `_ThinGlassSceneColor`. This works with TAA disabled
and never reads from its active color render target. The snapshot includes earlier opaque, cloud and Sprite passes.

Transmission reduces the diffuse lobe and composites tinted background throughput with IOR-based dielectric reflection.
IOR=1 produces no dielectric reflection. Ordinary shading retains existing BRDF defaults. Surface fog is applied
without fogging the captured background a second time. Asset previews and the legacy non-RG path retain ordinary shading.

This first model has no physical refraction offset, glass thickness/absorption or ordered multilayer transmission;
all glass samples the same pre-transparent snapshot. Shadow maps still use the existing opaque/alpha-test caster policy.

Postprocess overlays (PickBuffer, selection outline, GizmoPass and EditorDebugPass) explicitly load their existing
final color/depth attachments before drawing. They preserve the scene independently of transient texture load defaults.
When postprocessing produces a larger color output (DLSS SR), `PostprocessOverlayDepth` point-samples the scene depth
into a matching DSV. Depth-tested overlays use `RenderingData::FinalDepthHandle()` and the final color dimensions;
the original scene depth and render viewport remain at the scene input resolution. This avoids DX12 clamping the
overlay viewport to the smaller scene depth attachment.
The overlay depth copy uses its own `postprocess_overlay_depth.hlsl` shader/pass 0, leaving the shared Blit shader
unchanged. Shader pass metadata must be declared inside the `//info begin` / `//info end` block.

`RenderingData` value-initializes its graph handles so the first frame cannot select an unproduced temporal output.
Overlay consumers receive the depth version written by the producer (`GetTexture` after registration), rather than
the creation-time v0 handle, which has no producer. A graph validation failure can skip one-time environment work;
this must be fixed at the invalid access rather than hidden by changing the SkyView update schedule.

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

The Streamline DLSS and optional FidelityFX FSR DX12 backends share one temporal-mode resolver and RenderGraph SR
feature. The resolver selects the effective mode and render size before `PrepareRenderFrame`, so jitter and frame
resources use the selected input dimensions. The pass evaluates SR after color/depth/motion-vector production and
before postprocessing; postprocessing reads the upscaled color handle and derives source dimensions from the graph
texture description. TAA history is invalidated while either SR mode is active. The pass records a spatial upscale
fallback before native SDK evaluation. Scene View's camera settings expose FSR beside DLSS in the upsampling selector;
The camera stores a shared super-resolution mode (Off, DLSS or FSR) and quality (Quality, Balanced, Performance
or Ultra Performance). DLSS maps the shared quality to its supported modes; FSR supports all four qualities. The
selected FSR quality drives the SDK render-resolution query and resulting dispatch dimensions.
FSR SDK dispatch, motion-vector/depth conventions, and DX12 state restoration still require runtime validation.

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

## Directional shadow near-plane coverage

`DrawShadowView` sets `_ZBufferParams.z = 1` in the shadow-only camera buffer for directional views, including
camera cascades and probe orthographic shadows. Other shadow views leave it zero; point shadows retain their
near/far values in `.xy`. `shadow_caster.hlsli` uses the flag to pancake vertex depth onto the light near plane:
`min(z, w)` for reverse Z and `max(z, 0)` otherwise. XY, world position, far clipping, alpha testing and CPU
caster culling remain unchanged. This preserves projected coverage when an otherwise visible caster crosses
the fixed-margin light near plane, without extra scene traversal, draws, resources or shader variants.
It does not include casters entirely outside the CPU shadow frustum.

## Camera matrices and jitter

`Camera` owns the single projection jitter used by the current raster view. It stores both the stable no-jitter projection
and the current render projection. `GetProj()` returns the render projection; `GetProjNoJitter()` returns the stored stable
projection. `GetViewProj()` retains its compatibility meaning and returns the stable current VP. New code that needs an
explicit meaning should use `GetViewProjNoJitter()` or `GetViewProjJittered()`.

The per-camera constant buffer follows the same contract:

```text
_MatrixVP             current jittered render VP
_MatrixIVP            inverse of _MatrixVP
_MatrixVP_NoJitter    current stable VP
_MatrixVP_Pre         previous stable VP
```

Call `Camera::MarkTemporalHistoryReset()` after a camera cut or teleport to make the next render frame use its current stable VP
as the previous VP, preventing a one-frame camera-motion spike.

Camera culling uses the stable no-jitter VP. Camera render-ray corners are derived separately from the jittered VP and feed
the screen-ray constants used by volumetric clouds. Perspective and orthographic camera jitter both store projection-space
NDC displacement in `Camera::_jitter.xy`; `Camera::_jitter.zw` stores the corresponding top-left-origin texture-UV delta.
Perspective editor screen/world conversion uses the stable projection so picking and gizmo interaction do not follow
sub-pixel render jitter. A custom view/projection supplied through `SetViewAndProj()` has zero jitter and identical stable
and render matrices.

`RenderPipeline::Render()` prepares each registered camera through `Camera::PrepareRenderFrame(frame_index)` after waiting
for the previous render worker and before view/culling preparation. This refresh samples projection jitter from the graphics
frame index and updates render VP/ray corners without
rebuilding stable projection, view or culling planes. It is independent of input/controller updates, including idle 2D
views. History advances only at this synchronized boundary, once per supplied render frame index, using a per-camera
last-rendered stable VP snapshot. Controller matrix rebuilds, `LookTo()` and `SetViewAndProj()` do not advance history.
The first rendered view and an explicitly reset view use previous=current; skipped render frames retain the last rendered
VP. Cubemap face cameras are prepared at their synchronized render entry. Custom view/projection cameras retain zero jitter.
Orthographic Camera width defaults to 10 world units; AA continues to default to None.

When `VolumetricClouds` is registered as a renderer feature, its tiled temporal pass runs at event 455, after
`MotionVector` (450) and before Sprite (460) / forward transparency (475). `CloudMain` uses Camera's jittered render-ray
corners. Cloud history reprojection uses no-jitter MotionVector data plus the previous-minus-current Camera UV jitter;
invalid history is seeded by a full-resolution Cloud dispatch before tiled reuse resumes. Resize, camera/projection
discontinuity, render gaps, camera changes and Cloud parameter changes invalidate this history. The feature is currently
not added to `Renderer::_features` by default, so this temporal Cloud path remains opt-in.
Cloud explicitly reads the environment system's `_rg_handles._sky_view_lut` at record time; no sky-LUT global slot is
required. Missing inputs or skipped history-producing work prevent composition and history commit.

## TAA blended responsiveness

The native RenderGraph TAA feature records `TAAOpaqueSnapshot` at event456, after MotionVector450 / Cloud455 and before Sprite460
and forward transparency475. The pass captures its source handle at record time and copies the pre-blend HDR color into
a transient RG texture. `TemporalAA` compares the composed input against that snapshot; a component difference above
0.001 HDR marks a responsive pixel. This is color-affecting coverage, rather than a material-ID or geometric coverage mask.

Each camera's TAA history owns two R8_UNORM responsive textures alongside its two color textures. The resolve writes
every current mask pixel, explicitly reads the previous mask/snapshot in RG, and rotates both histories after dispatch.
Current responsive coverage and the expanded, reprojected previous coverage return raw current color, protecting both
blended content and newly uncovered background. Alpha-test deferred coverage remains in core TAA. The global history
factor is unchanged. The legacy non-RG resolve binds the resources but disables this snapshot-based policy.

Core clipping retains RGB luminance/coverage behavior and independently clamps history Co/Cg to the current 3x3 chroma
envelope. This rejects stale colors without coupling tiny R11G11B10 chroma quantization differences to luminance rejection.
The policy does not supply transparent object velocity; changes below the mask threshold still use ordinary TAA.

`billboard.hlsl` intentionally removes projection jitter for stable blended rasterization. Perspective matrices remove
the shader-side `[0][2]`/`[1][2]` offsets; orthographic matrices remove `[0][3]`/`[1][3]`. Actual Editor GizmoPass icons
write final color after postprocess; pre-TAA billboard behavior is a separate supported test path.

## RayTraceGI camera temporal contract

RayTraceGI camera-change detection uses `Camera::GetViewProjNoJitter()`, so Halton camera jitter does not reset temporal
history or reference accumulation. Its current GBuffer/depth reconstruction uses the jittered inverse VP from the current
camera view. The DXR path builds its camera constants from that same view and keeps previous VP stable/no-jitter.

Realtime RTGI reprojection combines no-jitter MotionVector data with Camera's top-left-origin UV jitter delta:
`history_uv = current_uv - motion + previous_jitter_uv - current_jitter_uv`. Reservoir validation, primary denoise taps
and clamp-neighbor centers derive from this corrected history location. Reference accumulation averages independent samples at a fixed output pixel, including sky pixels;
raster depth/normal rejection and jitter reprojection do not apply. Real camera movement resets its progressive mean. Camera identity, cuts, resize/target recreation, history signatures and
`Camera::MarkTemporalHistoryReset()` invalidate RTGI history. Camera exposes a monotonically increasing reset revision so
TAA and RTGI independently observe explicit resets even after PrepareRenderFrame consumes the pending Camera flag.

RTGI's GIPass runs at kAfterMotionVector (455), producing opaque scene color before TAAOpaqueSnapshot456 and Sprite460.
Persistent raw/history/debug outputs are RenderTexture resources, and reservoir/surface buffers use CreateSync so resize
resources exist before their first dispatch. History validity, previous UV jitter and ping-pong rotation commit after the
actual primary/denoise dispatch; skipped primary work cannot commit denoise history. These are single-camera resources;
the M7 fixture does not establish multi-camera isolation or interactive acceptance.

## Frame resources

### Path tracer emissive hits

Blender Live Link sends optional `transmission` and `ior` parameters for the primary Glass/Refraction/Principled
closure, including materials converted by native channel baking. Old messages default to transmission 0 and IOR 1.5.
Layered extra reflection lobes and spatially varying transmission/IOR are explicit conversion approximations.
Path material glass classification uses the metallic value after texture sampling, rather than a texture multiplier.
Transient `MaterialData` reuses its reserved opacity-texture word for scalar base alpha, retaining the 92-byte stride.
Transparent surface alpha is stochastic null coverage, separate from physical dielectric transmission. Null crossings
preserve previous BSDF MIS state and do not consume BSDF depth; total traversal is bounded by max depth plus 17 segments.
This coverage support applies to path continuation. Shadow-ray coverage and full Blender closure matching are not implied.

SceneRayTracingProxy extracts mesh lights using Renderer ScenePrimitive indices. Its `_PathLightLookup` uint buffer
contains a four-word header (primitive count, directional count, analytic count, primitive-table start), original
light-index lists for directional and analytic lights, per-primitive triangle-table offsets, and triangle-to-light
indices. Non-emissive primitives/filtered triangles use UINT32_MAX; instance-specific tables distinguish shared meshes.
The proxy updates the lookup with the unified lights; RayTraceGI registers its RenderGraph read and binds it for
Compute PrimaryRay and DXR. This is transient runtime data and does not change serialized/reflected asset layouts.

DXR path rays reuse their nearest scene hit for emissive triangles, then read the corresponding light and its actual
uniform/alias selection PMF for emission MIS. Emissive surfaces retain BSDF scattering, and terminal-depth surface
emission is evaluated. Only finite analytic lights require separate software intersection; miss rays iterate the
directional index list. Compute PrimaryRay retains the full finite-light intersection fallback until its software BVH
has equivalent hit coverage. The full unified-light list and alias NEE sampling remain available to other GI consumers.

Path tracer bindless mesh and texture reads use `NonUniformResourceIndex`: hit instances, material texture handles
and sampled-light resources can differ between rays in the same wave. This applies to both DXR and Compute tracing.

Renderer owns the material-index namespace in `PrimitiveData` and the matching current-frame material buffer.
Compute and DXR path tracing consume that same buffer; DXR declares its RenderGraph read explicitly.
SceneRayTracingProxy owns acceleration structures, geometry resources and light data, but does not rewrite material
indices or maintain a second material table. Common material upload includes coverage alpha and transparent flags.

Continuous path BSDFs additionally sample each directional light directly, using the small directional lookup list.
The directional NEE, unified-light NEE and BSDF miss techniques use a three-way power heuristic with their actual
PDFs. This prevents the sun from receiving rare, large NEE weights when the unified list contains many mesh emitters.
Delta paths retain their ordinary emission-hit treatment. Finite light sampling and its alias table stay unchanged.

### TAA accumulation and presentation coordinates

TAA color histories remain on each frame's jittered raster grid. History reprojection uses the stable MotionVector
plus `previous_jitter_uv - current_jitter_uv`. The `TemporalAA` pass publishes this accumulation as
`_taa_history_color`; it is not a stable screen image. `TemporalAAStabilize` reads the completed accumulation and
samples `stable_uv + current_jitter_uv` with the existing bicubic filter into a separate transient UAV. Its output is
`_taa_color`, consumed by PostprocessStack/Bloom/ToneMapping. The presentation image never feeds back into history.
Policy A current and reprojected previous responsive coverage, plus the stabilization filter footprint around current
coverage, retain current pixels, including billboards intentionally rasterized without jitter. Previous coverage is read
only with valid history. The legacy path uses a feature-owned, per-camera presentation
texture allocated only when that path executes; dimension/format changes release it with the histories.
Core/history regression captures use `_taa_history_color`; screen stability acceptance uses `_taa_color` and final
ToneMapping output. Passing the history reprojection oracle alone does not prove stable presentation.

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

Standard Lit exposes independent `_RoughnessTex` and `_MetallicTex` scalar maps. Both sample red in linear data space;
the resulting roughness/metallic equals the sample multiplied by `_RoughnessValue`/`_MetallicValue`, respectively.
Without a map the scalar stands alone. Forward/deferred and shared Compute/DXR material evaluation use this contract;
GI metallic evaluation uses the same factor. Sampler-mask bits are 8 for roughness and 64 for metallic.
Inspector rows derive from shader metadata, and property-ID texture assignment maintains these flags and asset GUIDs.
Transient CPU/HLSL `MaterialData` stores two independent bindless handles and has a 128-byte stride.
Standard Lit volume attenuation uses only `_AttenuationColor` and `_AttenuationDistance`; the legacy scalar
`_Absorption` property and loader/setter compatibility have been removed. Cloud/fog absorption is independent.

Blender Live Link exports/imports `roughness_path` and `metallic_path` separately, including baked material graphs.
Connected scalar images are converted to Non-Color red-channel maps and use factor 1, since Blender replaces socket
defaults with linked values. Baked maps also use factor 1. Old externally stored packed maps require re-export or
reassignment to the separate slots; bundled empty packed slots have been migrated.

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

## MotionVector deformation and variant contracts

MotionVector UV0 is TEXCOORD0; previous local positions are TEXCOORD1 (float3). Its alpha clip uses material alpha
multiplied by sampled albedo alpha, matching GBuffer. The draw selects CPU_DEFORM from the vertex layout without
changing shared Material keywords. The legacy object-motion attachment loads existing camera motion and scene depth.

SkinningSystem owns current/previous position buffers per mesh and entity. Changed poses rotate outputs once per frame;
stationary, initial and discontinuous submissions bind current positions as previous. CPU and Compute paths share this
history contract. GPU vertex-stream changes invalidate view versions and refresh native DX12 views before state/binding.

Material resource bindings and frame constant blocks resolve against the actual draw variant. Global slot caches rebuild
on variant/layout changes; constant blocks include variant/layout versions and canonical material values.
Standard Lit's uploaded `_SamplerMask` comes from the current texture-use state. Its uploaded `_MaterialID` keeps the
existing mapping (checker = 2, otherwise = 0), after canonical/serialized values are copied into the variant layout.

## Imported camera view basis and projection

Camera defaults remain local +Z forward/+Y up with the ordinary symmetric projection. Optional local view axes,
projection X reversal, NDC projection offset and aspect lock support imported camera conventions without changing
entity transforms or child hierarchy. Scene camera documents persist these settings; old documents retain defaults.
The historical FovH API/_fov_h field is consumed as a vertical field of view by BuildPerspectiveFovLHMatrix.

RecalculateMatrix applies the configured local axes to the camera rotation, then builds the ordinary view and
projection with optional X reversal and off-center offsets. Culling planes are oriented away from the frustum
interior, including reflected screen axes. Screen/world conversion uses the configured projection for imported
orthographic cameras. OutputSize and SetPixelSize preserve source aspect when locked. Camera updates use the
TransformSystem's cached world rotation, avoiding quaternion extraction from a scaled world matrix.

The Editor preview fits a locked camera's source aspect inside its existing preview bounds. This changes preview
texture/display dimensions, not the source projection. Existing native camera defaults and preview ratio remain unchanged.

Camera render scale multiplies the camera output dimensions for internal GBuffer, depth, and color resources. The final presentation blit restores output dimensions using the camera's bilinear or point upsampling choice; a scale of 1 preserves the original dimensions.

## Path tracer GGX sampling

Compute and DXR share `ray_trace/sampling.hlsli`. `SAMPLE_BRDF_VNDF_GGX` defaults to 1 for Heitz 2018
visible-normal sampling; setting it to 0 before inclusion selects the legacy NDF control. The numeric switch
controls opaque specular and rough dielectric sampling together with their evaluation/MIS PDFs.
VNDF reflection has direction PDF `D(h) * G1(wo) / (4 * abs(n.wo))`. Dielectric transmission uses the same
conditional microfacet-normal PDF followed by the existing refraction Jacobian and Fresnel lobe probability.
Normals face the incident path direction at both front-face and back-face hits. Rejected directions terminate
the sample without resampling or renormalization. BSDF values, discrete mirror events and random streams retain
their existing contracts; switching the sampler requires shader recompilation and fresh reference accumulation.

Compute and DXR preserve authored corner normals, including Blender Live Link smooth/split normals. Before BSDF
evaluation and sampling, the shared path core falls back to the oriented geometric normal when the interpolated
shading normal faces away from the incoming ray. This prevents a front-facing smooth surface from returning zero
solely because its shading normal crosses the view hemisphere at grazing angles; ordinary smooth shading is retained.

Texture sampler ownership, material overrides and per-view mip bias are documented in `texture-sampling.md`.
