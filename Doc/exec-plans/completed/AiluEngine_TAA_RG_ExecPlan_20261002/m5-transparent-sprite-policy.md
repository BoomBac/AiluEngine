# M5 - Transparent, Billboard, and Sprite Temporal Policy

## Purpose

Define and implement a safe first policy for color rendered before TAA but lacking complete per-pixel velocity/reactive data.

## Scope

- Forward blended transparency.
- Sprite pass.
- Billboard special projection path.
- Alpha-test distinction.
- Decide supported first-release behavior and document known limitations.

## Non-goals

- A production-grade reactive mask for every material type is not mandatory for the first implementation.
- No broad scene-color-chain refactor unless required by the chosen policy.

## Architecture Constraints

Current order is:

```text
MotionVector 450
Sprite 460
Forward transparent 475
CopyColor 500
TAA 550
```

Therefore both Sprite and blended transparent color enter TAA, while they generally do not provide their own motion vector.
The underlying opaque depth/motion is what TAA sees.

Alpha-test/cutout opaque geometry is different: it already participates in the deferred MotionVector pass and should remain in
the normal TAA path.

## Baseline

`ForwardPass` is named `TransparentPass` and renders transparent queues. `SpriteRenderPass` writes into `_color_target` with alpha
blending before CopyColor.

`billboard.hlsl` manually attempts to remove camera jitter from `_MatrixP`, so billboard raster sample semantics can differ from
normal jittered scene geometry.

## Target Outcome

The engine has an explicit documented behavior for blended transparency/Sprites rather than accidental reuse of opaque motion.
Moving transparent content should not produce severe multi-frame smears in the default supported path.

## Relevant Files

```text
Engine/Src/Render/Features/CommonPasses.cpp
Engine/Src/Render/2D/SpriteRenderPass.cpp
Engine/Src/Render/2D/SpriteRenderFeature.cpp
Engine/Res/Shaders/hlsl/forwardlit.hlsl
Engine/Res/Shaders/hlsl/billboard.hlsl
Engine/Res/Shaders/hlsl/Compute/taa.hlsl
```

## Milestones

### M5.1 - Classify material types before changing order

Create three categories:

```text
Opaque / alpha-test
    has scene depth and MotionVector support
    stays in core TAA path

Blended world transparency
    usually no depth write / no velocity

Sprite / billboard
    usually blended and may intentionally use stable projection
```

Do not move alpha-test geometry out of TAA just because the shader contains alpha clipping.

### M5.2 - First supported policy

For this milestone, prefer the least invasive policy that can be implemented and validated in the current color-chain design.
There are two acceptable paths. Choose one and record it in `progress.md`.

#### Policy A - Keep blended content before TAA with explicit history rejection

Use if the engine can cheaply produce a reactive/responsive mask.

Requirements:

- Create a one-channel RG reactive mask initialized to 0.
- Transparent/Sprite coverage writes a high reactive value.
- TAA reads the mask and reduces/rejects history for those pixels.
- The mask is an explicit RG dependency.

This is architecturally preferred long-term but may require material/render-target plumbing.

#### Policy B - Declare blended temporal support limited for first core release

Use if reactive-mask plumbing would explode scope.

Requirements:

- Keep current order.
- Preserve strong neighborhood clipping.
- Validate common transparent motion and record exact artifact level.
- Do not claim transparent TAA is solved.
- Keep M5 incomplete for shipping-quality acceptance, but allow M6/M7 development to proceed if the user explicitly accepts
  this limitation.

Do not use a hidden workaround such as globally lowering `_HistoryFactor` for all pixels.

### M5.3 - Do not casually move TAA before transparency

A post-TAA transparent policy sounds simple but current RG callbacks use mutable `_rg_handles._color_target`; TAA cannot safely
replace `_color_target` without auditing every earlier/later callback. If selecting post-TAA transparency, first design an
explicit scene-color chain handle and make passes capture record-time handles. Treat that as a separate subproject, not a one-line
event change.

### M5.4 - Audit billboard jitter removal

`billboard.hlsl` copies `_MatrixP` and removes jitter. Verify intended behavior:

- if billboard is blended and intentionally responsive/post-temporal, stable projection may be desirable;
- if billboard is supposed to participate in TAA like world geometry, it should use the same jittered sample convention.

Do not change the math until the transparent policy is chosen.

### M5.5 - Sprite motion test

Move a Sprite laterally over a high-contrast background and capture:

