# M4 - Native RenderGraph TAA Integration

## Purpose

Convert the corrected TAA core into a real RG pass with explicit resource versions and persistent-history imports.

## Scope

- Implement `TAAExecutePass::OnRecordRenderGraph()`.
- Import persistent A/B history.
- Read current scene color, depth, motion, and previous history explicitly.
- Write current history/output explicitly.
- Feed the RG post-process chain.
- Make ping-pong state safe under RG execution.
- Remove dependence on legacy global textures and `EndPass()`.

## Non-goals

- No transparent reactive mask yet.
- No Cloud/RTGI fixes yet.
- No TAA algorithm rewrite.

## Architecture Constraints

Current RG scene-color chain:

```text
Forward/Sprite
    -> _color_target
CopyColor event 500
    -> _color_tex
TAA event 550
    -> must produce resolved color
PostprocessStack event 575
    -> reads _color_tex when valid
```

Therefore the least invasive first RG integration is:

```text
TAA reads current _color_tex
TAA writes persistent history write target
TAA replaces logical _color_tex with the TAA output handle for subsequent PostprocessStack registration
```

However `RenderingData` warns against rewriting `_color_target` because earlier pass execute callbacks access the final mutable
handle. Do not rewrite `_color_target` in M4. Only update the post-process input chain (`_color_tex` or a new explicit TAA color
handle) after confirming earlier callbacks do not consume that field at execution time.

## Baseline

`TAAExecutePass` inherits the default empty `RenderPass::OnRecordRenderGraph()`. In RG mode the legacy `Execute()` is not called.

`PostprocessStack::OnRecordRenderGraph()` chooses:

```cpp
RDG::RGHandle color = rendering_data._rg_handles._color_tex.IsValid()
    ? rendering_data._rg_handles._color_tex
    : rendering_data._rg_handles._color_target;
```

This provides a clean insertion point for TAA.

## Target Outcome

RG graph has explicit edges:

```text
CopyColor -> TAA <- MotionVector
                 <- DepthCopy
                 <- HistoryPrev(import)
TAA -> HistoryCurrent(import/write)
TAA -> PostprocessStack
```

No legacy global state is required for TAA inputs.

## Relevant Files

```text
Engine/Inc/Render/Features/TemporalAA.h
Engine/Src/Render/Features/TemporalAA.cpp
Engine/Inc/Render/RenderingData.h
Engine/Src/Render/Renderer.cpp
Engine/Src/Render/Features/PostprocessStack.cpp
Engine/Src/Render/RenderGraph/RenderGraph.cpp
Engine/Inc/Render/RenderGraph/RenderGraph.h
```

## Milestones

### M4.1 - Add RG method declaration/implementation

Declare:

```cpp
void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) override;
```

in `TAAExecutePass`.

Do not copy the legacy `Execute()` body directly. RG setup and execution state must be separated.

### M4.2 - Prepare per-camera history before AddPass

At record time:

1. Fetch/create `TAAHistoryState` by camera hash.
2. Detect resolution/history invalidation using M3 rules.
3. Ensure A/B textures exist with camera color descriptor and `_is_random_access = true`.
4. Select stable pointers:

```text
history_read_texture
history_write_texture
```

Do not flip `_write_a` yet.

Import inside builder or before pass using RG Import. Existing engine patterns permit both `graph.Import()` and
`builder.Import()`.

### M4.3 - Capture handles locally

Because RG callbacks execute later and `RenderingData::_rg_handles` is mutable during recording, do not depend on reading a
field in the execute callback if that field is going to be reassigned after setup.

Before `AddPass`, copy current inputs:

```cpp
const RDG::RGHandle current_color = rendering_data._rg_handles._color_tex;
const RDG::RGHandle depth = rendering_data._rg_handles._depth_tex;
const RDG::RGHandle motion = rendering_data._rg_handles._motion_vector_tex;
```

Inside setup, import/select history handles and declare:

```text
Read(current_color, SRV)
Read(depth, SRV)
Read(motion, SRV)
Read(history_read, SRV) only if history valid or still bind dummy safely
Write(history_write, UAV)
```

