# AiluEngine Volumetric Fog — Progress

> Mutable state file.
>
> This is the file that should be updated most often.
>
> Before implementation:
>
> 1. Read `00_Architecture_Overview.md`.
> 2. Read `01_Milestone_Index.md`, then open only the current file under `Milestones/`.
> 3. Read this file.
>
> After implementation:
>
> 1. Update checkboxes.
> 2. Record validation.
> 3. Record discoveries/decisions only if they affect future work.
> 4. Set the next task.

# Current Milestone

```text
Milestone 0 — Establish Baseline & Debug Views
```

Recommended immediate sequence:

```text
M0 baseline
    ↓
M1-01 ray thickness
    ↓
M1-02 accumulation Z mapping
    ↓
M1-03 jitter
    ↓
M1-04 HG direction
    ↓
M1-05 shadow optional
    ↓
M1-06 no-light safety
    ↓
Milestone 1 validation
```

禁止同时实现多个尚未验证的数学修改。

# Progress

## Milestone 0 — Baseline

- [ ] 保存 baseline screenshots
- [ ] 建立 Wide FOV 测试镜头
- [ ] 建立 Directional Shadow 测试镜头
- [ ] 建立 No ShadowMap 测试
- [ ] 建立 No Directional Light 测试
- [ ] 建立 Multi-Camera 测试
- [ ] 记录 baseline GPU timing
- [ ] Milestone 0 acceptance passed

## Milestone 1 — Correctness

- [ ] M1-01 修复 ray thickness
- [ ] M1-01 validation passed
- [ ] M1-02 修复 accumulation Z mapping
- [ ] M1-02 validation passed
- [ ] M1-03 限制 jitter 到 single slice
- [ ] M1-03 validation passed
- [ ] M1-04 确认并修复 HG phase direction
- [ ] M1-04 validation passed
- [ ] M1-05 ShadowMap 改为 optional
- [ ] M1-05 validation passed
- [ ] M1-06 No Main Light safety
- [ ] M1-06 validation passed
- [ ] 保存 Milestone 1 A/B screenshots
- [ ] 记录 Milestone 1 GPU timing
- [ ] Milestone 1 acceptance passed

## Milestone 2 — Temporal

- [ ] M2-01 新增 history state
- [ ] M2-02 per-camera history
- [ ] M2-03 history invalidation
- [ ] M2-04 previous clip validity
- [ ] M2-05 luminance rejection
- [ ] M2-06 optional neighborhood clamp
- [ ] M2-07 temporal parameter tuning
- [ ] static-camera validation
- [ ] moving-camera validation
- [ ] camera-cut validation
- [ ] multi-camera validation
- [ ] 记录 Milestone 2 GPU timing
- [ ] Milestone 2 acceptance passed

## Milestone 3 — Medium / Height Fog

- [ ] M3-01 `_VolumetricLight.a` 改为 extinction
- [ ] M3-02 `EvaluateFogMedium`
- [ ] M3-03 Height Fog
- [ ] M3-04 Uniform fallback
- [ ] M3-05 optional low-frequency noise
- [ ] density/extinction debug view
- [ ] height movement validation
- [ ] temporal stability validation
- [ ] 记录 Milestone 3 GPU timing
- [ ] Milestone 3 acceptance passed

## Milestone 4 — Environment Scattering

- [ ] M4-01 environment source
- [ ] M4-02 ambient intensity
- [ ] M4-03 source-term integration
- [ ] shadow-region validation
- [ ] ambient=0 regression
- [ ] 记录 Milestone 4 GPU timing
- [ ] Milestone 4 acceptance passed

## Milestone 5 — Transparent Integration

- [ ] M5-01 global fog resource
- [ ] M5-02 common fog sampling include
- [ ] M5-03 transparent apply
- [ ] M5-04 double-fog validation
- [ ] glass validation
- [ ] particle validation
- [ ] Milestone 5 acceptance passed

## Milestone 6 — Dynamic Froxel Resolution

- [ ] M6-01 viewport/quality based XY resolution
- [ ] M6-02 configurable Z slices
- [ ] M6-03 resource recreation/history invalidation
- [ ] 16:9 validation
- [ ] 16:10 validation
- [ ] ultrawide validation
- [ ] resize validation
- [ ] quality-switch validation
- [ ] profiling
- [ ] Milestone 6 acceptance passed

## Optional Follow-up

