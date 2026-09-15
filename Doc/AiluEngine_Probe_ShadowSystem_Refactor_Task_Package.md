# AiluEngine Reflection Probe / Static Diffuse Probe 阴影与递归 GI 修复任务包

> 基于工程快照：`Engine(20260915-131907).zip`
>
> 目标读者：轻量代码模型。本文刻意写得较细，要求优先按步骤实施，不要自行扩大范围。
>
> 主要目标：
>
> 1. Probe Capture 时禁止旧 Reflection Probe / Static Diffuse Probe 继续参与光照，彻底切断递归反馈。
> 2. Probe Capture 正确绘制阴影。
> 3. Directional Light 对普通 Camera 使用 CSM，对 Probe 使用单张局部正交阴影，不给 Probe 使用 CSM。
> 4. 将 Shadow 的所有权从 `LightingSystem + LightComponent + Renderer + ShadowCastPass` 分散状态，收敛到新的 `ShadowSystem`。
> 5. 保持现有 RDG、材质 ShadowCaster Pass、Spot/Point/Area Shadow 基本行为，不做无关重构。

---

## 1. 当前问题与代码现状

### 1.1 Probe Capture 会继续采样旧的 Static Diffuse Probe

当前 `Renderer::BuildScenePrimitives()` 会无条件执行 Static Diffuse Probe 采样：

```cpp
_static_probe_system.Update(s);

...

collect_gi_data(r.View<ECS::StaticMeshComponent>());
collect_gi_data(r.View<ECS::CSkeletonMesh>());
```

采样成功后会写入：

```cpp
data._gi_data_index = gi_data_it->second;
```

GPU 侧通过：

```cpp
g_primitive_gi_data
```

获得已有 Static Diffuse Probe SH。

但 Reflection Probe 与 Static Diffuse Probe 烘焙都仍然调用普通：

```cpp
renderer->Render(camera, scene);
```

因此当前链路可能变成：

```text
旧 Static Diffuse Probe SH
        ↓
场景物体 diffuse lighting
        ↓
Probe Capture Cubemap
        ↓
Cubemap -> SH / Prefilter
        ↓
新 Probe
```

这是错误的隐式递归。

重新烘焙期间旧 `_baked_data` 仍然有效，因此不能依赖“覆盖资源”自然消除反馈。

### 1.2 Probe Capture 的 Shadow 被两处主动关闭

`Renderer::Render()` 对 Cubemap Target 创建六个 face camera 时：

```cpp
Camera &tmp_cam = Camera::GetCubemapGenCamera(cam, face);
tmp_cam._is_scene_camera = false;
tmp_cam._is_render_shadow = false;
tmp_cam._postprocess_enabled = false;
```

这里显式关闭了 Shadow。

同时 `_is_render_light_probe` 分支只加入：

```cpp
_gbuffer_pass
_lighting_pass
_forward_pass
```

没有加入 `_shadowcast_pass`。

所以仅仅将：

```cpp
tmp_cam._is_render_shadow = true;
```

并不能解决问题。

### 1.3 Shadow 的规划职责分散在多个系统

当前 Shadow 相关职责至少分散在三处：

```text
LightingSystem
    ├─ 更新 Light world state
    ├─ BuildCascadeShadow
    ├─ 构造 Spot / Point / Area shadow camera
    └─ 把 shadow camera 放进 LightComponent

Renderer::PrepareLight
    ├─ 再次遍历 LightComponent
    ├─ 读取 LightComponent::_shadow_cameras
    ├─ 创建 ShadowViewRequest
    ├─ 设置 shadow matrix / index
    └─ 填写 RenderingData

ShadowCastPass
    ├─ 创建/拥有 Shadow RenderTexture
    ├─ Directional / Spot / Area / Point 分支
    └─ 实际绘制 ShadowCaster
```

导致 `LightComponent` 同时承担：

```text
Light 属性
+
Shadow view 临时运行时状态
```

这是 Probe 很难接入的根本原因。

### 1.4 Directional Shadow 直接依赖 Main Camera

`LightingSystem::Update()` 当前 Directional Light 使用：

```cpp
Camera &selected_cam = Camera::sSelected ? *Camera::sSelected : *Camera::sCurrent;
```

随后：

```cpp
BuildCascadeShadow(selected_cam, ...);
```

这意味着：

```text
Directional Light Shadow == Main Camera CSM
```

这个假设对 Reflection Probe / Static Diffuse Probe 是错误的。

Probe 是 360° Capture，不应该围绕某一个 cubemap face 或 Main Camera 生成 CSM。

### 1.5 `_scene_render_data_valid` 当前混入了 View-dependent Shadow 状态

当前 `PrepareSceneRenderData()` 会：

```cpp
PrepareMaterial(s);
BuildScenePrimitives(s);
PrepareScene(s);
PrepareLight(s);
```

然后缓存：

```cpp
_prepared_cascade_shadow_data
_prepared_spot_shadow_data
_prepared_area_shadow_data
_prepared_point_shadow_data
```

后续 Camera 通过：

```cpp
RestorePreparedShadowData();
```

恢复。

这相当于把：

```text
Camera / View dependent shadow state
```

缓存成：

```text
Scene frame data
```

这是错误的职责边界。

Probe 加入自己的 Directional Local Shadow 后，如果继续沿用此结构，非常容易出现：

```text
Probe Shadow 状态污染 Main Camera
```

或者：

```text
Main Camera CSM 被 Probe Capture 错误复用
```

### 1.6 Shadow RT 当前既在 `ShadowCastPass` 持有，又在 RDG 内创建

当前 `ShadowCastPass` 自身有：

```cpp
Ref<RenderTexture> _p_mainlight_shadow_map;
Ref<RenderTexture> _p_addlight_shadow_maps;
Ref<RenderTexture> _p_point_light_shadow_maps;
```

但 `Renderer::DoRender()` 还会每次创建：

```cpp
_rendering_data._rg_handles._main_light_shadow_map = _rd_graph->CreateResource(...);
_rendering_data._rg_handles._addi_shadow_maps = _rd_graph->CreateResource(...);
_rendering_data._rg_handles._point_light_shadow_maps = _rd_graph->CreateResource(...);
```

Shadow 资源所有权不清晰。

本次重构后统一由 `ShadowSystem` 持有持久 RenderTexture，并通过 RDG `Import` 使用。

---

# 2. 本次重构的最终目标结构

最终结构应变成：

```text
LightingSystem
    ↓
只更新 Light 世界状态
    ├─ position
    ├─ direction
    ├─ range
    ├─ color
    └─ area points

Renderer
    ↓
PrepareFrameSceneData
    ├─ Material
    ├─ Primitive
    ├─ Environment
    └─ Light GPU 基础数据

Renderer::Render
    ↓
创建当前 RenderRequest / RenderPurpose
    ↓
ShadowSystem::Prepare
    ├─ Camera -> Directional CSM
    ├─ Probe  -> Directional Single Orthographic Shadow
    ├─ Spot   -> Light-owned perspective shadow
    ├─ Area   -> Light-owned perspective shadow
    └─ Point  -> Light-owned cube shadow
    ↓
Renderer::CullShadowSet
    ↓
ShadowRenderPass
    ↓
GBuffer / DeferredLighting / Forward / Sky
```

Shadow 的核心所有权变为：

```text
Light + ShadowConsumer(RenderPurpose)
                ↓
           ShadowSystem
                ↓
             ShadowSet
```

不要继续维持：

```text
LightComponent -> shadow cameras -> Renderer -> ShadowCastPass
```

---

# 3. 明确的非目标

本任务不要顺手实现以下内容：

- Virtual Shadow Map。
- Shadow Atlas 动态 allocator。
- 跨多帧静态 Shadow Cache。
- Shadow caster revision / dirty tracking。
- GPU Driven Shadow Culling。
- Mesh Shader Shadow。
- DDGI 本体。
- 多次 GI bounce。
- 改写整个 RenderGraph。
- 改写全部 Camera 系统。
- 改写材质 ShadowCaster Pass。
- 改写现有 PCF 算法。

本任务只做足以让 Probe Shadow / GI Capture 正确，并把 Shadow 架构整理到可继续扩展的程度。

---

# 4. 推荐新增文件

新增：

```text
Inc/Render/ShadowSystem.h
Src/Render/ShadowSystem.cpp
```

现有工程使用递归源码收集时，不需要手动维护单独的 source list；如果当前构建配置不是自动收集，则再补充构建文件。

不要新增多层 Shadow 子目录，不需要过度拆文件。

---

# 5. 新的 Render Purpose / Feature 定义

建议放在 `Inc/Render/RenderingData.h`。

## 5.1 `ERenderPurpose`

```cpp
enum class ERenderPurpose : u8
{
    kCamera,
    kReflectionProbe,
    kStaticDiffuseProbe
};
```

含义：

```text
kCamera
    普通 Game / Scene / Preview Camera。

kReflectionProbe
    Reflection Probe Cubemap Capture。

kStaticDiffuseProbe
    Static Diffuse Probe Cubemap Capture。
```

Preview Camera 暂时仍然视为 `kCamera`，不要额外扩枚举。

## 5.2 Render Feature Flag

建议定义：

```cpp
enum ERenderFeature : u32
{
    kRenderFeatureStaticDiffuseGi = 1u << 0u,
    kRenderFeatureReflectionProbe = 1u << 1u,
    kRenderFeatureEnvironmentIbl = 1u << 2u
};
```

辅助常量：

```cpp
inline constexpr u32 kDefaultRenderFeatures = kRenderFeatureStaticDiffuseGi | kRenderFeatureReflectionProbe |
                                               kRenderFeatureEnvironmentIbl;
```

不要把 Shadow 塞进这个 feature mask。

Shadow 是否启用使用独立 `_enable_shadows`，因为 Shadow 同时影响 CPU Shadow planning 和 GPU sampling。

## 5.3 `RenderRequest`

增加：

```cpp
struct RenderRequest
{
    ERenderPurpose _purpose = ERenderPurpose::kCamera;
    u32 _feature_flags = kDefaultRenderFeatures;
    bool _enable_shadows = true;
    f32 _probe_shadow_distance = 40.0f;
};
```

