# Blender bake pipeline experiments

## Purpose / scope
Test FullSync temporary-scene reuse, GPU baking and independent-process PNG encoding separately on BMW27
and Classroom. Preserve resolution/sampling, source scenes and the existing material transport contract.
Do not add geometry cache architecture or multi-object batching until these simpler factors are measured.

## Architecture constraints / relevant files
Plugin-system architecture, Blender material_bake/object_sync and existing native performance harnesses.
All Blender RNA and bake calls stay on the main thread. Session resources have explicit finally cleanup.
No Editor or Engine implementation changes; worker encodes only copied RGBA bytes.

## Current milestone / progress
- M0: source/package backup, device probe and two fresh controls. Complete.
- M1: scoped scene reuse, phase instrumentation and context optimization. Complete.
- M2: isolated GPU/PNG/depsgraph experiments with texture/frame checks. Complete.
- M3: retain measured defaults, package/install and final repeated validation. Complete.

## Decision log
Use environment-controlled diagnostic factors before changing defaults. Compare identical Engine binaries.
GPU probe reports NVIDIA RTX 5070 CUDA/OPTIX and AMD HIP; use the NVIDIA device alone for GPU experiments.

## Surprises / discoveries
The first instrumented control failed before export due to a corrected indentation error; all state restored.
Direct temp_override alone cannot supply all mesh Edit Mode operators. Preserve window switching for UV atlas
projection; remove it for Object Mode bake calls only. Failed first attempt retained under `context`.
Classroom initial control includes 3.084s repeated source depsgraph acquisition and 3.789s context restoration.
Scene reuse alone changes export 30.749 -> 30.547s, insufficient evidence for a default switch. Object bake
context-only override reduces restoration to 0.631s; export 27.582s with unchanged textures and frames.
NVIDIA CUDA baking is slower: BMW 11.243s, Classroom 157.351s. GPU material parameters fail strict equality
for Plate; do not enable by default. OPTIX is detected but not tested. Pure-copy PNG process matches native
RGBA8 rounding, color transform, alpha and row orientation in the smoke; actual scene texture comparison passes.
PNG alone exports Classroom in 28.205s; fixed-snapshot depsgraph reuse alone takes 27.798s. Combined CPU
configuration exports in 24.781s, overall 32.301s. These are exploratory single trials, not final medians.

## Validation
Evidence beside selected Editor.exe:
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderBakePipelinePerf_20261012_01`.
Native pixel comparison and failure cleanup precede real imports. Separate throughput, transport, numerical
and visual checks. Restore recorded runtime/project files and source blend hashes after every run.

## Outcomes
Default configuration retains CPU Cycles, the existing per-batch temporary scenes, fixed-snapshot depsgraph
reuse, Object Mode context override and automatic independent PNG encoding on supported Windows installs.
Scene reuse and GPU selection remain opt-in diagnostic factors. This task does not implement multi-object
baking, persistent geometry/material cache architecture or additional Engine worker publication.

Two old-version controls and two final installed runs use identical Editor/Engine/plugin binary hashes,
source blend hashes and capture configuration. First control adds instrumentation without behavior changes;
second loads saved pre-task bake/material modules. Final bake source SHA256:
`4b855fbf18a6027a406f35fcf3cbc9f50717eaa82fb615dc19644856e66ec63a`.
Source, ZIP and installed bake/material/object/encoder/worker hashes match. Preferences are not saved.

| Scene | Blender generation median | Overall median | Overall reduction |
| --- | --- | --- | --- |
| BMW27 | 2.858 -> 2.719s (4.86%) | 7.776 -> 7.268s | 6.53% |
| Classroom | 31.560 -> 25.039s (20.66%) | 35.637 -> 33.301s | 6.56% |

Overall timing runs from Blender launch to Engine End application, including startup/handshake and overlapping
export/import. Classroom final generation range 24.333-25.744s; overall 31.895-34.706s versus old overall
35.108-36.167s. BMW old overall 7.169-8.382s versus final 6.966-7.570s; its small gain overlaps run variation.
Do not extrapolate these two-trial medians to other scenes, GPU backends or cold disk/kernel behavior.
Classroom Engine actual Apply median remains 9.149s (old 9.669s). Faster Blender generation increases queued
work toward the tail, so its 20.66% export reduction produces only a 6.56% overall reduction. No Engine code
or FullSync scheduling change is included. Parent process CPU metrics exclude the encoder process.

Single-factor Classroom export results, exploratory one trial each: baseline 30.749s; scene reuse 30.547s;
context-only bake 27.582s; depsgraph reuse 27.798s; PNG process 28.205s; CUDA 157.351s. The combined source
trial exports in 24.781s. GPU strict material comparison fails at Plate, even though its Engine import/captures
complete; this is not a quality PASS. CPU factors and final runs pass strict material parameters/bindings.

Each final run compares all 962 consumed textures. Final repeat matches all exactly; first final has one
normal PNG differing by at most 1/255 (RMS 0.00648/255), with all other textures exact. GBuffer1 RGB matches
exactly in both scenes and runs. Final RGB RMS 0.0173-0.0192/255, P99 zero, max 2/255, near baseline frame
noise. Three finite contiguous 1152x640 frames per selected resource. Representative final captures visually
reviewed; existing bright/clipped Classroom output persists in the control. This establishes before/after
equivalence, not absolute Blender render fidelity.

Seven Blender regressions pass: process/scratch-session cleanup including killed worker and cache invalidation,
native batch/constants/spatial/HDR/opacity tests, scalar materials, Diffuse/Glossy semantics, empty-instance error
recovery, instance synchronization and bulk meshes. Random HDR/alpha PNG smoke matches native bytes exactly.
CUDA device smoke restores preferences/context. Debug build passes. Final native exports verify scene/view
layer and object/mesh/material/image/scene datablock counts, in addition to original scene file hashes. All
recorded runtime/project files restored byte-exactly; owned Editor/Blender/encoder processes are reaped.
Evidence includes pipeline-report.json, final-medians.json, stage texture/frame comparisons, backups and diffs.
