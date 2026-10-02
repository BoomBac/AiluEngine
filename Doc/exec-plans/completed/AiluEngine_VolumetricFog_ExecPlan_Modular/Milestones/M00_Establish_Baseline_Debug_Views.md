# Milestone 0 — Establish Baseline & Debug Views

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

建立可重复的基准场景与调试手段，确保之后每一个数学修改都能 A/B 对比。

## Scope

仅建立测试基线和调试输出。

不修改 fog 算法。

## Tasks

### M0-01 Save Baseline Parameters

固定：

```text
fog_density = 1.0
fog_distance = 50
g = 0.0
intensity = 1.0
temporal = off
```

额外保留：

```text
g = 0.6
temporal = on
blend_factor = current baseline
```

### M0-02 Prepare Test Cameras

至少：

```text
Scene A: Plane + Directional Light + long shadow
Scene B: Near cube occluder
Scene C: Wide FOV 90~110°
Scene D: Camera facing sun/light propagation relation
Scene E: Camera facing opposite direction
Scene F: No ShadowMap
Scene G: No Directional Light
Scene H: Two cameras alternating render
```

### M0-03 Debug Views

继续保留现有 debug voxel。

如改动低风险，可加入：

```text
0 Final
1 InjectedLight
2 Density
3 Extinction
4 AccumulatedScattering
5 Transmittance
6 HistoryWeight
7 HistoryRejected
```

DebugMode 不应阻塞 Milestone 0。

## Validation

- Baseline scene 可稳定复现。
- 保存 before screenshot。
- 记录当前 GPU timing。
- Debug voxel 正常。
- Shader 无新增 warning。

## Acceptance Gate

没有 baseline screenshot / capture，不进入 Milestone 1。

---