提供构造辅助函数，减少调用点自己手填：

```cpp
static RenderRequest MakeCameraRenderRequest();
static RenderRequest MakeReflectionProbeRenderRequest(f32 shadow_distance);
static RenderRequest MakeStaticDiffuseProbeRenderRequest(f32 shadow_distance);
```

Probe 的默认配置必须是：

```text
Static Diffuse GI    OFF
Reflection Probe     OFF
Environment IBL      ON
Shadow               ON
```

即：

```cpp
request._feature_flags = kRenderFeatureEnvironmentIbl;
request._enable_shadows = true;
```

这里非常重要：

> Probe Capture 禁止的是 Probe-based indirect lighting，不是所有环境光。

Sky / Environment IBL 仍然允许参与 Capture。

---

# 6. `Renderer::Render` API 修改

保留旧调用兼容：

```cpp
void Render(const Camera &cam, const Scene &s);
```

新增：

```cpp
void Render(const Camera &cam, const Scene &s, const RenderRequest &request);
```

旧版本实现：

```cpp
void Renderer::Render(const Camera &cam, const Scene &s)
{
    Render(cam, s, MakeCameraRenderRequest());
}
```

不要让调用方继续直接改：

```cpp
renderer->_is_render_light_probe
```

本次重构完成后删除：

```cpp
bool _is_render_light_probe = false;
```

Renderer 保存当前 request：

```cpp
RenderRequest _render_request;
```

提供只读查询：

```cpp
const RenderRequest &GetRenderRequest() const { return _render_request; }

bool IsRenderingProbe() const
{
    return _render_request._purpose == ERenderPurpose::kReflectionProbe ||
           _render_request._purpose == ERenderPurpose::kStaticDiffuseProbe;
}
```

`VolumetricClouds.cpp` 当前：

```cpp
if (renderer._is_render_light_probe)
    return;
```

修改为：

```cpp
if (renderer.IsRenderingProbe())
    return;
```

所有其他 `_is_render_light_probe` 使用点一并替换。

---

# 7. Probe Capture 调用点修改

## 7.1 Reflection Probe

当前 `LightingSystem::Update()` 中大致为：

```cpp
renderer->_is_render_light_probe = true;
renderer->SubmitTaskPass(comp._pass.get());
renderer->Render(cam, *SceneManagement::SceneMgr::Get().ActiveScene());
renderer->_is_render_light_probe = false;
```

改成：

```cpp
const RenderRequest request = MakeReflectionProbeRenderRequest(comp._size);
renderer->SubmitTaskPass(comp._pass.get());
renderer->Render(cam, *SceneManagement::SceneMgr::Get().ActiveScene(), request);
```

`_probe_shadow_distance` 不建议简单使用无限远。

第一版可以：

```cpp
request._probe_shadow_distance = std::min(comp._size, QuailtySetting::s_probe_shadow_distance);
```

新增质量参数：

```cpp
inline static f32 s_probe_shadow_distance = 40.0f;
inline static u32 s_probe_directional_shadow_map_resolution = 1024u;
```

放在 `QuailtySetting`。

Probe cubemap 的 far plane 可以继续大于 shadow distance。

即：

```text
Capture Far
    !=
Probe Shadow Distance
```

## 7.2 Static Diffuse Probe

`LightingSystem::UpdateStaticDiffuseProbeBake()` 当前：

```cpp
renderer->_is_render_light_probe = true;
renderer->Render(camera, *scene);
renderer->_is_render_light_probe = false;
```

改成：

```cpp
const RenderRequest request = MakeStaticDiffuseProbeRenderRequest(QuailtySetting::s_probe_shadow_distance);
renderer->Render(camera, *scene, request);
```

如果 Static Diffuse Probe Group 后续增加独立 shadow distance 配置，再从组件读取；当前不要扩大组件序列化范围。

---

# 8. Probe Cubemap 六个面必须共享一次 Shadow 准备

这是必须项。

当前 `Renderer::Render()`：

```cpp
for (u16 i = 1; i <= 6; ++i)
{
    Camera &tmp_cam = Camera::GetCubemapGenCamera(cam, face);
    DoRender(tmp_cam, ...);
}
```

不能变成每个 face 都生成一次完整 Shadow。

目标：

```text
Probe Render Begin
    ↓
Prepare Probe ShadowSet
    ↓
Cull Probe Shadow Views
    ↓
Render Shadows once
    ↓
+X face
-X face
+Y face
-Y face
+Z face
-Z face
    ↓
Probe Render End
```

因此 `Render()` 在进入 cubemap face loop 前完成：

```cpp
ShadowSet shadow_set;
if (request._enable_shadows)
{
    _shadow_system.Prepare(cam, s, request, shadow_set);
    CullShadowSet(s, shadow_set);
}
```

然后每个 face 调：

```cpp
const bool render_shadow_maps = i == 1u;
DoRender(tmp_cam, s, request, &shadow_set, render_shadow_maps, output_target, dst_view_index);
```

注意：

```cpp
tmp_cam._is_render_shadow = false;
```

必须删除。

不要继续让 Cubemap Camera 自己决定 Shadow。

Shadow 由 `RenderRequest::_enable_shadows` 决定。

Cubemap face camera 只负责：

```text
view/projection
layer mask
output size
postprocess off
scene camera off
```

---

# 9. 新 `ShadowSystem` 数据结构

为降低实现风险，本次允许 `ShadowView` 内部继续持有一个轻量 `Camera` 对象。

不要第一版就额外重构 ViewFrustum 生成工具。

关键是：

> Shadow Camera 不再存在于 `LightComponent` 中，而由 `ShadowSystem` 运行时生成。

## 9.1 Shadow 类型

```cpp
enum class EShadowViewType : u8
{
    kDirectional,
    kSpot,
    kArea,
    kPoint
};
```

## 9.2 Directional 模式

```cpp
enum class EDirectionalShadowMode : u8
{
    kNone,
    kCsm,
    kSingleOrthographic
};
```

## 9.3 `ShadowView`

```cpp
struct ShadowView
{
    Camera _camera;
    CullResult _cull_result;

    EShadowViewType _type = EShadowViewType::kDirectional;
    u16 _light_index = 0u;
    u16 _target_slice = 0u;

    Matrix4x4f _shadow_matrix;

    Vector3f _light_world_position = Vector3f::kZero;
    f32 _camera_near = 0.01f;
    f32 _camera_far = 1.0f;
};
```

这里 `Camera` 只用于：

```text
GetViewFrustum
Position
layer mask
View/Proj
```

不把它注册成场景 Camera。

## 9.4 `DirectionalShadowSet`

```cpp
struct DirectionalShadowSet
{
    EDirectionalShadowMode _mode = EDirectionalShadowMode::kNone;
    u16 _view_begin = 0u;
    u16 _view_count = 0u;
    f32 _shadow_distance = 0.0f;

    Array<Vector4f, RenderConstants::kMaxCascadeShadowMapSplitNum> _splits;
};
```

`_splits`：

- CSM：保持当前 sphere center + radius² 语义。
- Probe Single：只使用 `[0]`。

## 9.5 `ShadowSet`

```cpp
struct ShadowSet
{
    DirectionalShadowSet _directional;
    Vector<ShadowView> _views;

    u8 _additional_shadow_num = 0u;
    u8 _point_shadow_num = 0u;

    bool _valid = false;
};
```

不要在 `ShadowSet` 中保存 GPU Texture 所有权。

Texture 统一由 `ShadowSystem` 持有。

---

# 10. `ShadowSystem` 建议接口

`Inc/Render/ShadowSystem.h`：

```cpp
#pragma once
#ifndef __SHADOW_SYSTEM_H__
#define __SHADOW_SYSTEM_H__

#include "Framework/Common/NonCopyable.h"
#include "Render/RenderingData.h"

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::Render
{
    class ShadowSystem final : public NonCopyable
    {
    public:
        ShadowSystem();
        ~ShadowSystem() = default;

        void Prepare(const Camera &consumer_camera, const SceneManagement::Scene &scene,
                     const RenderRequest &request, ShadowSet &out_shadow_set);

        RenderTexture *GetDirectionalShadowMap(EDirectionalShadowMode mode) const;
        RenderTexture *GetAdditionalShadowMap() const { return _additional_shadow_map.get(); }
        RenderTexture *GetPointShadowMap() const { return _point_shadow_map.get(); }

    private:
        void PrepareDirectional(const Camera &consumer_camera, const SceneManagement::Scene &scene,
                                const RenderRequest &request, ShadowSet &shadow_set);
        void PrepareSpot(const SceneManagement::Scene &scene, ShadowSet &shadow_set);
        void PrepareArea(const SceneManagement::Scene &scene, ShadowSet &shadow_set);
        void PreparePoint(const SceneManagement::Scene &scene, ShadowSet &shadow_set);

        void BuildCameraDirectionalShadow(const Camera &camera, const ECS::LightComponent &light,
                                          ShadowSet &shadow_set);
        void BuildProbeDirectionalShadow(const Camera &probe_camera, const ECS::LightComponent &light,
                                         const RenderRequest &request, ShadowSet &shadow_set);

    private:
        Ref<RenderTexture> _camera_directional_shadow_map;
        Ref<RenderTexture> _probe_directional_shadow_map;
        Ref<RenderTexture> _additional_shadow_map;
        Ref<RenderTexture> _point_shadow_map;
    };
}

#endif
```

可以根据 include 依赖增加前置声明。

不要让 `ShadowSystem` 依赖 `Renderer`。

不要让 `ShadowSystem` 自己执行 Draw。

它负责：

```text
规划 Shadow View
生成 projection
维护 Shadow RT
```

Renderer 负责：

```text
Cull ShadowView
```

ShadowRenderPass 负责：

```text
Draw ShadowCaster
```

---

# 11. Shadow RenderTexture 所有权迁移

## 11.1 Camera Directional CSM

创建：

```cpp
const u32 resolution = QuailtySetting::s_cascade_shaodw_map_resolution;
```

Texture：

```text
2D Array
resolution × resolution
array = RenderConstants::kMaxCascadeShadowMapSplitNum
format = ShadowMap
```

