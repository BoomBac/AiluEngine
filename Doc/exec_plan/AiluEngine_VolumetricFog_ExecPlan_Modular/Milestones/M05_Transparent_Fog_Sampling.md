# Milestone 5 — Transparent Fog Sampling

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

让透明对象与 volumetric fog 正确融合。

## Constraint

禁止仅通过：

```text
把 fullscreen composite 移到 transparent 后
```

来解决。

---

## M5-01 Global Fog Resource

向透明 shader 暴露：

```text
_VolumetricFogAccum
_fog_far
required camera/froxel params
```

---

## M5-02 Shared Sampling Include

新增类似：

```text
volumetric_fog_common.hlsli
```

提供：

```hlsl
float4 SampleVolumetricFog(float2 screen_uv, float view_depth);
```

定义：

```text
rgb = accumulated scattering
a   = transmittance
```

---

## M5-03 Transparent Apply

透明 fragment 输出前应用：

```text
fogged_rgb = transparent_rgb * transmittance + scattering
```

必须根据：

```text
straight alpha
premultiplied alpha
```

实际 blend model 调整。

---

## M5-04 Avoid Double Fog

确保：

```text
Opaque scene fog once
Transparent fog once
```

透明对象不能既自己 sample，又被 fullscreen composite 再 fog 一次。

---

## Milestone 5 Acceptance Gate

测试：

```text
glass plane
alpha particle
transparent quad
opaque behind transparent
```

要求：

- 无 double fog。
- 远处 particle 自然融入。
- Transparent 不再像贴在 fog 上层。

---
