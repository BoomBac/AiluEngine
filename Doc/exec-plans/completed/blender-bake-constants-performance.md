# Blender constant-channel bake optimization

## Purpose and scope
Reduce proven zero-emission and constant scalar passes; retain native normal baking, paths, resolution and factors.
Compare fresh BMW27 and Classroom exports against the current batch exporter. Preserve unrelated dirty changes.

## Architecture constraints and relevant files
Plugin-system architecture; Blender RNA stays on its main thread. No new worker resource publication.
`Tools/Blender/ailu_live_link/material_bake.py`, `material_sync.py`, native performance and capture harnesses.

## Current milestone and progress
- M0: snapshot current sources and two fresh native baselines. Complete.
- M1: conservative constant analysis, deferred reconstruction and save/buffer phase diagnostics. Complete.
- M2: native pixel/lifetime regressions and two installed scene comparisons. Complete.
- M3: build, package/install, final comparison and state restoration. Complete.

## Decision log
Copy unchanged color EMIT target alpha into zero emission RGB to reproduce padding, retaining pass order.
Reconstruct primitive unlinked finite scalar defaults after the existing final coverage pass. Unknown/linked
scalar or nonzero emission falls back to native baking. Retain existing 2/255 native pixel comparison tolerance.
Keep independent PNG files and the existing transport contract for exact texture comparisons.

## Surprises and discoveries
The initial broader scalar/HDR reconstruction showed quantization differences and slightly different HDR
normalization factors. Nonzero HDR emission keeps native baking. Deferred scalar reconstruction retains the
original pass order and matches native primitive defaults within the existing pixel tolerance. The zero-only
intermediate `final` and reordered prototype `optimized` timings are excluded from the final performance claim.
The initial deferred version `constants` is also diagnostic only; final `validated` stages include guards for
alpha defaults near opacity thresholds and retain a single identical installed bake-module hash.

## Validation
Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderBakeConstantsPerf_20261012_01`.
Native reference pixel comparisons, cleanup/mode regressions, fresh Editor imports and contiguous captures.
Overall timing starts at Blender launch and ends at Editor End application; overlapping costs are not summed.

## Outcomes
Final source/ZIP/install hashes match. Bake module SHA256:
`6f37dd2cd4b091635858c91b0b5df920a2b3ec7615ef9feae3229db2455bcb75`.
Two old-source controls and two final installed runs use identical Editor/Engine/plugin binaries and source
blend hashes. Overall timing includes Blender startup and handshake, ending at Engine End application.

| Scene | Blender generation median | Overall median | Overall reduction |
| --- | --- | --- | --- |
| BMW27 | 2.833 -> 2.667s (5.87%) | 7.118 -> 6.965s | 2.15% |
| Classroom | 36.566 -> 32.750s (10.43%) | 38.193 -> 35.086s | 8.13% |

Classroom baseline overall range 38.032-38.354s; final range 33.225-36.947s. BMW baseline range
6.968-7.268s; final range 6.862-7.068s. BMW overall gain is small relative to runtime variation.
Classroom process CPU time median decreases 207.672 -> 173.773 CPU-seconds (16.32%). Its normalized
CPU utilization need not increase when redundant native work is removed; final average CPU cores are
4.76 and 5.92 on a 32-logical-CPU machine. BMW CPU-time readings vary and do not show a CPU-time gain.
No Python background threads or Engine texture-publication workers were introduced.

Native batch calls: BMW 45 -> 37; Classroom 845 -> 656. Classroom final phase medians are preparation
4.169s, graph setup 1.170s, Cycles bake 11.506s, pixel/cleanup 1.643s and save/publish 5.556s. PNG-specific
preparation takes 1.219s and encode/write 2.608s, included within save/publish. Source buffer diagnostics
release only one newly loaded image per scene; scan/free under 0.3ms does not explain wall cost and does
not isolate decoding within Cycles. Engine material Apply remains approximately 7.8-8.1s and includes
the existing synchronous decode/mip/resource publication. Blender/Engine pipeline costs overlap.

Each final run compares 962 textures and unchanged payload factors/bindings against the old source control.
955 textures match exactly; seven Classroom roughness images differ by at most 1/255. All normals,
emission, alpha/base and metallic outputs match exactly. Opacity threshold defaults keep native scalar baking.
Both scenes' captured GBuffer1 RGB matches exactly. Final RGB RMS is 0.0170-0.0192/255, P99 zero and
maximum 2/255, near baseline adjacent-frame noise. Three finite contiguous frames per resource per scene.
Representative Engine captures were visually inspected; Classroom's existing bright/clipped appearance is
also present in baseline. This verifies before/after equivalence, not Blender-to-Engine absolute visual fidelity.

Six native Blender regressions pass: batch/constants/spatial fallback/HDR/threshold/cleanup, scalar materials,
Diffuse/Glossy graph semantics, empty-instance error recovery, instance synchronization and bulk mesh export.
`cmake --build --preset build-debug` passes. Changed source line widths and Python syntax checked. Final
native runs restore all nine recorded runtime/project files byte-exactly and preserve source blend hashes.
Evidence: `final-report.json`, final stage texture/image comparisons, package verification and exact source
diffs in the session directory. Prototype/intermediate timings are retained but excluded from final claims.