命名建议：

```cpp
"_CameraDirectionalShadowMap"
```

## 11.2 Probe Directional Shadow

创建单张 2D ShadowMap：

```text
resolution = QuailtySetting::s_probe_directional_shadow_map_resolution
array = 1
```

建议仍使用 `Tex2DArray`，array size = 1，这样 shader 采样逻辑更容易与 CSM 共用。

名称：

```cpp
"_ProbeDirectionalShadowMap"
```

## 11.3 Additional Shadow

沿用当前容量：

```text
Spot max + Area max
```

分辨率第一版继续：

```cpp
QuailtySetting::s_cascade_shaodw_map_resolution >> 1u
```

## 11.4 Point Shadow

沿用 CubeArray：

```text
array = RenderConstants::kMaxPointLightNum
```

## 11.5 删除旧资源所有权

当 ShadowSystem 接通后，删除 `ShadowCastPass` 中：

```cpp
Ref<RenderTexture> _p_mainlight_shadow_map;
Ref<RenderTexture> _p_addlight_shadow_maps;
Ref<RenderTexture> _p_point_light_shadow_maps;
```

同时删除 `Renderer::DoRender()` 中对 shadow map 的：

```cpp
_rd_graph->CreateResource(...)
```

Shadow 资源改为 RDG External Import。

---

# 12. `ShadowCastPass` 重构为纯执行器

建议类名改成：

```cpp
ShadowRenderPass
```

如果一次重命名影响太大，可以第一阶段保留 `ShadowCastPass` 类名，但内部必须按新结构实现。

构造：

```cpp
explicit ShadowRenderPass(ShadowSystem *shadow_system);
```

成员：

```cpp
ShadowSystem *_shadow_system = nullptr;
```

不要持有 Shadow Texture。

`RenderingData` 增加：

```cpp
const ShadowSet *_shadow_set = nullptr;
bool _render_shadow_maps = false;
```

含义：

```text
_shadow_set
    当前 top-level Render 使用的 ShadowSet。

_render_shadow_maps
    true  -> 本次 graph 要真正写 Shadow Map。
    false -> 本次 graph 只 Import/Read 已经生成的 Shadow Map。
```

这正好解决 Probe 六个 face 的共享。

---

# 13. RDG 中 Shadow Resource 的 Import 规则

这是本次实现非常重要的一点。

Cubemap 六个 face 是六次 `DoRender()`，因此六次 RenderGraph 都必须拥有有效 Shadow handle。

但只有第一个 face 真正写 Shadow。

因此 `ShadowRenderPass` 在每个 shadow-enabled view 都要存在。

### 13.1 第一个 Probe Face

```text
Import ProbeDirectionalShadowMap
Import AdditionalShadowMap
Import PointShadowMap

Write DSV
Execute Shadow Draw
```

### 13.2 Probe Face 2~6

```text
Import same ProbeDirectionalShadowMap
Import same AdditionalShadowMap
Import same PointShadowMap

Read only
Execute = no-op
```

这样 Deferred / Forward 后续仍然得到有效：

```cpp
_rendering_data._rg_handles._main_light_shadow_map
_rendering_data._rg_handles._addi_shadow_maps
_rendering_data._rg_handles._point_light_shadow_maps
```

为了尽量不改大量已有 Lighting Pass，`_main_light_shadow_map` 这个 handle 名称本次可以暂时保留。

对于 Probe，它实际指向：

```text
_ProbeDirectionalShadowMap
```

对于 Main Camera，它实际指向：

```text
_CameraDirectionalShadowMap
```

### 13.3 Builder 伪代码

```cpp
void ShadowRenderPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
{
    if (rendering_data._shadow_set == nullptr)
        return;

    graph.AddPass(_name, RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
    {
        auto *directional = _shadow_system->GetDirectionalShadowMap(
            rendering_data._shadow_set->_directional._mode);

        rendering_data._rg_handles._main_light_shadow_map = builder.Import(directional);
        rendering_data._rg_handles._addi_shadow_maps = builder.Import(_shadow_system->GetAdditionalShadowMap());
        rendering_data._rg_handles._point_light_shadow_maps = builder.Import(_shadow_system->GetPointShadowMap());

        if (rendering_data._render_shadow_maps)
        {
            rendering_data._rg_handles._main_light_shadow_map =
                builder.Write(rendering_data._rg_handles._main_light_shadow_map, EResourceUsage::kDSV);
            rendering_data._rg_handles._addi_shadow_maps =
                builder.Write(rendering_data._rg_handles._addi_shadow_maps, EResourceUsage::kDSV);
            rendering_data._rg_handles._point_light_shadow_maps =
                builder.Write(rendering_data._rg_handles._point_light_shadow_maps, EResourceUsage::kDSV);
        }
        else
        {
            builder.Read(rendering_data._rg_handles._main_light_shadow_map);
            builder.Read(rendering_data._rg_handles._addi_shadow_maps);
            builder.Read(rendering_data._rg_handles._point_light_shadow_maps);
        }
    }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
    {
        if (!data._render_shadow_maps || data._shadow_set == nullptr)
            return;

        // draw ShadowSet
    });
}
```

如果当前 `builder.Import()` 后再 `builder.Read()` 的接口要求不同，按现有 Environment/CubeMap import 用法调整，但语义必须保持不变。

---

# 14. `LightingSystem` 重构

这是必须完成的核心阶段。

## 14.1 `LightingSystem` 只更新 Light 世界属性

保留：

```cpp
comp->_light._light_pos = transf._position;

Vector3f light_forward = LightComponent::kDefaultDirectionalLightDir;
light_forward.xyz = transf._rotation * light_forward.xyz;
comp->_light._light_dir.xyz = Normalize(Vector3f{light_forward.xyz});
```

Area Light 保留 world points 更新。

删除 `LightingSystem::Update()` 中所有：

```text
BuildCascadeShadow
_shadow_cameras
_cascade_shadow_data
_shaodwcam_num 的 runtime planning
```

包括 Directional / Spot / Point / Area 的 shadow camera 构造。

### 注意执行顺序

当前 Probe Bake 在 Light world state 更新之前执行。

当前顺序大致：

```text
Reflection Probe update
Static Diffuse Probe update
ProcessVXGI
Light transform update
```

必须改成：

```text
UpdateLightWorldState
    ↓
Reflection Probe update
    ↓
Static Diffuse Probe update
    ↓
ProcessVXGI
```

可以在 `LightingSystem::Update()` 中先做一次 `_entities` Light loop，只更新 world data，然后再做 Probe。

推荐拆内部私有函数：

```cpp
void UpdateLightWorldState(Register &r);
void UpdateReflectionProbes(Register &r);
```

已有：

```cpp
void UpdateStaticDiffuseProbeBake(Register &r);
```

最终：

```cpp
void LightingSystem::Update(Register &r, f32 delta_time)
{
    PROFILE_BLOCK_CPU("LightingSystem::Update")

    UpdateLightWorldState(r);
    UpdateReflectionProbes(r);
    UpdateStaticDiffuseProbeBake(r);
    ProcessVXGI(r);
    UpdateProbeDebugVisualization(r);
}
```

不要求严格使用这些函数名，但职责必须分开。

---

# 15. `LightComponent` 清理

ShadowSystem 接管完成后，`LightComponent` 最终只保留配置和 Light runtime world data。

删除：

```cpp
Array<Camera, 6> _shadow_cameras;
Array<Vector4f, 4> _cascade_shadow_data;
```

`ShadowData` 当前：

```cpp
struct ShadowData
{
    bool _is_cast_shadow;
    float _constant_bias;
    float _slope_bias;
    float _padding;
    u16 _shaodwcam_num;
};
```

`_shaodwcam_num` 是 runtime 结果，不应该属于配置。

确认无序列化依赖后删除：

```cpp
u16 _shaodwcam_num;
```

保留：

```cpp
_is_cast_shadow
_constant_bias
_slope_bias
```

如果为了第一阶段编译过渡需要，可以暂时留字段但不再使用；最终任务完成前应删除旧引用。

---

# 16. Directional Main Camera Shadow：迁移现有 CSM

现有 `BuildCascadeFrustumCorners()` 与 `BuildCascadeShadow()` 算法基本可保留。

将它们从 `RenderSystem.cpp` 移到 `ShadowSystem.cpp` 的匿名 namespace。

例如：

```cpp
namespace
{
    struct CascadeShadowInfo
    {
        Matrix4x4f _view;
        Matrix4x4f _projection;
        Vector3f _center = Vector3f::kZero;
        f32 _radius = 0.0f;
    };

    void BuildCascadeFrustumCorners(...);
    CascadeShadowInfo BuildCascadeShadow(...);
}
```

Main Camera：

```cpp
if (request._purpose == ERenderPurpose::kCamera)
{
    BuildCameraDirectionalShadow(consumer_camera, light, shadow_set);
}
```

保持：

```cpp
QuailtySetting::s_main_light_shaodw_distance
QuailtySetting::s_cascade_shadow_map_count
QuailtySetting::s_cascade_shadow_map_split
QuailtySetting::s_cascade_shaodw_map_resolution
```

每一个 cascade：

1. 生成 `ShadowView`。
2. `ShadowView::_camera.SetViewAndProj(view, projection)`。
3. Layer mask 设置为 ShadowCaster layer。
4. `_target_slice = cascade_index`。
5. `_shadow_matrix = view * projection`。
6. 保存 split sphere。

`ShadowSet::_directional`：

```cpp
_mode = EDirectionalShadowMode::kCsm;
_view_begin = 当前 views 起始 index;
_view_count = cascade_count;
_shadow_distance = QuailtySetting::s_main_light_shaodw_distance;
```

---

# 17. Directional Probe Shadow：单张 Local Orthographic

Probe **禁止使用 CSM**。

## 17.1 原则

Reflection Probe / Static Diffuse Probe 都是 360° consumer。

它们没有主相机意义上的：

```text
near cascade
mid cascade
far cascade
```

因此统一生成一张：

```text
Local Orthographic Directional Shadow
```

六个 cubemap face 共用。

## 17.2 推荐实现

新增匿名 namespace helper：

