# Milestone 3 — Medium Model / Height Fog

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

从 homogeneous medium 扩展到 world-space medium。

---

## M3-01 VolumetricLight Alpha Stores Extinction

当前：

```hlsl
_VolumetricLight[idx] = float4(injected, slice_far);
```

改为：

```text
RGB = lighting/scattering source
A   = extinction
```

Integration 使用：

```hlsl
float slice_extinction = voxel.a;
```

---

## M3-02 EvaluateFogMedium()

推荐：

```hlsl
struct FogMedium
{
    float extinction;
    float scattering_albedo;
};

FogMedium EvaluateFogMedium(float3 world_pos)
{
    ...
}
```

第一版可仅使用 extinction。

`scattering_albedo` 暂时固定为 1。

---

## M3-03 Height Fog

基础模型：

```text
density(y) =
    base_density *
    exp(-(y - base_height) * height_falloff)
```

必须 clamp 指数输入，避免低于 base_height 时爆炸。

新增参数建议：

```text
_fog_height
_height_falloff
```

不要一开始加入大量艺术控制量。

---

## M3-04 Uniform Fallback

要求：

```text
height_falloff = 0
```

可退化到 uniform fog。

---

## M3-05 Optional Low-Frequency Noise

Height Fog 验收后才做。

可参考：

```text
volumetric_noise_generator
```

参数：

```text
_noise_strength
_noise_scale
_noise_scroll
```

要求低频，避免 froxel aliasing。

Noise disabled 时不能产生额外采样成本。

---

## Milestone 3 Acceptance Gate

- `height_falloff = 0` 与旧 uniform fog 接近。
- 地面浓、高空自然衰减。
- Camera 上下移动无明显 slice banding。
- Temporal 打开后 medium pattern 稳定。
- Noise off 无额外 texture sample。

---