Store resulting handles as pass-local captured values/member values that cannot be confused by later handle reassignment.

### M4.4 - Execute compute TAA with RG-resolved resources

Execution callback resolves and explicitly binds:

```text
_CurrentColor        <- current_color
_CameraDepthTexture  <- depth
_MotionVectorTexture <- motion
_HistoryColor        <- history_read
_CurrentTarget       <- history_write
```

Set constants:

```text
_SourceTex_TexelSize
_Jitter(prev,current)
_HistoryValid
_ClampQuality
_HistoryQuality
_MotionQuality
_VarianceClampScale
_Sharpness
_HistoryFactor
```

Dispatch using exact camera dimensions.

Do not call `cmd->Blit()` to a second TAA output unless required by format restrictions. The history-write target can also be
the current TAA output.

### M4.5 - Feed postprocess chain

After setup returns the write handle/version, assign the logical postprocess input:

```cpp
rendering_data._rg_handles._color_tex = taa_output_handle;
```

Only do this after verifying `CopyColor` execution captured its own target handles safely. `CopyColorPass` currently uses the
`data._rg_handles` fields during execute, so specifically check whether mutating `_color_tex` later would cause CopyColor to
write the TAA handle rather than its original transient color texture.

If it does, do **not** mutate `_color_tex` globally. Instead add an explicit field, for example:

```cpp
RDG::RGHandle _taa_color;
```

and update `PostprocessStack` selection order:

```text
_taa_color if valid
else _color_tex if valid
else _color_target
```

This is the safer design if callbacks reference mutable global handles. Prefer correctness over minimizing one field.

### M4.6 - Ping-pong commit in execute callback

RG does not call legacy `BeginPass/Execute/EndPass` methods. Do not rely on:

```cpp
TAAExecutePass::EndPass()
```

to flip A/B.

Follow the Cloud RG pattern: only after a successful TAA dispatch/output write, commit:

```text
history_valid = true
previous_uv_jitter = current_uv_jitter
last_render_frame = current frame
write_a = !write_a
```

Be careful about capturing a reference to an unordered/map element across operations that could rehash. Either ensure the map
is not mutated between record and execute or identify state by camera hash again in the callback.

### M4.7 - History invalid frame in RG

When history is invalid, still declare/write the current history target and bind either:

- the read history safely but shader ignores it because `_HistoryValid=0`, or
- a valid dummy current/history texture.

Never leave an unbound SRV if the descriptor system can retain an old binding.

### M4.8 - Remove legacy RG assumptions

After RG TAA passes validation:

- `_postprocess_input` remains legacy-only and should not control RG TAA.
- RG path must not use global `_MotionVectorTexture` or `_CameraDepthTexture` for TAA.
- Legacy `EndPass()` history flip must not also run for RG.

Keep legacy path only if still supported by renderer config.

### M4.9 - Enable the TAA feature

Only now add TAA to the feature list/quality setting for RG cameras. Ensure activation also matches camera anti-aliasing mode:

```text
TemporalAA feature active AND camera AA == TAA
```

or centralize policy so those states cannot drift.

Do not allow camera jitter without TAA resolve in normal shipping configuration, except deliberate debug mode.

## Current Milestone

M4.

## Progress

Update after each RG graph/capture validation.

## Decision Log

- Do not mutate `_color_target` in M4.
- Use explicit persistent history imports.
- Flip temporal state only after execution.
- Add a dedicated `_taa_color` handle if mutable `_color_tex` would corrupt earlier pass callbacks.

## Surprises & Discoveries

The biggest RG risk is not barriers; it is handle-version aliasing caused by reading mutable `RenderingData::_rg_handles` in
later execute callbacks. If a pass needs the version from record time, capture that handle locally.

## Validation

### V1 - Graph inspection

Expected logical dependencies:

```text
CopyColor -> TAA
MotionVector -> TAA
CopyDepth -> TAA
HistoryPrev -> TAA -> HistoryCurrent
TAA -> Bloom/ToneMapping
```