```cpp
struct ProbeDirectionalShadowInfo
{
    Matrix4x4f _view;
    Matrix4x4f _projection;
    Vector3f _center = Vector3f::kZero;
    f32 _radius = 0.0f;
};
```

函数：

```cpp
ProbeDirectionalShadowInfo BuildProbeDirectionalShadow(const Vector3f &probe_position,
                                                        const Vector3f &light_direction,
                                                        f32 shadow_distance,
                                                        u32 shadow_resolution);
```

实现可以直接复用当前 CSM 稳定化思路。

### Step 1：radius

```cpp
f32 radius = std::max(shadow_distance, 0.1f);
```

第一版用 sphere，不需要 box-fit 优化。

### Step 2：Light basis

```cpp
Vector3f light_up = Vector3f::kUp;
if (std::abs(DotProduct(light_direction, light_up)) > 0.99f)
    light_up = Vector3f::kForward;

const Vector3f light_forward = Normalize(light_direction);
const Vector3f light_right = Normalize(CrossProduct(light_up, light_forward));
const Vector3f corrected_up = CrossProduct(light_forward, light_right);
```

### Step 3：构建仅旋转的 light view

保持当前 CSM 方式即可：

```cpp
Matrix4x4f light_view;
BuildViewMatrixLookToLH(light_view, Vector3f::kZero, light_forward, corrected_up);
```

### Step 4：Probe center 转 light space

```cpp
Vector3f center_ls = TransformCoord(light_view, probe_position);
```

### Step 5：Texel snapping

```cpp
const f32 world_units_per_texel = radius * 2.0f / static_cast<f32>(shadow_resolution);
center_ls.x = std::round(center_ls.x / world_units_per_texel) * world_units_per_texel;
center_ls.y = std::round(center_ls.y / world_units_per_texel) * world_units_per_texel;
```

### Step 6：Depth range

第一版：

```cpp
constexpr f32 kCasterDepthMargin = 20.0f;
const f32 min_z = center_ls.z - radius - kCasterDepthMargin;
const f32 max_z = center_ls.z + radius + kCasterDepthMargin;
```

### Step 7：Orthographic projection

```cpp
Matrix4x4f projection;
BuildOrthographicMatrix(projection, center_ls.x - radius, center_ls.x + radius,
                        center_ls.y + radius, center_ls.y - radius, min_z, max_z);
```

### Step 8：World-space snapped center

和当前 CSM 相同：

```cpp
const Vector3f center_ws = light_right * center_ls.x + corrected_up * center_ls.y +
                           light_forward * center_ls.z;
```

### Step 9：写入 `ShadowSet`

```cpp
shadow_set._directional._mode = EDirectionalShadowMode::kSingleOrthographic;
shadow_set._directional._view_begin = static_cast<u16>(shadow_set._views.size());
shadow_set._directional._view_count = 1u;
shadow_set._directional._shadow_distance = shadow_distance;
shadow_set._directional._splits[0] = Vector4f(center_ws, radius * radius);
```

创建一个 `ShadowView`：

```cpp
ShadowView view;
view._type = EShadowViewType::kDirectional;
view._light_index = 0u;
view._target_slice = 0u;
view._camera.SetViewAndProj(info._view, info._projection);
view._camera._layer_mask = shadow_cast_layer;
view._shadow_matrix = info._view * info._projection;
shadow_set._views.emplace_back(std::move(view));
```

这里 `light_index` 按当前引擎只支持主 Directional Shadow 的逻辑先使用 `0`。

不要在本任务顺手实现多 Directional Shadow。

---

# 18. Spot / Area Shadow 构造迁移

Spot / Area shadow 与观察 Camera 无关。

从 `LightingSystem` 中迁移到 `ShadowSystem::PrepareSpot()` / `PrepareArea()`。

逻辑可保持当前行为。

Spot：

```cpp
Camera shadow_camera;
const f32 near_plane = 0.01f;
const f32 far_plane = light._light._light_param.x * 1.5f;
shadow_camera.SetLens(90.0f, 1.0f, near_plane, far_plane);
shadow_camera.Position(light._light._light_pos.xyz);
shadow_camera.LookTo(light._light._light_dir.xyz, Vector3f::kUp);
shadow_camera._layer_mask = shadow_cast_layer;
```

注意：

当前 Spot Shadow 固定 FOV 90°，本任务不要顺便改成 outer spot angle，避免增加行为变化。

可以另建 TODO。

Area 同理保持当前算法。

Additional shadow array index：

```text
Spot 与 Area 共用一张 2D Array
```

继续使用递增 `_target_slice`。

`ShadowSet::_additional_shadow_num` 记录实际使用数量。

---

# 19. Point Shadow 构造迁移

从 `LightingSystem` 移走现有六面 Camera 构造。

保留当前方向：

```cpp
static const Vector3f kTargets[] =
{
    {1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -1.0f}
};
```

以及 up vector。

每个 Point Light 创建 6 个 `ShadowView`。

`_target_slice` 使用 face index：

```cpp
0u ... 5u
```

`_light_index` 使用 Point Light GPU index。

Point Shadow Texture 仍是 CubeArray。

实际 DSV：

```cpp
CalculateViewIndex(Texture::ETextureViewType::kDSV, face, 0, point_light_index);
```

保留当前：

```cpp
CAST_POINT_SHADOW
```

keyword 行为。

---

# 20. Renderer 负责 ShadowSet Cull

新增：

```cpp
void CullShadowSet(const Scene &s, ShadowSet &shadow_set);
```

实现使用现有：

```cpp
Renderer::Cull(const SceneRenderData &scene_data, const ViewCullInput &input, CullResult &output);
```

不要再把 Shadow View 塞进 `_scene_render_data._shadow_views`。

ShadowSet 自己持有 CullResult。

伪代码：

```cpp
void Renderer::CullShadowSet(const Scene &s, ShadowSet &shadow_set)
{
    JobGroup cull_group;

    for (u32 i = 0u; i < shadow_set._views.size(); ++i)
    {
        JobSystem::Get().Dispatch(cull_group, [this, &shadow_set, i]()
        {
            ShadowView &view = shadow_set._views[i];

            ViewCullInput input;
            input._frustum = view._camera.GetViewFrustum();
            input._camera_position = view._camera.Position();
            input._layer_mask = view._camera._layer_mask;
            input._is_custom_vp = view._camera.IsCustomVP();

            Cull(_scene_render_data, input, view._cull_result);
        });
    }

    JobSystem::Get().Wait(cull_group);

    if (auto *animation_system = const_cast<Scene &>(s).GetRegister().GetSystem<ECS::AnimationSystem>();
        animation_system != nullptr)
    {
        for (auto &view : shadow_set._views)
            animation_system->PrepareVisibleSkinning(view._cull_result);
    }

    RefreshScenePrimitiveVertexBuffers();
}
```

注意：

- 如果 Main View 和 Shadow View 已经有统一并行 cull 调度，可以继续合并 JobGroup。
- 第一版以正确和简单为优先，分开等待也可以。
- 不要继续使用 `FinalizeShadowViewData()` 给旧 `_rendering_data._xxx_shadow_data` 填指针。

---

# 21. `SceneRenderData` 不再保存 Shadow View

删除：

```cpp
Vector<ShadowViewRequest> _shadow_views;
```

最终删除：

```cpp
struct ShadowViewRequest
```

`SceneRenderData` 应只描述 scene-shared 数据：

```cpp
struct SceneRenderData
{
    const SceneManagement::Scene *_scene = nullptr;
    const Vector<ScenePrimitive> *_primitives = nullptr;
    const Vector<PrimitiveData> *_primitive_data = nullptr;
    const Vector<MaterialData> *_material_data = nullptr;
};
```

Shadow View 是每次 render request 的状态，不属于 scene cache。

---

# 22. `PrepareSceneRenderData` 去 Camera 化

当前签名：

```cpp
void PrepareSceneRenderData(const Scene &s, const Camera &cam);
```

改为：

```cpp
void PrepareSceneRenderData(const Scene &s);
```

内部不要再：

```cpp
_rendering_data._camera = &cam;
ResetSceneShadowData(s);
PrepareLight(s); // 当前混合 shadow planning
```

改成：

```cpp
void Renderer::PrepareSceneRenderData(const Scene &s)
{
    PROFILE_BLOCK_CPU("RenderPrepare.Scene")

    _environment_system.Update(s);
    _rendering_data._environment = &_environment_system.Runtime();

    _scene_render_data._scene = &s;

    PrepareMaterial(s);
    BuildScenePrimitives(s);
    PrepareScene(s);
    PrepareLightData(s);

    _scene_render_data._primitives = &_scene_primitives;
    _scene_render_data._primitive_data = &_primitive_data;
    _scene_render_data._material_data = &_material_data_cache;
    _scene_render_data_valid = true;
}
```

将当前 `PrepareLight()` 改名或拆成：

```cpp
PrepareLightData()
```

它只填：

```text
Light color
Light position
Light direction
Light params
Area points
ActiveLightCount
bias 配置
```

不要创建 ShadowView。

---

# 23. Directional Shadow 数据必须从 PerScene 迁移为 PerCamera/View

这是本次重构必须特别处理的一点。

当前 `CBufferPerSceneData` 中有：

```cpp
float4 _CascadeShadowSplit[kMaxCascadeShadowMapSplit];
float4 _CascadeShadowParams;
float4 _MainShadowMapParams;
float4x4 _CascadeShadowMatrix[kMaxCascadeShadowMapSplit];
```

这些数据实际上是 **View-dependent**：

```text
Main Camera -> CSM matrices
Probe       -> Single Orthographic matrix
```

因此不能继续让 Probe/Main Camera 共用同一套 PerScene shadow transform。

## 23.1 推荐迁移方式

在 `CBufferPerCameraData` 末尾增加：

```cpp
float4x4 _DirectionalShadowMatrix[kMaxCascadeShadowMapSplit];
float4 _DirectionalShadowSplit[kMaxCascadeShadowMapSplit];
float4 _DirectionalShadowParams;
float4 _DirectionalShadowMapParams;
uint _RenderFeatureFlags;
float3 _RenderViewPadding0;
```

然后补足 padding，使：

