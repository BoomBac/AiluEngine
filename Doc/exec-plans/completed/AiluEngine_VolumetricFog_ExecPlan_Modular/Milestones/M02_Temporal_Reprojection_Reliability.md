# Milestone 2 — Temporal Reprojection Reliability

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

把简单 history lerp 提升为可靠 temporal reuse。

---

## M2-01 History State Structure

新增类似：

```cpp
struct VolumetricFogHistory
{
    Ref<Texture3D> _light_a;
    Ref<Texture3D> _light_b;

    Matrix4x4f _prev_view;
    Matrix4x4f _prev_proj;

    f32 _prev_near = 0.0f;
    f32 _prev_far = 0.0f;

    bool _is_cur_a = true;
    bool _valid = false;
};
```

适配项目实际资源类型。

---

## M2-02 Per-Camera History

目标：

```text
camera_identity -> VolumetricFogHistory
```

优先复用引擎已有稳定 camera identity / revision。

不要默认把临时裸指针作为永久 ID。

覆盖：

```text
Game Camera
Editor Camera
Capture Camera
Cubemap
```

如果 Cubemap 不需要 temporal，明确禁用，而不是共享 history。

---

## M2-03 History Invalidation

以下情况设：

```text
history.valid = false
```

条件：

```text
first frame
camera switch
camera cut
projection change
near change
fog_far change
froxel resolution change
temporal off -> on
history texture recreate
```

FOV/aspect 比较使用 epsilon 或 revision，不使用 fragile exact float compare。

---

## M2-04 Previous Clip Validity

Previous world position 必须确认：

```text
previous clip.w valid
point in front of previous camera
previous z valid
previous uv valid
```

不能只依赖：

```hlsl
prev_uvw in [0, 1]
```

---

## M2-05 Lightweight Luminance Rejection

第一版先做：

```text
current_luma
history_luma
relative_difference
```

超阈值：

```text
history_weight = 0
```

或按差异衰减 history confidence。

不要第一版直接做复杂 variance clipping。

---

## M2-06 Optional Neighborhood Clamp

只有 M2-05 后仍明显 ghosting 才做。

优先：

```text
XY cross
or
6-neighbor
```

不默认上 3x3x3。

---

## M2-07 Temporal Weight Semantics

当前：

```text
lerp(history, current, blend_factor)
```

意味着：

```text
blend_factor 越小 -> history 越重
```

考虑属性重命名：

```text
_temporal_current_weight
```

验证值：

```text
0.05
0.10
0.15
0.25
```

---

## Milestone 2 Acceptance Gate

Static Camera：

- noise 数帧内明显稳定。
- volumetric shadow 不持续闪。

Moving Camera：

- 无明显长时间 trail。
- 遮挡变化能快速响应。

Camera Cut：

- 第一帧不读取旧 history。

Multi-Camera：

- history 不串。

---