### V2 - PIX/RenderDoc resource binding

Inspect one TAA dispatch:

- CurrentColor is current scene color.
- HistoryColor is previous A/B side.
- CurrentTarget is opposite A/B side.
- MotionVector is current frame version.
- CameraDepth is current frame copied depth.

### V3 - Version correctness

Confirm CopyColor still writes its own intended `_color_tex`, not the later TAA output due to mutable handle reassignment.

### V4 - History flip

Frame N write A/read B; frame N+1 write B/read A. Resize/cut resets validity without double flipping.

### V5 - Postprocess

Bloom/ToneMapping consume TAA resolved color. Gizmo/editor overlays maintain their intended order.

## Acceptance Criteria

- [x] `TAAExecutePass::OnRecordRenderGraph()` exists and runs.
- [x] Current color is explicit RG read.
- [x] Depth is explicit RG read.
- [x] Motion is explicit RG read.
- [x] Previous history is an imported RG read.
- [x] Current history/output is an imported RG UAV write.
- [x] No TAA input depends on legacy global texture state.
- [x] Ping-pong flips only after execute.
- [x] CopyColor handle/version is not corrupted by TAA registration.
- [x] Postprocess consumes TAA output.
- [x] Resize/toggle/cut behavior from M3 still passes in RG.
- [x] GPU capture shows no read/write resource hazard.

### Acceptance evidence - 2026-10-02

M4 core/native RG automated acceptance passed. Evidence combines the real Editor scene/postprocess path, an isolated
production `OnRecordRenderGraph()` GPU fixture, and two replayed RenderDoc captures. This does not close M3's previously
recorded real-scene thin-rail convergence and complete feature off/on acceptance gaps or claim interactive visual acceptance.

- Full `cmake --build --preset build-debug` passed; final GPU fixture changes rebuilt with the same preset/target.
- Fresh Editor PID 49352, NewScene, 1088x576: 12 completed sessions, 168 DDS + 8 PNG, each sequence continuous.
  TAA current SRV and CopyColor output resolve to the same physical color texture; BloomDownsample_0 input resolves to
  alternating TAA A/B textures. Depth/motion/history inputs and ToneMapping output were also captured. Camera AA off/on,
  a 5-unit cut and return captured successfully; original pose/AA/features and scene revisions 40/39 were restored.
- Isolated native RG: 85 same-frame current/output pairs; 64 consecutive static frames and 64 Camera jitter values.
  First use, invalidate helper, translation/orientation/FOV cuts, render gap, six resize frames and all nine factor=0 samples
  resolve exactly to current RGB. Resize is covered by this fixture; the Editor viewport was not resized.
  The isolated toggle calls the invalidation helper; live Editor camera AA off/on provides additional smoke evidence,
  not complete feature activation/first-frame off/on acceptance.
- RenderDoc frame140/frame141, event358: four SRVs and one UAV, dispatch 40x23x1 for 320x180. History A/output B becomes
  history B/output A, with disjoint read/write resource IDs; both captures replayed and contain zero debug messages.
  Native fixture D3D12 ERROR/CORRUPTION=0; Editor logs contain zero severity 0/1 messages. Existing severity 2 warnings remain.
- No production implementation or shader changed during this acceptance. Initial isolated RG attempts assigned the first
  color resource reserved handle 0 and did not run TAA; excluded. Final fixture reserves the initial handle to match
  Renderer resource preallocation. Generic RenderGraph handle-zero allocation remains a separate follow-up.
- Report: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M4_Acceptance_20261002/report.md`.
  Evidence directories: `run1/`, `isolated_rg3/`, `renderdoc_rg/`; restoration hashes in `restoration.json`.

M4 completion means opaque/core TAA is functional in RG. M5-M7 handle feature-specific temporal compatibility.

## Known Follow-ups

Potential future `TemporalHistoryRegistry` can centralize history imports and resets.

## Outcomes

Record final RG handle flow in `progress.md`.

## Resume Protocol

If output is wrong, first inspect actual resolved resources/versions in the TAA dispatch before editing shader math.