```cpp
sizeof(CBufferPerCameraData) % 256 == 0
```

继续成立。

当前 `CBufferPerCameraData` 大约正好 512 bytes；加入上述字段后需要重新计算 padding。

不要删除这个 static_assert：

```cpp
static_assert((sizeof(CBufferPerCameraData) % 256) == 0,
              "Constant Buffer size must be 256-byte aligned");
```

最简单做法：

- 添加字段。
- 编译看当前 size。
- 用 `float4 _render_view_padding[n]` 补齐到下一个 256 边界。

不要靠 `#pragma pack`。

## 23.2 Shader 修改

所有 Directional shadow sampling 从：

```text
CBufferPerSceneData::_CascadeShadowMatrix
CBufferPerSceneData::_CascadeShadowSplit
CBufferPerSceneData::_CascadeShadowParams
_MainShadowMapParams
```

改为读取 `CBufferPerCameraData` 新字段。

为了降低风险，第一阶段可以暂时保留 PerScene 旧字段，不删除，只停止 Shader 使用。

待编译和画面稳定后再决定是否删除旧字段。

## 23.3 Main Camera

写：

```text
count = cascade count
matrix[0..count-1] = CSM
split[0..count-1] = cascade sphere
shadow distance = main shadow distance
```

## 23.4 Probe

写：

```text
count = 1
matrix[0] = Probe Local Ortho
split[0] = Probe sphere center + radius²
shadow distance = probe shadow distance
```

其余 matrix/split 清零，禁止沿用旧帧值。

---

# 24. Render Feature Flag 放入 PerCamera

Probe GI 禁用也必须是 per-view，而不是 scene global。

在上一节新增的：

```cpp
uint _RenderFeatureFlags;
```

中写入 `RenderRequest::_feature_flags`。

`Renderer::PrepareCamera()` 增加：

```cpp
cam_cb_data->_RenderFeatureFlags = _render_request._feature_flags;
```

Shadow camera 使用的临时 `CBufferPerCameraData` 可以将 flag 设为 0。

所有手工创建 `CBufferPerCameraData` 的位置需要保证新字段已初始化。

至少检查：

```text
Src/Render/Renderer.cpp
Src/Render/RenderPipeline.cpp
Src/Render/Features/CommonPasses.cpp
Src/Render/Features/TemporalAA.cpp
Src/Render/Features/RayTraceGI.cpp
Src/RHI/DX12/DXRSample.cpp
Src/UI/UIRenderer.cpp
Src/UI/TextRenderer.cpp
Src/Render/Gizmo.cpp
```

如果局部变量是：

```cpp
CBufferPerCameraData camera_data;
```

建议改成：

```cpp
CBufferPerCameraData camera_data{};
```

避免新字段未初始化。

---

# 25. 禁止 Probe Capture 使用旧 Static Diffuse GI

这是原始 Bug 1 的核心修复。

## 25.1 不要清空 `_baked_data`

禁止使用这种方式：

```cpp
const Guid old = group._baked_data;
group._baked_data = Guid::EmptyGuid();
RenderProbe();
group._baked_data = old;
```

原因：

- Editor Main View 会闪烁。
- Bake 失败需要旧数据继续有效。
- 多线程/异步后不安全。
- 这是修改 Scene State 来控制 View Feature，职责错误。

## 25.2 Shader 侧 gate

找到当前使用：

```text
g_primitive_gi_data
PrimitiveData::_gi_data_index
```

计算 Static Diffuse GI 的 shader 逻辑。

加入：

```hlsl
const bool enable_static_diffuse_gi =
    (_RenderFeatureFlags & kRenderFeatureStaticDiffuseGi) != 0u;
```

只有：

```hlsl
if (enable_static_diffuse_gi && primitive._gi_data_index != kInvalidGiIndex)
{
    // sample/evaluate static diffuse probe SH
}
```

才允许 Static Diffuse Probe 参与。

Probe Capture：

```text
_RenderFeatureFlags 不包含 kRenderFeatureStaticDiffuseGi
```

Main Camera：

```text
包含 kRenderFeatureStaticDiffuseGi
```

## 25.3 CPU `BuildScenePrimitives` 不需要为 Probe 重建

完成 per-view feature gate 后：

```cpp
BuildScenePrimitives(s)
```

仍然可以每帧只构建一次，并保留：

```cpp
PrimitiveData::_gi_data_index
```

Probe View 只是不使用它。

这比 Probe 时重新生成一份 PrimitiveData 更合理。

---

# 26. 禁止 Reflection Probe Capture 采样旧 Reflection Probe

Static Diffuse Probe 只是其中一个反馈源。

Reflection Probe 自己重新烘焙时也不能出现：

```text
旧 Reflection Probe
    ↓
新 Reflection Probe Capture
```

因此找到当前 Reflection Probe specular / box projection / probe selection shader 路径。

加入：

```hlsl
const bool enable_reflection_probe =
    (_RenderFeatureFlags & kRenderFeatureReflectionProbe) != 0u;
```

Probe Capture：关闭。

普通 Camera：开启。

如果当前 Reflection Probe 还没有真正接入主 PBR shader，而只有 Environment Prefilter，则保留此 bit，并确保未来接入时使用；本任务至少完成 Static Diffuse GI gate。

---

# 27. Environment IBL 在 Probe Capture 中仍然保留

不要错误地把：

```text
RadianceTex
PrefilterEnvTex
Environment SH
Sky
```

全部关闭。

Probe 捕获应该允许：

```text
Sky
Environment Lighting
Direct Light
Shadow
Emission
```

关闭：

```text
Static Diffuse Probe GI
Reflection Probe GI
VXGI
RayTraceGI
RTXDI
SSAO
Volumetric Fog
Volumetric Clouds
Postprocess
```

当前 `_is_render_light_probe` 分支已经会关掉部分 feature。

改成使用：

```cpp
IsRenderingProbe()
```

保持这些行为。

---

# 28. `ShadowRenderPass` 的实际绘制逻辑

不要再按旧 `_cascade_shadow_data/_spot_shadow_data/...` 四套数组分支。

统一遍历：

```cpp
for (const ShadowView &view : shadow_set._views)
```

根据 `view._type` 决定 RT 和 Point keyword。

## 28.1 Directional

目标：

```cpp
RenderTexture *shadow_map = _shadow_system->GetDirectionalShadowMap(shadow_set._directional._mode);
```

DSV slice：

```cpp
view._target_slice
```

Camera CSM：0..cascade_count-1。

Probe：0。

## 28.2 Spot / Area

目标：

```cpp
_shadow_system->GetAdditionalShadowMap()
```

slice：

```cpp
view._target_slice
```

## 28.3 Point

目标：

```cpp
_shadow_system->GetPointShadowMap()
```

Cube index：

```cpp
view._light_index
```

face：

```cpp
view._target_slice
```

## 28.4 Draw code 尽量复用

提取小 helper：

```cpp
void DrawShadowView(CommandBuffer *cmd, const ShadowView &view, bool point_shadow);
```

伪代码：

```cpp
void ShadowRenderPass::DrawShadowView(CommandBuffer *cmd, const ShadowView &view, bool point_shadow)
{
    CBufferPerCameraData camera_data{};
    camera_data._MatrixVP = view._shadow_matrix;
    camera_data._CameraPos.xyz = view._light_world_position;
    camera_data._RenderFeatureFlags = 0u;

    if (point_shadow)
    {
        camera_data._ZBufferParams.x = view._camera_near;
        camera_data._ZBufferParams.y = view._camera_far;
    }

    cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &camera_data,
                         RenderConstants::kPerCameraDataSize);

    Vector<QueuedDrawItem> draw_items;

    for (const auto &[queue, objects] : view._cull_result)
    {
        for (const auto &obj : objects)
        {
            Material *material = obj._material;
            if (material == nullptr)
                continue;

            const i16 shadow_pass = material->GetActiveShader()->FindPass("ShadowCaster");
            if (shadow_pass == -1)
                continue;

            if (point_shadow)
                material->EnableKeyword("CAST_POINT_SHADOW");
            else
                material->DisableKeyword("CAST_POINT_SHADOW");

            draw_items.emplace_back(MakeQueuedDrawItem(queue, obj, material, static_cast<u16>(shadow_pass),
                                                       material->SurfaceType() == ESurfaceType::kTransparent));
        }
    }

    EmitQueuedDraws(cmd, draw_items);
}
```

注意现有 Material keyword 是共享 Material 状态。

本任务先保持当前行为，不扩大到 per-draw keyword snapshot 重构。

---

# 29. Light GPU 数据与 ShadowSet 对接

当前 `Renderer::PrepareLight()` 同时写 Light 基础数据和 Shadow 数据。

重构后分两步：

```text
PrepareLightData
    写 scene-shared Light 数据

ApplyShadowSetToView
    写当前 View 的 shadow-related 数据
```

新增：

```cpp
void ApplyShadowSetToView(const Scene &s, const ShadowSet *shadow_set, CBufferPerCameraData &camera_data);
```

Directional 的 view-dependent 数据写 PerCamera。

Spot / Point / Area 的：

```text
shadowmap_index
bias
shadow matrix
```

仍可暂时留在 PerScene Light 数据中，因为它们不依赖 consumer camera。

第一版每次 `PrepareLightData()` 都根据 `LightComponent::_shadow._is_cast_shadow` 填固定索引和 matrix 可由 ShadowSystem 结果同步。

更推荐：

`ShadowSystem::Prepare()` 完成后提供只读 mapping，再由 Renderer 写 scene light shadow metadata。

为了降低改动复杂度，可按以下方式：

### Directional

PerScene Light：

```text
shadowmap_index = 0 / -1
constant_bias
slope_bias
```

PerCamera：

```text
Directional matrices
Directional split
Directional shadow count
Directional shadow distance
```

### Spot / Area

继续在 PerScene Light 保存：

```text
shadowmap_index
shadow_matrix
shadow distance
bias
```

### Point

继续在 PerScene Light 保存：

```text
shadowmap_index
shadow distance
bias
```

Point 六个 matrix 实际只在 Shadow Pass 绘制需要，不需要给 shading shader 全部暴露。

---

