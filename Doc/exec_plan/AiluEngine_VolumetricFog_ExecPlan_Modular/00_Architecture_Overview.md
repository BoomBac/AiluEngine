# AiluEngine Volumetric Fog — Architecture Overview

> Stable context file. Read this file first when starting or resuming the task.
>
> Detailed implementation steps live in `01_Milestones.md`.
> Runtime execution state lives in `02_Progress.md`.

# Purpose

在现有 AiluEngine Froxel Volumetric Fog 架构基础上，修复当前实现中的几何积分、Z 采样、随机抖动、
相函数方向、历史重投影以及无阴影路径等正确性问题，并逐步提升介质表现能力与稳定性。

本计划不重写体积雾系统，而是保持当前：

```text
LightInjection
    -> Temporal Reprojection
    -> LightIntegration
    -> Composite
```

主流程，将当前“基础均匀体积光”逐步提升为稳定、可扩展的实时体积雾实现。

执行原则：

1. Correctness 优先于 Feature。
2. 每个 Milestone 必须独立验证，通过后才能进入下一阶段。
3. 不允许同时修改多个尚未验证的数学模型。
4. 优先保持现有 RenderGraph / RenderFeature 接口不变。
5. 每个阶段必须保留清晰的 A/B 验证方式。
6. 不使用调大 density/intensity 的方式掩盖数学错误。

# Scope

覆盖：

- Froxel ray distance / slice thickness 修正。
- Accumulation texture 深度坐标语义修正。
- LightInjection stochastic jitter 范围修正。
- Henyey-Greenstein phase function 方向约定统一。
- 无主光 / 无 ShadowMap 时的安全 fallback。
- Temporal reprojection history validity。
- Per-camera volumetric history。
- Uniform density 扩展为 world-space Height Fog。
- 使用 `_VolumetricLight.a` 存储 extinction/density。
- Environment low-frequency in-scattering。
- Transparent object volumetric fog sampling。
- Froxel resolution 配置化。
- 为 Local Fog Volume / Point / Spot volumetric light 留扩展点。

推荐交付目标为：

```text
Milestone 0 ~ 4
```

即：

```text
Correctness
+ Reliable Temporal
+ Height Fog
+ Environment Scattering
```

# Non-goals

本轮不包含：

- 不重构 RenderFeature 系统。
- 不重写 RenderGraph。
- 不实现完整参与介质材质系统。
- 不实现 multiple scattering。
- 不实现云雾统一系统。
- 不实现 fluid / smoke simulation。
- 不实现 volumetric path tracing。
- 不立即实现 clustered volumetric lighting。
- 不立即支持任意数量 Point / Spot Light。
- 不修改 TAA 主流程。
- 不改为全分辨率 2D ray march。
- 不在 Correctness 阶段增加大量艺术参数。
- 不在验证前恢复旧 MaxZ optimization。

# Architecture Constraints

## Existing Pipeline

保持：

```text
VolumetricFog_LightInject
    |
    v
VolumetricLight 3D Texture
    |
    v
VolumetricFog_LightIntegration
    |
    v
VolumetricFogAccum 3D Texture
    |
    v
VolumetricFogComposite
```

Correctness 阶段不新增大型 Pass。

## Froxel Projection

继续使用：

```cpp
cam->GetProjNoJitter()
```

TAA sub-pixel jitter 不参与 froxel world-space layout。

体积雾 stochastic jitter 仅在 LightInjection 内部使用，并限制在当前 slice 附近。

## Z Distribution

继续使用 logarithmic slicing：

```text
z(t) = near * pow(far / near, t)
```

不改为 linear Z。

## Resource Model

Milestone 0~1 尽量保持现有：

```text
_volumetric_light_a
_volumetric_light_b
_accum_texture
```

资源组织。

只有进入 Temporal per-camera 阶段后才允许调整 history ownership。

## Texture Format

暂时保持：

```text
R16G16B16A16_FLOAT
```

格式压缩和带宽优化放到后续 profiling。

## Coding Style

遵循项目现有约定：

- 普通变量：`lower_case_with_underscore`
- 类/结构体成员：`_lower_case_with_underscore`
- 静态变量：`s_...`
- 常量：`kCamelCase`
- 类/结构体/函数：CamelCase
- 行宽尽量不超过 120。

## Milestone Isolation

每个 Milestone 完成后至少执行：

```text
Build
-> Static Camera
-> Moving Camera
-> Shadow Boundary
-> Sky
-> Near Geometry
-> Wide FOV
```

