# Milestone 4 — Environment In-Scattering

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

避免只有 Directional Light 时，阴影区域 fog 过黑。

---

## M4-01 Low-Frequency Environment

优先使用现有：

```text
Environment
SH
Sky ambient
```

第一版可只使用：

```text
SH L0
```

或 sky average color。

不要第一版接 DDGI / Surfel GI。

---

## M4-02 Independent Ambient Intensity

新增：

```text
_ambient_intensity
```

不复用 `_intensity` 同时控制太阳和 ambient。

---

## M4-03 Integrate As Source Term

Environment 必须在 medium source 中注入：

```text
directional source
+
environment source
```

然后统一经过 extinction / integration。

不要在 Composite 直接 add 一层 ambient fog。

---

## Milestone 4 Acceptance Gate

- Shadow region 不再绝对死黑。
- Ambient=0 可恢复 Milestone 3。
- Opaque depth relation 不被破坏。
- Environment 不产生明显“物体自发光”观感。

---