# 30. `RenderingData` Shadow 字段清理

最终不再需要：

```cpp
RenderingShadowData _cascade_shadow_data[...];
RenderingShadowData _spot_shadow_data[...];
RenderingShadowData _area_shadow_data[...];
RenderingPointShadowData _point_shadow_data[...];
u8 _addi_shadow_num;
u8 _addi_point_shadow_num;
```

用：

```cpp
const ShadowSet *_shadow_set = nullptr;
bool _render_shadow_maps = false;
```

替代。

`RenderingShadowData` / `RenderingPointShadowData` 如果没有其他引用则删除。

同时删除：

```cpp
ResetSceneShadowData();
FinalizeShadowViewData();
RestorePreparedShadowData();
```

以及 Renderer 成员：

```cpp
_prepared_cascade_shadow_data
_prepared_spot_shadow_data
_prepared_area_shadow_data
_prepared_point_shadow_data
_prepared_addi_shadow_num
_prepared_addi_point_shadow_num
```

这是本次重构必须达到的最终状态。

---

# 31. `_scene_render_data_valid` 新语义

重构后 `_scene_render_data_valid` 只表示：

```text
这一帧 Scene-shared render data 已准备完成
```

包括：

```text
Material data
Primitive data
Primitive GI data
Environment
Light world/GPU base data
```

不包括：

```text
Camera cull
Directional CSM
Probe local directional shadow
Shadow view cull
PerCamera data
```

因此后续多个 View：

```text
Main Camera
Scene Camera
Reflection Probe
Static Diffuse Probe
```

可以共享 scene data，但各自独立拥有 View/ShadowSet。

---

# 32. `PrepareViews` 简化方向

当前 `PrepareViews()` 会把 `_scene_render_data._shadow_views` 混入 `_view_data`。

重构后：

```text
_view_data
```

只保存真正 camera/render view cull 数据。

Shadow view cull 走：

```cpp
CullShadowSet()
```

因此删除：

```cpp
for (auto &request : _scene_render_data._shadow_views)
{
    ...
}
```

`PrepareFrameViews()` 同样删除这部分。

保留正常 Camera cull。

---

# 33. Renderer 顶层推荐流程

## 33.1 普通 Camera

推荐：

```cpp
void Renderer::Render(const Camera &cam, const Scene &s, const RenderRequest &request)
{
    if (_cur_fs == nullptr)
        return;

    _render_request = request;

    if (!_scene_render_data_valid)
        PrepareSceneRenderData(s);

    ShadowSet shadow_set;
    if (request._enable_shadows)
    {
        _shadow_system.Prepare(cam, s, request, shadow_set);
        CullShadowSet(s, shadow_set);
    }

    DoRender(cam, s, request, request._enable_shadows ? &shadow_set : nullptr,
             request._enable_shadows, nullptr, -1);
}
```

实际保留现有 target texture / event / task pass 逻辑，不要机械覆盖整个函数。

## 33.2 Cubemap Probe

```cpp
ShadowSet shadow_set;
if (request._enable_shadows)
{
    _shadow_system.Prepare(cam, s, request, shadow_set);
    CullShadowSet(s, shadow_set);
}

for (u16 i = 1u; i <= 6u; ++i)
{
    const ECubemapFace face = static_cast<ECubemapFace>(i);
    Camera &tmp_cam = Camera::GetCubemapGenCamera(cam, face);

    tmp_cam._is_scene_camera = false;
    tmp_cam._postprocess_enabled = false;
    tmp_cam.OutputSize(cam.TargetTexture()->Width(), cam.TargetTexture()->Height());

    const bool render_shadow_maps = request._enable_shadows && i == 1u;

    DoRender(tmp_cam, s, request, request._enable_shadows ? &shadow_set : nullptr,
             render_shadow_maps, cam.TargetTexture(), dst_view_index);
}
```

不要再：

```cpp
tmp_cam._is_render_shadow = false;
```

---

# 34. `DoRender` 推荐新签名

```cpp
void DoRender(const Camera &cam, const Scene &s, const RenderRequest &request,
              const ShadowSet *shadow_set, bool render_shadow_maps,
              RenderTexture *output_target = nullptr, i32 output_view_index = -1);
```

进入后：

```cpp
_render_request = request;
_rendering_data._camera = &cam;
_rendering_data._shadow_set = shadow_set;
_rendering_data._render_shadow_maps = render_shadow_maps;
```

Pass 选择不要再看：

```cpp
_is_render_light_probe
```

改为：

```cpp
if (IsRenderingProbe())
```

Probe 分支：

```cpp
if (request._enable_shadows)
    _render_passes.emplace_back(_shadowcast_pass.get());

_render_passes.emplace_back(_gbuffer_pass.get());
_render_passes.emplace_back(_lighting_pass.get());
_render_passes.emplace_back(_forward_pass.get());
```

ShadowPass 即使 `render_shadow_maps == false` 也要加入，因为它负责 Import shadow external resource，使后续 lighting 获得有效 handle。

---

# 35. Forward / Deferred Lighting 的 Shadow 开关

当前逻辑：

```cpp
const bool use_shadow_maps = rendering_data._camera != nullptr &&
                             rendering_data._camera->_is_render_shadow;
```

改为：

```cpp
const bool use_shadow_maps = rendering_data._shadow_set != nullptr &&
                             rendering_data._shadow_set->_valid;
```

不要再使用 `Camera::_is_render_shadow` 作为渲染策略。

Camera 字段可以暂时保留兼容，但新路径不依赖它。

待所有调用迁移后，可以评估删除 `_is_render_shadow`。

---

# 36. Probe 不执行昂贵 Screen-space / View-space Feature

当前行为继续保留：

Probe：

```text
SSAO            OFF
Fog             OFF
RayTraceGI      OFF
RTXDI           OFF
VXGI            OFF
VolumetricCloud OFF
Postprocess     OFF
Motion Vector   不需要
HZB             不需要
```

Probe 的最小 Pass：

```text
Shadow import/render
GBuffer
Deferred Lighting
Forward Transparent（如果当前 Probe 需要透明）
Sky
CubeMapGen task pass
```

不要为了修 Shadow 把 TAA/Motion/HZB 加回 Probe。

---

# 37. Shadow Layer Mask

当前：

```cpp
static ObjectLayer shadow_cast_layer = Application::Get().NameToLayer("ShadowCaster");
```

迁移到 `ShadowSystem.cpp` 中。

所有 ShadowView：

```cpp
view._camera._layer_mask = shadow_cast_layer._value;
```

不要使用：

```cpp
&=
```

因为新建 shadow camera 的初始 layer mask 可能不是全 1。

直接赋值更明确：

```cpp
view._camera._layer_mask = shadow_cast_layer._value;
```

---

# 38. Shadow Bias

保持现有算法，避免本次画面变化过多。

Directional / Spot / Area 当前有：

```cpp
bias * bias * 0.01f
```

Point 当前直接用原始 bias。

本次不要顺手统一 bias 定义。

只把配置读取位置从 `Renderer::PrepareLight()` 整理到 Light GPU metadata / ShadowSystem。

记录 TODO：

```text
TODO: 未来统一各 Light Type 的 shadow bias 单位。
```

---

# 39. Shadow CSM / Probe shader 兼容

为了尽量少改已有 Directional Shadow shader，Probe Single Shadow 可以表现为：

```text
Directional shadow count = 1
```

即同一个 sampling 函数：

```hlsl
if (_DirectionalShadowParams.x == 1)
{
    // only matrix[0]
}
else
{
    // CSM select
}
```

更推荐参数语义：

```text
_DirectionalShadowParams.x = shadow view count
_DirectionalShadowParams.y = 1 / shadow fade factor
_DirectionalShadowParams.z = 1 / shadow distance
_DirectionalShadowParams.w = directional shadow enabled
```

Main Camera：

```text
x = 4
```

Probe：

```text
x = 1
```

这样 Probe 不需要新建一套 directional shadow shader。

---

# 40. Reflection Probe / Static Probe 的 Shadow 距离

新增：

```cpp
inline static f32 s_probe_shadow_distance = 40.0f;
inline static u32 s_probe_directional_shadow_map_resolution = 1024u;
```

第一版统一使用。

不要将：

```cpp
probe_camera.Far()
```

直接作为 Shadow Distance。

例如：

```text
Probe Capture Far = 500 m
Probe Shadow      = 40 m
```

是完全合理的。

否则 1024 shadow map 覆盖 500m 会严重降低精度。

---

# 41. Static Diffuse Probe Bake 的旧数据生命周期

烘焙过程中旧 `_baked_data` 必须继续保留。

流程：

```text
Old Probe Data
    ↓
仍供普通 Main Camera 使用

Bake Capture View
    ↓
通过 RenderFeatureFlags 禁止采样 Old Probe

Bake 完成
    ↓
生成 New Probe Data Asset
    ↓
成功后替换 group._baked_data
```

如果烘焙失败：

```text
继续使用 Old Probe Data
```

这是预期行为。

---

# 42. Reflection Probe 更新同样需要 old/new 隔离

如果 Reflection Probe 目前是直接写回同一个 `_cubemap`：

建议至少保证 Capture shader 不采样当前 Reflection Probe。

如果未来出现需要一边 Capture、一边主相机继续读旧 reflection probe 的需求，再升级为：

```text
front cubemap
back cubemap
capture -> back
prefilter -> back
complete -> swap
```

本任务不强制实现 double-buffer Reflection Probe。

当前主要通过 feature flag 切断 Capture recursion。

---

# 43. 推荐实施顺序

轻量模型必须按以下顺序做，禁止一口气全部改完再编译。

## Phase 0：建立基线

- 确认当前工程可编译。
- 不改 Shader 行为。
- 记录 Reflection Probe / Static Probe Bake 入口。

编译检查点：工程原样通过。

---

## Phase 1：RenderRequest / RenderPurpose

实现：

- `ERenderPurpose`
- `ERenderFeature`
- `RenderRequest`
- `Renderer::Render(..., request)` overload
- `Renderer::IsRenderingProbe()`
- 替换 `_is_render_light_probe` 调用点

此阶段 Shadow 行为可以暂时保持旧逻辑。