- [ ] Milestone 7 Local Fog Volume
- [ ] Milestone 8 Point Light
- [ ] Milestone 8 Spot Light
- [ ] clustered/tiled light/fog culling

# Validation Records

## Baseline

Build:

```text
Not recorded
```

GPU:

```text
LightInject:      Not recorded
LightIntegration: Not recorded
Composite:        Not recorded
```

Captures / Screenshots:

```text
Not recorded
```

Known visual artifacts:

```text
- Wide-FOV edge fog thickness suspected incorrect.
- Accumulation Z mapping suspected half-slice shifted.
- Jitter currently spans multiple slices.
- HG direction convention requires confirmation.
```

---

## Milestone 1

Status:

```text
Not started
```

Build:

```text
Not recorded
```

GPU:

```text
Not recorded
```

Validation summary:

```text
Not recorded
```

---

## Milestone 2

Status:

```text
Not started
```

Validation summary:

```text
Not recorded
```

---

## Milestone 3

Status:

```text
Not started
```

Validation summary:

```text
Not recorded
```

---

## Milestone 4

Status:

```text
Not started
```

Validation summary:

```text
Not recorded
```

# Decision Log

Only add decisions discovered during implementation that are not already stable architecture decisions.

## Runtime Decisions

None yet.

# Surprises & Discoveries

Only add new findings from implementation/profiling.

Current known items are documented in `00_Architecture_Overview.md`.

## New Discoveries

None yet.

# Known Follow-ups

Non-blocking:

1. MaxZ / depth-aware early integration optimization.
2. Local Fog Volume.
3. Point / Spot volumetric lights.
4. Clustered volumetric light culling.
5. Multiple scattering approximation.
6. Physically based scattering albedo.
7. Colored absorption.
8. Volumetric emissive medium.
9. DDGI / Surfel GI indirect contribution.
10. Replace hard-coded blue-noise `1024` dimensions with runtime dimensions.
11. Higher-quality temporal variance clipping.
12. Froxel checkerboard/interleaved update.
13. Packed/half format bandwidth optimization.
14. Async compute feasibility.
15. Dynamic-resolution integration.
16. Fog / VolumetricCloud atmospheric lighting unification.

# Outcomes

## Delivery A

```text
Milestone 0 + 1
```

Status:

```text
Not complete
```

## Delivery B — Recommended Target

```text
Milestone 0 ~ 4
```

Status:

```text
Not complete
```

## Delivery C

```text
Milestone 0 ~ 6
```

Status:

```text
Not complete
```

# Resume Protocol

## 1. Read Stable Context

Read:

```text
00_Architecture_Overview.md
```

Do not redesign the system before reading it.

## 2. Read Current Progress

Read:

```text
02_Progress.md
```

Find:

```text
Current Milestone
first unchecked task
last passed validation
```

## 3. Read Only Relevant Milestone

Use `01_Milestone_Index.md` to locate and open only the current file under:

```text
Milestones/
```

Avoid loading all milestone implementation details unless necessary.

## 4. Check Git Diff

Focus on:

```text
Engine/Src/Render/Features/VolumetricFog.cpp
Engine/Inc/Render/Features/VolumetricFog.h
Engine/Res/Shaders/hlsl/Compute/volumetric_light.hlsl
Engine/Res/Shaders/hlsl/volumetric_fog_composite.hlsl
Engine/Res/Shaders/hlsl/froxel_common.hlsli
```

If current diff modifies unrelated systems, determine whether it is intentional before continuing.

## 5. Continue From First Unchecked Task

Do not repeat tasks that already have:

```text
implementation checked
+
validation checked
```

## 6. Validation Is Required

“Code written” does not mean task complete.

Only mark a milestone complete when:

```text
Implementation complete
+
Validation complete
+
No blocking regression
+
Progress updated
```

## 7. Stop on Broken Assumptions

If a baseline assumption is wrong:

- stop adding features;
- update the relevant architecture/decision note;
- re-run current correctness validation.

## 8. Avoid Cross-Milestone Changes

Especially while Milestone 1 is incomplete, do not implement:

```text
Height Fog
Noise
Local Fog
Point Light
Spot Light
```

## 9. End-of-Session Update

Before ending an implementation session, record:

```text
Current task
Completed task
Build status
Validation performed
GPU timing if available
Capture/screenshot name
Known artifact
New discovery
Next task
```

## 10. Next Task

Current next task:

```text
M0-01 / M0-02 — establish baseline scenes and captures
```
