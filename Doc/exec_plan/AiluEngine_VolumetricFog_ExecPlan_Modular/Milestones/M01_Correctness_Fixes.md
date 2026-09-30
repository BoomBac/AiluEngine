# Milestone 1 — Correctness Fixes

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

修复现有体积雾确定性的数学/资源依赖问题。

本阶段禁止新增：

```text
Height Fog
Noise
Local Fog
Point Light
Spot Light
```

---

## M1-01 Ray Thickness Correction

目标文件：

```text
Compute/volumetric_light.hlsl
```

当前：

```hlsl
float thickness = slice_z_far - slice_z_near;
```

改为 ray-space distance：

```hlsl
float3 view_ray = normalize(GetViewRay(uv));
float ray_z = max(abs(view_ray.z), 1e-4f);
float thickness = (slice_z_far - slice_z_near) / ray_z;
```

确认 `GetViewRay()` 与当前 projection convention 一致。

不要用 world-space ray 来计算 view-Z correction ratio。

### Validation

Wide FOV：

- 屏幕中心与边缘均匀 fog 强度接近。
- 不出现边缘异常变浓。
- 无 NaN。

---

## M1-02 Accumulation Z Mapping

### Phase A — Minimal Fix

保持 Texture3D depth 不变。

当前 storage：

```text
FogAccum[z] = integral(near -> far_boundary(z))
```

Texture center：

```text
(z + 0.5) / N
```

Composite 不能直接使用 boundary fraction 当 texture center coordinate。

实现：

- 明确 world depth -> accumulation sample coordinate。
- `view_depth <= near` 时直接：
  - scattering = 0
  - transmittance = 1

### Phase B — Optional Clean Boundary Model

只有 Phase A 后仍存在明显误差才考虑：

```text
N slices
-> N + 1 accumulation boundaries
```

定义：

```text
Accum[0] = identity
Accum[1] = after slice 0
...
Accum[N] = after all slices
```

默认不在第一轮扩大资源改动。

### Validation

- Near plane 不提前出现第一层 fog。
- 近裁剪区域移动物体无明显 fog pop。
- Debug froxel depth 与 composite depth 对齐。

---

## M1-03 Limit Stochastic Jitter

当前类似：

```hlsl
float jitter = (blue.r - 0.5f) * 4.0f;
```

改为例如：

```hlsl
static const float kJitterScale = 0.8f;
float jitter = (blue.r - 0.5f) * kJitterScale;
```

要求：

```text
abs(jitter) <= 0.5 slice
```

默认不能跨多个 slice。

### Validation

观察：

```text
shadow boundary
god ray edge
static camera
```

要求：

- temporal off 可存在单帧噪声；
- 不出现明显跨 slice 跳动；
- noise pattern 比旧版更局部。

---

## M1-04 HG Direction Convention

不要直接修改 `dot()`。

先确认 `_MainlightWorldPosition.xyz` 在：

```text
surface lighting
deferred lighting
shadow
```

中的实际语义。

Shader 内改用明确局部变量：

```hlsl
float3 light_travel_dir;
float3 sample_to_camera_dir;
```

目标：

```text
cos_theta = dot(light_travel_dir, sample_to_camera_dir)
```

且：

```text
g > 0 -> forward scattering
g = 0 -> isotropic
g < 0 -> backward scattering
```

### Validation

固定太阳方向分别测试：

```text
g = +0.7
g = 0
g = -0.7
```

Camera 朝两个相反方向观察。

必须确认正负语义与属性面板一致。

---

## M1-05 ShadowMap Optional

CPU / RenderGraph：

```text
shadow handle valid
    -> Read + Bind
shadow handle invalid
    -> do not Read invalid resource
```

Shader：

```hlsl
float shadow = 1.0f;

if (_has_main_light_shadow)
{
    shadow = ApplyCascadeShadowHard(...);
}
```

不要让 `main_light_shadow_map == nullptr` 直接导致整个 LightInjection return。

### Validation

- Disable shadow 后 fog 仍被方向光照亮。
- Enable shadow 后 volumetric shadow 正常。
- 两种模式无 NaN / black frame。

---

## M1-06 No Main Light Safety

确认没有方向光时：

```text
injected = 0
```

不得出现：

```text
NaN
Inf
history poisoning
invalid _DirectionalLights[0] access
```

如果灯光 buffer 无法保证空槽安全，则增加：

```text
_has_main_light
```

显式分支。

---

## Milestone 1 Acceptance Gate

必须全部满足：

- Wide FOV edge thickness 正确。
- Near-plane accumulation 起点正确。
- Jitter 不跨多个 Z slice。
- `g` 正负语义正确。
- 无 ShadowMap 时 feature 正常。
- 无主方向光时输出有限值。
- 无明显新增 GPU artifact。
- GPU 时间与 baseline 差异可忽略或可解释。

通过后更新 `02_Progress.md`，再进入 Milestone 2。

---
