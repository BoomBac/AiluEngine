# Blender bake batching and Full Sync performance

## Purpose and scope
Further reduce native Live Link wall time after scalar packing. Compare BMW27 and Classroom against the
current installed version. Preserve resolution, sampling, material bindings, atlas isolation and source scenes.

## Architecture constraints and relevant files
`Doc/architecture/plugin-system.md`, `asset-system.md`, `threading.md`;
`Tools/Blender/ailu_live_link/material_bake.py`, `material_sync.py`;
`Plugins/BlenderLiveLink/Src/BlenderLiveLinkPlugin.cpp`; existing native performance and capture harnesses.
Blender RNA and baking stay on its main thread. Engine workers must not publish Scene/Material/GPU state.

## Current milestone and progress
- M0: fresh baseline and exact source backup. Complete.
- M1: bounded object-level material batch baking and phase instrumentation; channel/lifetime regressions. Complete.
- M2: native performance, texture and image comparison; inspect Engine decode boundary and scheduling. Complete.
- M3: final build, package/install and runtime validation. Complete.

## Decision log
Group uncached surface materials by resolution. Bound simultaneous material targets to prevent memory growth.
Keep single-material export compatible. Do not introduce unsafe background Blender API calls.

## Validation
Evidence beside selected Editor.exe:
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderBakeBatchPerf_20261012_01`.
Factory-startup Blender, fresh Editor, unchanged scenes, exact state restoration; texture binding identities and
continuous GBuffer/final captures compared independently from timing.

## Surprises and discoveries
Engine PNG parsing currently creates Texture2D after CPU decoding/mipmap generation; ResourceMgr immediately
calls Apply. Moving this entry point to workers would cross resource/GPU lifetime boundaries. This task keeps
ownership unchanged and isolates Full Sync scheduling with exact-packet 4ms/32ms controls.
Full Sync uses a 32ms between-command budget; incremental updates retain 4ms. Begin still yields to display
progress, End/disconnect/overflow/unload semantics are unchanged. Individual Apply calls remain non-interruptible;
32ms is a batch scheduling target, not a bound on worst-case Editor latency. This intentionally spends more
main-thread time on bulk synchronization. No new Python threads or Engine resource-publication workers are added.
Batch fixture reduces 15 bake calls to 5 and retains native pixels, bump normals, alpha, duplicate slots and
resolution groups. Real Classroom batch calls: 955 -> 845. Phase sums: prepare 3.61s, graph setup 1.27s,
Cycles bake 12.82s, pixel/cleanup 1.89s, save/publish 5.30s. These omit enclosing source-image/depsgraph work.
One post-test CopyFile2 restoration hit WinError 1224 after both owned processes exited. All nine files were
restored and byte-verified after the transient mapping lock cleared; the harness now retries this error for 15s.

## Outcomes
Native Blender-only batch comparison: BMW generation 3.062 -> 2.815s; Classroom 37.177 -> 32.059s.
962 exported textures match exactly, including material factors and binding-owner identities. Engine binary
unchanged for this first pair. GBuffer/final captured; final analysis stored in `batch-comparison.json`.
Full Sync native Classroom still 49.645s after batching, so queue scheduling is the next isolated factor.
Strict identical-packet replay, two fresh Editor trials per setting, same new DLL with environment selecting
4ms or 32ms: BMW median 4.494 -> 3.554s (20.91% reduction); Classroom 46.688 -> 25.769s (44.81%).
GBuffer matches exactly; final RMS 0.0168/255 BMW and 0.0189/255 Classroom, P99 zero, maximum 2/255.
All selected captures have three contiguous finite 1152x640 frames. No apply failures or queue overflow.
Debug build passes. ZIP source hashes match and installed bake/material/timing modules are backed up and updated.
Second baseline uses both saved source modules with the new DLL constrained to 4ms; it records process CPU time.

Final native controls: old version two trials; optimized installed version three trials, including the final
package's Object Mode guard regression fix. Object Mode behavior is identical across all timed exports.

| Scene | Blender generation median | Reduction | Overall test median | Reduction |
| --- | --- | --- | --- | --- |
| BMW27 | 3.116 -> 2.812s | 9.76% | 8.582 -> 7.165s | 16.51% |
| Classroom | 37.540 -> 31.724s | 15.49% | 54.999 -> 34.044s | 38.10% |

Overall test time runs from Blender process launch to Editor applying End, including startup and handshake.
The Engine log's narrower interval runs from Begin application to End application: BMW median 4.833 -> 3.578s;
Classroom 51.347 -> 30.509s. `receive_ms` is cumulative per-packet receive time, not a receive-span timestamp;
queue wait is summed per packet. These values overlap and must not be added as wall-clock stages.
Classroom optimized overall range is 31.668–39.270s; generation range is 29.475–37.641s. Report all three
trials and the median rather than the fastest run. Slower repeat costs rose in preparation, bake, cleanup
and saving; the log does not identify a unique external cause. Strict replay scheduling gains remain stable.
Blender process CPU time covers all native worker threads. Classroom averages 5.18–7.41 logical cores during
optimized generation on 32 logical CPUs; the measured old repeat averages 6.53. This task improves latency
without claiming full CPU utilization or a new multithreaded decoder. Further CPU decode/mipmap parallelism
requires separating CPU output from texture creation/Apply; PNG serialization and repeated bake setup remain targets.

Final installed bake SHA256: `cc87595f29d440c8b0df42168e284f09535833ac30983dfa7a3fa4da3d70664d`.
Final source, ZIP and installed modules match. Final native scene textures: BMW 51 and Classroom 911 all
exactly match the baseline's consumed channels; scalar factors and material binding identities also match.
Final GBuffer matches exactly. Final RMS BMW 0.01975/255 and Classroom 0.01863/255, P99 zero, maximum 2/255;
stationary baseline noise is approximately 0.0187/255. All captures are finite and contiguous. Representative
captures were inspected. This validates before/after views and does not claim Blender-render equivalence or
interactive user acceptance of existing shading/exposure. No quality, resolution or sampling reduction.
Batch, single-material scalar/alpha, empty-instance/failure recovery, instance sync and mesh bulk regressions pass;
cached export still rejects Edit Mode without changing the source mode, and injected batch errors restore all
temporary datablocks, scene/view layer and bake suppression. Debug build and targeted diff checks pass.
All eight benchmark stages restore nine state files and retain original blend hashes. One batch summary was
recovered after the transient restore lock, explicitly marked in its manifest. Unrelated dirty changes remain intact.

Evidence: `aggregate.json`, `batch-smoke.json`, `scalar-comparison.json`, `*-comparison.json`,
`*-texture-comparison.json`, `build-debug.log`, native/replay stage logs and capture manifests,
`material_bake.py.patch`, `material_sync.py.patch`, `source_before/`, `installation/`, `installation_final/`.
