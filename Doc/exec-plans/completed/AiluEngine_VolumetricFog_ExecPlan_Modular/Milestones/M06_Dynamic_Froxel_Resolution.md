# Milestone 6 — Dynamic Froxel Resolution

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

消除固定 `170 x 90 x 96` 对 viewport 的依赖。

---

## M6-01 Quality Configuration

推荐使用：

```text
_xy_tile_size
_z_slice_count
```

候选测试：

```text
Low:
tile = 16
z = 64

Medium:
tile = 12
z = 96

High:
tile = 8
z = 96 or 128
```

数值必须通过 profiling 决定。

---

## M6-02 Remove Implicit Z Double Source

当前：

```cpp
Vector3UInt(..., 96)
```

同时 shader：

```hlsl
#define VOXEL_SLICE_COUNT 96
```

改为：

- runtime constant；或
- 少量 shader quality variants。

不允许继续隐式依赖两边恰好相同。

---

## M6-03 Resize / Quality Change

发生：

```text
viewport resize
quality change
froxel layout change
```

必须：

```text
recreate texture
invalidate history
```

---

## Milestone 6 Acceptance Gate

- 16:9 正确。
- 16:10 正确。
- Ultrawide 正确。
- Resize 无 history contamination。
- Quality change 无 crash。
- GPU cost 与 froxel 数量变化基本匹配。

---