编译检查点：

- Main Camera 正常。
- Probe 仍然可以 Capture。
- 不允许访问 `_is_render_light_probe`。

---

## Phase 2：新建 ShadowSystem，先搬资源

实现：

- `ShadowSystem` class。
- Camera CSM texture。
- Probe directional texture。
- Additional texture。
- Point texture。
- Renderer 持有：

```cpp
ShadowSystem _shadow_system;
```

或：

```cpp
Scope<ShadowSystem> _shadow_system;
```

推荐直接成员对象，无需 heap，除非 include/cycle 不方便。

此阶段可暂时不切换绘制路径。

编译检查点：资源创建成功，无泄漏。

---

## Phase 3：LightingSystem 去 Shadow Camera

- 把 `BuildCascadeShadow` 移到 `ShadowSystem.cpp`。
- LightingSystem 只更新 world light data。
- Probe Update 移到 light world state 更新之后。
- ShadowSystem 开始构造旧等价 shadow camera。

暂时保留 `LightComponent::_shadow_cameras` 字段但不再写。

编译检查点：Light transform/color 正常。

---

## Phase 4：ShadowSet + Shadow cull

实现：

- `ShadowView`
- `DirectionalShadowSet`
- `ShadowSet`
- `ShadowSystem::Prepare()`
- `Renderer::CullShadowSet()`

先让 Main Camera 生成：

```text
CSM + Spot + Area + Point
```

用 debugger/assert 确认 view count。

典型：

```text
1 Directional, 4 cascades
2 Spot
1 Point

views = 4 + 2 + 6 = 12
```

编译检查点：ShadowSet 内容正确。

---

## Phase 5：ShadowRenderPass 切换到 ShadowSet

- Pass 不再读旧 `_rendering_data._cascade_shadow_data` 等。
- Pass 不再持有 RT。
- 使用 ShadowSystem RT。
- RDG 使用 Import。

Main Camera 先跑通。

验收：

- Main Camera CSM 画面与重构前基本一致。
- Spot / Point / Area Shadow 正常。

完成后再继续 Probe。

---

## Phase 6：Probe Directional Single Shadow

- `BuildProbeDirectionalShadow()`。
- Reflection / Static Probe 使用 `kSingleOrthographic`。
- 六个 face 共用一个 ShadowSet。
- 第一个 face 真正 render shadow。
- 后五个 face 只 Import/Read。

验收：

- Probe cubemap 中能看到 Directional shadow。
- 六个 face 阴影方向连续，没有 face-specific CSM 差异。
- GPU capture 中 Probe update 只看到一次 directional shadow draw，而不是六次。

---

## Phase 7：PerCamera Directional Shadow Data + Feature Flags

- CBufferPerCameraData 增加 Directional shadow data。
- Directional sampling shader 改读 PerCamera。
- 加 `_RenderFeatureFlags`。
- Static Diffuse GI shader 加 gate。
- Reflection Probe sampling 加 gate（如果当前已接入）。

验收：

- Probe capture 不再受旧 Probe 影响。
- Main Camera 仍正常使用 Probe GI。
- 同一帧 Probe render 后 Main Camera 的 CSM 不被污染。

---

## Phase 8：Scene/View Cache 清理

删除：

```text
ShadowViewRequest
SceneRenderData::_shadow_views
FinalizeShadowViewData
RestorePreparedShadowData
prepared shadow arrays
ResetSceneShadowData 中旧 runtime array 逻辑
```

`_scene_render_data_valid` 只缓存 Scene 数据。

验收：

- 多 Camera 正常。
- Probe 更新帧主 Camera Shadow 正常。

---

## Phase 9：删除 LightComponent 旧字段

删除：

```cpp
_shadow_cameras
_cascade_shadow_data
_shaodwcam_num
```

全局搜索保证 0 引用。

最终编译检查。

---

# 44. 需要修改的主要文件清单

## 必改

```text
Inc/Render/RenderingData.h
Inc/Render/Renderer.h
Src/Render/Renderer.cpp

Inc/Render/ShadowSystem.h                新增
Src/Render/ShadowSystem.cpp              新增

Inc/Render/Features/CommonPasses.h
Src/Render/Features/CommonPasses.cpp

Inc/Scene/Component.h
Src/Scene/RenderSystem.cpp

Inc/Render/ShaderInterop.h
```

## 需要搜索并调整

```text
Src/Render/Features/VolumetricClouds.cpp
Src/Render/RenderPipeline.cpp
Src/Render/Features/TemporalAA.cpp
Src/Render/Features/RayTraceGI.cpp
Src/RHI/DX12/DXRSample.cpp
Src/UI/UIRenderer.cpp
Src/UI/TextRenderer.cpp
Src/Render/Gizmo.cpp
```

## Shader

当前代码快照未包含完整 shader 资源目录时，在实际工程中全局搜索：

```text
g_primitive_gi_data
_gi_data_index
_CascadeShadowMatrix
_CascadeShadowSplit
_CascadeShadowParams
_MainShadowMapParams
```

然后按本文修改。

---

# 45. 建议新增 Assert / Debug 校验

开发期间添加：

```cpp
AL_ASSERT(shadow_set._directional._view_count <= RenderConstants::kMaxCascadeShadowMapSplitNum);
AL_ASSERT(shadow_set._additional_shadow_num <=
          RenderConstants::kMaxSpotLightNum + RenderConstants::kMaxAreaLightNum);
AL_ASSERT(shadow_set._point_shadow_num <= RenderConstants::kMaxPointLightNum);
```

Probe：

```cpp
if (request._purpose != ERenderPurpose::kCamera)
{
    AL_ASSERT(shadow_set._directional._mode != EDirectionalShadowMode::kCsm);
    AL_ASSERT(shadow_set._directional._view_count <= 1u);
}
```

Cubemap：

```cpp
AL_ASSERT(!render_shadow_maps || face == ECubemapFace::kPositiveX /* 按实际枚举名调整 */);
```

不要保留大量每帧日志。

可以在 debug 下临时输出一次：

```text
ShadowSet purpose=ReflectionProbe directional=Single views=...
```

验证后删掉。

---

# 46. 必须验证的测试场景

## Test A：旧 Static Diffuse Probe 递归污染

场景：

```text
一个已有明显红色 SH 的旧 Probe Group
一个白色物体
一个新的 Probe Bake
```

操作：重新 Bake。

预期：

- 新 Probe Capture 不出现旧红色 Probe 的二次注入。
- Main Camera 在 Bake 期间仍可继续看到旧 Probe。
- Bake 成功后切换到新数据。

---

## Test B：Directional Shadow in Reflection Probe

场景：

```text
Directional Light
Cube blocker
Ground
Reflection Probe
```

预期：

- Reflection Probe cubemap 中 Ground 有 blocker 的阴影。
- 手动更新前后稳定。
- 六个面拼接时阴影不会因为每 face CSM 不同而跳变。

---

## Test C：Static Diffuse Probe Shadow

场景同上。

Bake Probe Group。

预期：

- 被遮挡区域 SH 明显更暗。
- 重新 Bake 不叠加旧 GI。

---

## Test D：Probe 更新帧 Main Camera CSM 不污染

设置 Reflection Probe：

```text
update every tick = true
```

主 Camera 移动。

预期：

- Main Camera CSM 正常跟随。
- 不出现一帧突然变成 Probe local shadow 的情况。
- `_scene_render_data_valid` 不再恢复 Probe Shadow 状态。

---

## Test E：Spot / Point Shadow

场景：

```text
Spot Light + shadow
Point Light + shadow
Reflection Probe
```

预期：

- Probe Capture 中 Spot / Point shadow 可见。
- Main Camera 中仍然正常。

---

## Test F：无 Shadow

所有 Light：

```text
_is_cast_shadow = false
```

预期：

- ShadowSet 可以为空或 `_valid = false`。
- Forward/Deferred 使用 dummy shadow resource 或不采样。
- RDG 不出现 invalid handle。

---

## Test G：无 Directional，仅 Spot

预期：

- `_directional._mode = kNone`。
- Additional Shadow 仍可正常工作。
- `_main_light_shadow_map` 若 lighting pass 要求有效 handle，绑定 ShadowSystem 提供的 1x1 dummy directional shadow。

建议 ShadowSystem 持有一个统一 dummy ShadowMap，或者继续使用已有 pass dummy texture。

不要让无 Directional 的场景因为 invalid RG handle 崩溃。

---

# 47. GPU Capture / Profiler 验收

Reflection Probe 一次更新时应观察到：

```text
ProbeShadow
    Directional Local Ortho × 1
    Spot × N
    Area × N
    Point × N × 6 faces

Probe +X Scene
Probe -X Scene
Probe +Y Scene
Probe -Y Scene
Probe +Z Scene
Probe -Z Scene
```

不应该出现：

```text
4 CSM × 6 cubemap faces
```

也不应该出现：

```text
Probe +X -> full shadow set
Probe -X -> full shadow set
...
```

Shadow Draw 至少必须在六面间共享。

---

# 48. 性能上的第一阶段要求

本任务的性能底线：

1. Probe 六面只做一次 ShadowSet planning。
2. Probe 六面只做一次 Shadow cull。
3. Probe 六面只绘制一次 Shadow Map。
4. Directional Probe 只有一张 shadow，而不是 CSM。
5. Main Camera 行为不比重构前明显增加 CPU shadow planning。

暂不强制：

```text
Probe 与 Main Camera 之间共享 Spot/Point Shadow Draw
```

这是下一步优化，可以做，但不是这次正确性重构的阻塞项。

原因：

- Probe 六面共享已经消除了最大的重复。
- Probe 与 Main Camera 跨 top-level consumer 共享需要额外的 frame cache 生命周期管理。
- 先保证架构与结果正确，再加 frame-shared local shadow cache 风险更低。

如果实现过程中非常容易复用，则可以加入；否则不要扩大本次任务。

---

# 49. 可选后续优化：Frame Shared Local Shadow

本任务完成后可追加：

```text
Spot / Point / Area Shadow
    ↓
一帧生成一次
    ↓
Reflection Probe / Static Probe / Main Camera 共用
```

可以在 ShadowSystem 增加：