```text
current frame
motion vector
TAA history UV
TAA final
```

This proves whether the selected policy actually handles the known conflict.

## Current Milestone

M5.

## Progress

Policy A selected during the 2026-10-03 repair: keep ordering, produce explicit responsive coverage from the composed
color/pre-blend snapshot difference, and reject history on current and departed coverage. The original Policy B result
below is retained as failed historical evidence. Final automated repair/retest results are recorded in `progress.md`.

## Decision Log

No universal transparent solution is assumed. Correctly scope the first supported behavior.

## Surprises & Discoveries

If Forward transparent materials already have a usable velocity/stencil/reactive channel, prefer reusing it over adding a new
system.

## Validation

- Static transparent object: stable.
- Moving transparent object over detailed background.
- Moving Sprite.
- Billboard under TAA jitter.
- Alpha-test foliage remains temporally stable and still writes motion.

## Acceptance Criteria

For full M5 completion:

- [x] Alpha-test is correctly classified and remains in core TAA path.
- [x] Blended transparency behavior is explicit, not accidental.
- [x] Sprite behavior is explicit.
- [x] Billboard jitter behavior matches the chosen policy (captured perspective and orthographic raster stability).
- [x] Moving blended content does not produce severe persistent smear under the supported policy (automated fixture).
- [x] No global history-weight workaround was used.

If only Policy B is accepted temporarily, mark M5 as PARTIAL in `progress.md` rather than PASS.

## Known Follow-ups

Transparent velocity, broader material coverage, interactive visual review and GPU-cost measurement remain follow-ups.
The response mask detects color changes above0.001 HDR and bypasses temporal accumulation on affected coverage.

## Outcomes

Final repair supersedes the original failure below: Policy A automated acceptance passes across seven cases,
784 consecutive frames and3136 native DDS. Current/departed responsive coverage returns exact current RGB; perspective
and orthographic billboard raster stability pass; colored cutout retains deferred motion and passes chroma rejection.
M3 static/detail/controls and independent core-RG regressions also pass. Final evidence is
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M5_RepairFinal_20261003/`, with companion M3Final/CoreFinal runs.
See `progress.md` for numerical results, intermediate regression failures, sampler precision and restoration evidence.
Interactive Editor review, broad/multi-camera coverage and GPU-cost profiling remain separate.

### Original Policy B acceptance (historical failure)

Automated production-pass acceptance: 448 consecutive frames, 1344 native DDS, Debug build passed, process exit0,
D3D12 ERROR/CORRUPTION0. Evidence:
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/TAA_M5_Acceptance_20261003_final/`.

| Case | Result |
| --- | --- |
| Static blend/Sprite/cutout | TAA reduces measured adjacent-frame ROI RMS; local automated stability evidence |
| Moving blend | MotionXY0; trails exceed0.03 HDR red excess outside5x5 footprint; after hiding persist24 frames |
| Moving production Sprite | MotionXY0; same failure, persists31 frames; matched current/motion/derived historyUV/final retained |
| Billboard shader with per-object binding | All32 static perspective-jitter frames retain identical50x50 bounds; scoped raster PASS |
| Alpha-test checker coverage | Remains deferred/core TAA; intended3-pixel velocity measured2.998,100% covered pixels moving |
| Colored alpha-test disocclusion | Persistent colored residual despite valid object velocity; fails chroma rejection, at least32 frames |

No full M5 PASS. Policy B exposes limited blended support, and the newly measured chromatic-disocclusion case also limits
the core clipping path. Static/core velocity acceptance must not be substituted for a ghost-free motion claim.

The billboard test uses a dedicated pre-TAA RG test pass with the production shader and the same per-object `DrawMesh`
binding used by GizmoPass. Actual Editor icon billboards use GizmoPass after postprocess and write FinalColorHandle;
they are a separate path. Orthographic/multi-camera/interactive billboard gates remain unverified. Grayscale billboard
temporal smear is not quantified by the red-channel gate, so the billboard checklist item is not fully closed.

The Sprite historyUV visualization is CPU-derived from captured zero velocity and actual Camera jitter; it is not a GPU
historyUV debug capture. Capture integrity and image review passed; interactive Editor visual acceptance is not claimed.
See `progress.md` for thresholds, rejected fixture runs, reproduction tools and restoration evidence.

## Resume Protocol

Do not alter pass ordering until the RG color-chain consequences have been audited.