未通过时不得继续叠加新 feature。

# Baseline

源码基线：

```text
AiluEngine_Source(20261001-042419)
```

当前主要参数：

```cpp
Vector3UInt _voxel_num = Vector3UInt(170, 90, 96);

f32 _fog_density = 1.0f;
f32 _fog_distance = 50.0f;
bool _temporal_reprojection = false;
f32 _blend_factor = 0.5f;
f32 _g = 0.0f;
f32 _intensity = 1.0f;
```

当前流程：

```text
170 x 90 x 96 Froxel

LightInjection
    Directional Light
    Shadow
    HG Phase
    Blue Noise Jitter
    Optional Temporal

LightIntegration
    Uniform Density
    Exponential Extinction

Composite
    Scene * Transmittance + Scattering
```

# Known Baseline Problems

## B-01 Ray Thickness

当前 integration 使用：

```hlsl
slice_z_far - slice_z_near
```

这是 view-space Z 差，而不是 ray 实际穿过介质的长度。

非屏幕中心 ray 应近似：

```text
ray_distance = view_z_distance / abs(view_ray.z)
```

否则 Wide FOV / 屏幕边缘 fog 偏弱。

## B-02 Accum Z Half-Slice Offset

Integration 在 voxel `z` 中保存：

```text
积分到 slice z 的 far boundary
```

而 Texture3D texel center 为：

```text
(z + 0.5) / N
```

Composite 当前直接使用 boundary fraction 采样，会产生约半 slice 错位。

Near plane 附近尤其明显。

## B-03 Jitter Range Too Large

当前类似：

```hlsl
float jitter = (blue.r - 0.5f) * 4.0f;
```

范围达到：

```text
[-2, +2] slices
```

这会破坏 shadow coherence 与 temporal correspondence。

目标：默认限制到：

```text
[-0.5, +0.5] slice
```

以内。

## B-04 HG Phase Direction Convention

当前：

```hlsl
cos_theta = dot(view_dir, light_dir);
```

但：

```text
view_dir = camera -> sample
```

且 `_MainlightWorldPosition.xyz` 的实际语义需与 surface lighting 统一。

必须明确：

```text
light_travel_dir
sample_to_camera_dir
```

并保证：

```text
g > 0 -> forward scattering
g = 0 -> isotropic
g < 0 -> backward scattering
```

## B-05 Weak Temporal Rejection

History validity 当前主要依赖：

```text
prev_uvw in [0, 1]
```

缺少：

- camera cut
- projection change
- behind-camera test
- luminance rejection
- neighborhood clamp
- history confidence

## B-06 Global History State

当前类似：

```text
_matrix_prev_v
_matrix_prev_p
_history_near
_history_far
_volumetric_light_a
_volumetric_light_b
```

是 Feature 全局状态。

多 Camera / Cubemap / Editor Camera 可能串 history。

## B-07 ShadowMap Hard Dependency

当前无 ShadowMap 时可能直接跳过 volumetric light injection。

正确行为：

```text
has shadow map -> sample shadow
no shadow map  -> shadow = 1
```

Shadow 不应成为整个 fog feature 的存在条件。

## B-08 Uniform Medium Only

当前：

```hlsl
float slice_density = _FogDensity;
```

整个世界 density 恒定，视觉上容易形成“均匀牛奶雾”。

## B-09 VolumetricLight Alpha Is Available

当前 alpha 写入 `slice_far`，但 integration 又根据 slice index 重新计算深度。

因此 alpha 可用于：

```text
medium extinction
```

无需增加新 3D texture。

## B-10 Transparent Not Integrated

Fullscreen fog composite 发生在透明物体之前/附近，但透明 shader 本身没有按 fragment 深度采样 volumetric accumulation。

长期目标应是：

```text
transparent_color * T + S
```

而不是简单把 fullscreen fog 移到 transparent 之后。

## B-11 Fixed Froxel Resolution

当前固定：

```text
170 x 90 x 96
```

对 viewport / aspect ratio / quality level 不够通用。

# Target Outcome

## Correctness

- Wide FOV 下边缘与中心介质厚度一致。
- Near plane 不提前出现第一层 fog。
- Fog depth 与 opaque depth 对齐。
- Jitter 不跨越多个 slice。
- `_g > 0` 的 forward scattering 语义明确。
- 无 ShadowMap 时 fog 正常。
- 无方向光时无 NaN / Inf。

## Stability