```cpp
u64 _shared_local_shadow_frame = std::numeric_limits<u64>::max();
bool _shared_local_shadow_rendered = false;
```

但不要在当前重构未稳定前实现跨 consumer cache。

Directional Shadow 不共享：

```text
Camera CSM != Probe Local Ortho
```

---

# 50. 易踩坑

## 50.1 不要让 Probe 继续用 `Camera::sSelected`

ShadowSystem 只能使用显式传入的：

```cpp
consumer_camera
```

Probe 绝不能偷偷读：

```cpp
Camera::sSelected
Camera::sCurrent
```

来决定 Directional shadow。

---

## 50.2 Probe Directional shadow 必须基于 parent probe position

不要用六个 `tmp_cam` 分别生成 directional shadow。

必须使用 cubemap parent：

```cpp
cam.Position()
```

只生成一次。

---

## 50.3 External Shadow RT 的 RDG 状态

同一个 persistent ShadowMap 会跨多个 RenderGraph 使用。

确保每个 graph 都通过：

```cpp
builder.Import(...)
```

建立当前 graph 的 resource handle。

不要跨 graph 直接复用旧 `RGHandle`。

`RGHandle` 只在所属 graph 内有效。

---

## 50.4 后五个 Cubemap face 仍然需要 Shadow RG handle

不能因为不重画 Shadow 就完全跳过 ShadowPass。

否则 Deferred/Forward：

```cpp
builder.Read(rendering_data._rg_handles._main_light_shadow_map);
```

会读 invalid handle。

正确方式是：

```text
Shadow pass retained
render_shadow_maps=false
import + read only
execution no-op
```

---

## 50.5 清零 PerCamera Directional 数据

每次 `PrepareCamera()` 先：

```cpp
for (...)
{
    _DirectionalShadowMatrix[i] = Matrix4x4f::kIdentity; // 按实际 API
    _DirectionalShadowSplit[i] = Vector4f::kZero;
}

_DirectionalShadowParams = Vector4f::kZero;
```

再 Apply ShadowSet。

避免无 shadow view 继承上一个 camera 的值。

---

## 50.6 Probe Capture 不要关闭 Environment IBL

旧 Probe GI 与 Environment IBL 是两个概念。

只关：

```text
Static Diffuse Probe
Reflection Probe
```

不要把天空变黑。

---

## 50.7 不要为了避免递归直接禁掉所有 indirect lighting

Environment SH / Sky 可以保留。

如果未来做 multi-bounce Static GI，再显式实现 bounce iteration。

---

## 50.8 `LightComponent` Generated/Serialization

删除字段前检查：

```text
_shadow_cameras
_cascade_shadow_data
_shaodwcam_num
```

是否参与 reflection/serialization。

当前它们看起来是 runtime 字段，不是 `APROPERTY`，但仍要全局搜索 generated code。

如果生成系统需要重新生成头文件，按 AiluHeadTool 现有流程执行，不要手改 generated 文件作为最终方案。

---

# 51. 代码风格要求

遵循项目现有风格，同时执行以下要求：

- 普通变量：`lower_snake_case`
- 类/结构体成员：`_lower_snake_case`
- static 变量：`s_...` 或现有项目一致的 `s...` 形式；不要新增全局裸 static 状态。
- 常量：`kCamelCase`
- 类/结构体/函数：`CamelCase`
- 尽量保持 120 列以内。
- 不增加无意义 wrapper/interface。
- 不引入多层继承。
- 不引入 ShadowManager + ShadowSystem + ShadowContext 三套重复抽象。
- `ShadowSystem` 一个核心类足够。
- 生命周期优先使用 Renderer 成员所有权，避免全局 singleton。

---

# 52. 推荐的最终类关系

```text
Renderer
 ├─ EnvironmentSystem
 ├─ StaticDiffuseProbeSystem
 ├─ ShadowSystem
 ├─ ShadowRenderPass
 ├─ DeferredGeometryPass
 ├─ DeferredLightingPass
 └─ ForwardPass

ShadowSystem
 ├─ CameraDirectionalShadowMap
 ├─ ProbeDirectionalShadowMap
 ├─ AdditionalShadowMap
 └─ PointShadowMap

ShadowSet
 ├─ DirectionalShadowSet
 └─ Vector<ShadowView>

ShadowView
 ├─ Camera
 ├─ CullResult
 ├─ type
 ├─ light index
 ├─ target slice
 └─ shadow matrix
```

`LightComponent`：

```text
只保存 Light/Shadow 配置
不保存 Shadow Camera
```

---

# 53. 最终帧流程

## 普通 Camera

```text
Frame Begin
    ↓
LightingSystem::UpdateLightWorldState
    ↓
PrepareSceneRenderData once
    ↓
ShadowSystem::Prepare(Camera)
    ↓
Directional CSM
Spot / Area / Point ShadowViews
    ↓
CullShadowSet
    ↓
ShadowRenderPass
    ↓
Camera GBuffer
    ↓
Lighting
    ↓
Forward
    ↓
Postprocess
```

## Reflection Probe

```text
LightingSystem::UpdateLightWorldState
    ↓
Reflection Probe dirty
    ↓
RenderRequest
    purpose = ReflectionProbe
    static diffuse GI = OFF
    reflection probe = OFF
    environment IBL = ON
    shadow = ON
    ↓
ShadowSystem::Prepare(Probe Parent Camera)
    ↓
Directional Local Ortho × 1
Spot / Area / Point
    ↓
CullShadowSet once
    ↓
ShadowRenderPass once
    ↓
+X
-X
+Y
-Y
+Z
-Z
    ↓
Prefilter
```

## Static Diffuse Probe

```text
LightingSystem::UpdateLightWorldState
    ↓
Static Probe Bake
    ↓
RenderRequest
    purpose = StaticDiffuseProbe
    static diffuse GI = OFF
    reflection probe = OFF
    environment IBL = ON
    shadow = ON
    ↓
ShadowSystem::Prepare
    ↓
Capture Cubemap with Shadow
    ↓
Project Cubemap -> SH
    ↓
Bake success
    ↓
Replace baked data
```

---

# 54. 最终删除清单

任务完成前全局搜索，以下旧结构应不再存在或无有效引用：

```text
Renderer::_is_render_light_probe
LightComponent::_shadow_cameras
LightComponent::_cascade_shadow_data
ShadowData::_shaodwcam_num
SceneRenderData::_shadow_views
ShadowViewRequest
Renderer::_prepared_cascade_shadow_data
Renderer::_prepared_spot_shadow_data
Renderer::_prepared_area_shadow_data
Renderer::_prepared_point_shadow_data
Renderer::_prepared_addi_shadow_num
Renderer::_prepared_addi_point_shadow_num
Renderer::RestorePreparedShadowData
Renderer::FinalizeShadowViewData
```

旧 `RenderingShadowData` / `RenderingPointShadowData` 若无引用则删除。

`Camera::_is_render_shadow` 可以暂时保留兼容，但新 ShadowSystem 路径不得依赖它。

---

# 55. 完成标准

只有以下条件全部满足才算任务完成：

- [ ] Reflection Probe Capture 中旧 Probe GI 不参与。
- [ ] Static Diffuse Probe Bake 中旧 Static Diffuse Probe SH 不参与。
- [ ] Probe Capture 中 Direct Light 阴影正常。
- [ ] Probe Directional Shadow 为 Single Orthographic，不是 CSM。
- [ ] Cubemap 六个 face 共用一次 Shadow planning/cull/render。
- [ ] Main Camera 仍使用原有 CSM。
- [ ] Spot Shadow 正常。
- [ ] Point Shadow 正常。
- [ ] Area Shadow 不比重构前退化。
- [ ] Probe 更新帧不会污染 Main Camera CSM。
- [ ] `_scene_render_data_valid` 不再缓存 shadow view/runtime 数据。
- [ ] Shadow RT 所有权统一到 `ShadowSystem`。
- [ ] Shadow Pass 不再持有 Shadow RT。
- [ ] LightComponent 不再持有 Shadow Camera。
- [ ] Probe 六面只有第一面真正执行 Shadow draw。
- [ ] 后五个 face 仍然能通过 RDG Import 正确采样 shadow texture。
- [ ] 无 shadow 的场景不访问 invalid RG handle。
- [ ] `CBufferPerCameraData` 保持 256 byte alignment。
- [ ] Debug/Release 编译通过。
- [ ] 无新增明显 lifetime / dangling pointer 问题。

---

# 56. 给实现模型的执行要求

1. 先阅读本文与列出的相关文件，再开始修改。
2. 每完成一个 Phase 就编译，不要一次修改所有文件。
3. 遇到现有接口名与本文略有差异时，优先沿用工程已有接口，不要为了完全匹配文档新增重复 API。
4. 不要删除当前能工作的 Shadow shader/material 逻辑，只迁移数据来源与资源所有权。
5. 不要以“暂时关闭 Shadow”作为 Probe 修复方案。
6. 不要以“Bake 时清空旧 Probe 数据”作为递归 GI 修复方案。
7. 不要让 Probe 使用 Main Camera CSM。
8. 不要让每个 cubemap face 单独生成 Shadow。
9. 不要跨 RenderGraph 保存/复用 `RGHandle`；复用的是 `RenderTexture`，每个 graph 重新 Import。
10. 如果 Shader 文件不在当前源码包内，完成 C++ 后明确列出需要修改的 Shader symbol，不要伪造文件路径。
11. 如果某一步出现编译问题，优先保持结构目标不变并做最小适配，不退回旧 `_shadow_cameras` 架构。
12. 最终给出实际修改文件列表、删除的旧字段、测试结果和仍存在的 TODO。

---

# 57. 推荐最终提交说明

```text
Refactor shadow ownership and fix probe capture lighting

- move shadow planning/resources into ShadowSystem
- keep camera directional shadows as CSM
- use one local orthographic directional shadow for probe capture
- share one shadow render across six cubemap faces
- isolate probe capture from baked diffuse/reflection probe lighting
- move directional shadow transforms to per-view camera data
- remove shadow cameras from LightComponent
- separate scene render cache from view/shadow state
- import persistent shadow resources into RDG
```

