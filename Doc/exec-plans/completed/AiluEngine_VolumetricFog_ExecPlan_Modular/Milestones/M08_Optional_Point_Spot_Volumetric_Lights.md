# Milestone 8 — Optional Point / Spot Volumetric Lights

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

扩展 local volumetric lights。

## Constraint

禁止：

```text
每个 froxel 遍历场景所有灯光
```

如果只做实验验证，可限制：

```text
1~4 local volumetric lights
```

正式实现等待 renderer light culling / cluster infrastructure 可复用。

---
