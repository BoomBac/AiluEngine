# M2 - MotionVector Correctness

## Purpose

Make the motion texture a trustworthy, jitter-independent geometric motion source before TAA uses it.

## Scope

- Camera motion reconstruction after M1.
- Dynamic object motion.
- Dynamic-object occlusion against scene depth.
- First-frame/camera-cut safety.
- RG and legacy path parity where practical.

## Non-goals

- No transparent velocity yet.
- No velocity dilation.
- No TAA history changes.

## Architecture Constraints

Motion vector is defined in texture UV units:

```text
motion = current_no_jitter_uv - previous_no_jitter_uv
```

It must not include Camera jitter. TAA/Cloud/RTGI add jitter delta independently.

## Baseline

`motion_vector.hlsl` already follows the intended matrix split:

```text
SV_POSITION       <- jittered TransformPrimitiveToClipSpace
clip_pos_cur      <- _MatrixVP_NoJitter
clip_pos_pre      <- _MatrixVP_Pre
```

Camera motion reconstructs current world position through `_MatrixIVP`; after M1 this becomes the correct jittered inverse.

The major correctness problem is RG `DynamicMotion`:

```text
writes motion_vector_tex
binds separate motion_vector_depth
```

The separate depth does not contain the current scene's opaque depth, so hidden dynamic objects can write velocity over static
foreground. The material also has `ZWrite: On`.

## Target Outcome

- Static scene: zero motion regardless of jitter sample.
- Camera translation/rotation: coherent camera motion.
- Dynamic frontmost object: object motion.
- Dynamic object behind static foreground: no velocity contamination.
- Jitter sequence changes: no change in motion for otherwise static camera/scene.

## Relevant Files

```text
Engine/Src/Render/Features/CommonPasses.cpp
Engine/Res/Shaders/hlsl/motion_vector.hlsl
Engine/Res/Shaders/hlsl/defered_standard_lit.hlsl
Engine/Inc/Render/RenderingData.h
```

## Milestones

### M2.1 - Validate camera motion after M1 before modifying shader math

Do not immediately edit `CalculateNdcMotionFormClip()`. First run:

```text
static camera + changing TAA jitter
```

Expected motion texture: approximately zero.

If it is non-zero, inspect CameraCB values from M1. A non-zero result usually means current/previous no-jitter matrices are
wrong, not that the motion shader needs a jitter subtraction.

### M2.2 - Stop using isolated dynamic-motion depth

Preferred implementation:

1. Camera-motion fullscreen pass writes motion color without a depth attachment.
2. Dynamic-motion geometry pass binds the existing scene `_depth_target` as DSV for depth test.
3. Dynamic MotionVector material uses `ZWrite: Off` so velocity rendering cannot mutate scene depth.
4. Use `ZTest: LEqual` initially. Only move to `Equal` after confirming precision and alpha-test behavior.

RG setup target:

```cpp
graph.AddPass("DynamicMotion", RDG::PassDesc(), [&](RDG::RenderGraphBuilder &builder)
{
    builder.Read(rendering_data._rg_handles._motion_vector_tex);
    builder.Read(rendering_data._rg_handles._depth_target, EResourceUsage::kDSV);
    rendering_data._rg_handles._motion_vector_tex =
        builder.Write(rendering_data._rg_handles._motion_vector_tex);
}, ...);
```

Execution target:

```cpp
cmd->SetRenderTarget(rendering_data._rg_handles._motion_vector_tex,
                     rendering_data._rg_handles._depth_target);
```

Do not write `_depth_target` from this pass.

Shader metadata change in `defered_standard_lit.hlsl` MotionVector pass:

```text
ZTest: LEqual
ZWrite: Off
```

`_motion_vector_depth` can remain temporarily in `RenderingData` if other code references it, but it must no longer be needed
for DynamicMotion. Remove it only after a repository-wide search confirms no remaining consumer.

### M2.3 - Mirror correctness in legacy Execute path

The non-RG path currently allocates a temporary motion depth and renders both camera/object motion against it. Change it to:

```text
camera motion:
    color = motion_vector_rt
    no DSV required

dynamic motion:
    color = motion_vector_rt
    depth = camera scene depth target
    depth test enabled
    ZWrite Off
```

If the legacy path is formally deprecated, document that decision instead of leaving a known divergent bug.

### M2.4 - Clear/initial state

Confirm the RG motion texture has a deterministic value before `StaticMotion` writes the full screen. The camera motion pass is a
fullscreen draw and should fully initialize the motion texture. Do not add a redundant clear if the full-screen pass guarantees
coverage.

For first frame / invalid previous camera state, choose one explicit behavior:

```text
force previous VP = current no-jitter VP
```

so the first-frame camera motion is zero.

### M2.5 - Dynamic transform previous state

Verify `PrimitiveData::_prev_local_to_world` update timing. The MotionVector pass expects previous transform from the previous
rendered frame. If current scene update overwrites previous too early, fix the producer rather than compensating in shader.

Also test `CPU_DEFORM` path because MotionVector VS supports `position_prev` for deforming/skinned geometry.

### M2.6 - Camera cut

A camera cut should not produce huge motion that later temporal passes try to follow. If there is already a camera-cut signal,
force previous VP=current on the cut frame. If not, M3 will introduce a temporal-history invalidation path; for M2 at least
provide a way for MotionVector to output zero on the reset frame.

## Current Milestone

M2.

## Progress

Update `progress.md` after every validation case.

## Decision Log

- Bind scene depth for dynamic velocity.
- Keep motion jitter-free.
- Prefer ZWrite Off for velocity geometry.

## Surprises & Discoveries

If some dynamic material does not expose a `MotionVector` pass, record whether it is intentionally static, force-zero, or
unsupported. Do not silently fall back to camera motion for a moving frontmost object without documenting the limitation.

## Validation

### V1 - Static

TAA Camera jitter on, static scene/camera: motion near zero for 128 frames.

### V2 - Camera pan/rotation

Visualize RG motion. Direction must agree with actual history reprojection (`prev_uv = uv - motion`).

### V3 - Dynamic frontmost object

Move object laterally. Local object motion must override camera motion on its visible pixels.

### V4 - Occlusion

Move dynamic object behind a static wall. Wall pixels must keep camera/static velocity and never receive hidden-object velocity.
This is the key M2 gate.

### V5 - Alpha test

Alpha-test foliage/mesh MotionVector pass must clip consistently with GBuffer coverage.

### V6 - Jitter independence

Freeze all transforms and compare motion texture across Halton samples. Values should remain effectively zero.

## Acceptance Criteria

- [ ] Static scene motion is zero under changing jitter.
- [ ] Camera translation is correct.
- [ ] Camera rotation is correct.
- [ ] Dynamic object velocity is correct.
- [ ] Hidden dynamic geometry does not contaminate foreground velocity.
- [ ] MotionVector geometry no longer writes scene depth.
- [ ] First-frame/reset velocity does not explode.
- [ ] Alpha-test coverage is consistent enough for TAA.
- [ ] Legacy path is either fixed or explicitly deprecated.

Do not enter M3 until all pass.

## Known Follow-ups

Transparent/Sprite motion is M5. Velocity dilation is post-plan work.

## Outcomes

Write measured failure/pass cases to `progress.md`.

## Resume Protocol

Always inspect the MotionVector debug target directly. Do not use TAA ghosting as the primary diagnostic for M2.