- 静止 Camera 下体积阴影快速稳定。
- 移动 Camera 无明显长时间 ghosting。
- Camera cut 自动失效 history。
- 多 Camera 不互相污染 history。

## Visual Quality

- 支持 Height Fog。
- 阴影区存在低频环境 in-scattering。
- Fog density 可随高度变化。
- Transparent 与 volumetric fog 正确融合。
- 为 Local Volume / local volumetric lights 留扩展点。

## Maintainability

Shader 中最终应明确区分：

```text
Medium
Lighting Source
Phase
Shadow
Temporal
Integration
Composite
```

# Relevant Files

Primary:

```text
Engine/Src/Render/Features/VolumetricFog.cpp
Engine/Inc/Render/Features/VolumetricFog.h

Engine/Res/Shaders/hlsl/Compute/volumetric_light.hlsl
Engine/Res/Shaders/hlsl/volumetric_fog_composite.hlsl
Engine/Res/Shaders/hlsl/froxel_common.hlsli
```

Secondary:

```text
Engine/Src/Render/Renderer.cpp
Engine/Inc/Render/Renderer.h
Engine/Inc/Render/Features/RenderFeature.h

Engine/Res/Shaders/hlsl/shadow.hlsli
Engine/Res/Shaders/hlsl/common.hlsli
Engine/Res/Shaders/hlsl/cs_common.hlsli
```

Optional references:

```text
Engine/Src/Render/Features/VolumetricClouds.cpp
Engine/Inc/Render/Features/VolumetricClouds.h

Engine/Res/Shaders/hlsl/Compute/volumetric_cloud.hlsl
Engine/Res/Shaders/hlsl/Compute/volumetric_noise_generator.hlsl
```

VolumetricCloud 可参考 noise / temporal 思路，但不要强耦合两套 feature。

# Decision Log

## D-001 Keep Existing Froxel Architecture

继续使用 froxel volume + integration + composite。

当前问题主要属于数学正确性、history stability 和 medium model，不需要推倒重写。

## D-002 Correctness Before Feature

Height Fog / Noise / Local Volume 必须在 Correctness 完成后进入。

## D-003 Keep Non-Jittered Projection

Froxel layout 继续使用 `GetProjNoJitter()`。

## D-004 Jitter Inside One Slice

Injection jitter 默认限制到 ±0.5 slice 内。

## D-005 Reuse VolumetricLight Alpha

后续 `_VolumetricLight.a` 存储 extinction，而不是冗余的 `slice_far`。

## D-006 Per-Camera Temporal State

Temporal history 不继续使用 Feature 全局 camera state。

## D-007 Shadow Is Optional

ShadowMap 缺失时继续计算 unshadowed volumetric scattering。

## D-008 Environment Before Local Lights

先实现低频环境 in-scattering，再考虑 Point / Spot volumetric lights。

# Surprises & Discoveries

## S-001 Existing GetViewRay()

`volumetric_light.hlsl` 已经存在 `GetViewRay()`，ray-length correction 可低风险接入。

## S-002 Alpha Channel Is Effectively Free

`_VolumetricLight.a` 当前信息可由 slice index 重建，因此适合转存 extinction。

## S-003 MaxZ Experiment Already Existed

源码有 MaxZ / depth-aware integration 的旧注释代码。

当前先保持 full-volume accumulation + composite depth lookup，不急于恢复旧优化。

## S-004 Temporal Defaults Off

当前 `_temporal_reprojection = false`，适合 Correctness 阶段排查，不会被 history 掩盖。

## S-005 Z Count Has Two Sources

C++：

```cpp
Vector3UInt(170, 90, 96)
```

Shader：

```hlsl
#define VOXEL_SLICE_COUNT 96
```

未来动态质量等级必须消除这种隐式双来源。

# Outcomes

## Delivery A — Correct Volumetric Fog

包含：

```text
Milestone 0
Milestone 1
```

目标：修复数学正确性，不增加大规模架构复杂度。

## Delivery B — Production-Usable Fog

包含：

```text
Milestone 0 ~ 4
```

目标：

```text
Correctness
+ Reliable Temporal
+ Height Fog
+ Environment Scattering
```

这是本轮推荐交付。

## Delivery C — Full Scene Integration

再包含：

```text
Milestone 5
Milestone 6
```

目标：

```text
Transparent Integration
+ Dynamic Froxel Resolution
```

Local Fog Volume / Point / Spot Light 留到后续独立任务。
