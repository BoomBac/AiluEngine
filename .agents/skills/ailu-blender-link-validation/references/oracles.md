# Deterministic validation and verdict rules

## Levels
L0 static/build; L1 wire/protocol; L2 imported scene structure vs **independent** bpy source reference; L3 GPU GBuffer/projection/render; L4 incremental/reconnect/persistence and performance. A pass applies only to the listed levels. Exact packet replay tests the Engine decoder/import, NOT Blender-to-Engine data equivalence.

## Hard assertions
Correct frame header/length/sequence; exactly one Begin and End; sender queue drained; actual receiver applied End; zero Engine Apply failed, overflow, rejection, disconnect; no duplicate GUID or missing expected supported entity; correct effective parent/child and material slot ownership; no unjustified reexports on idle; object deletions and expected GUID/revision rules; source hash unaffected.

## Numeric assertions
Default *proposal for controlled fixtures* (calibrate before acceptance): world position max absolute error <= 1e-4 world units; UV <= 1e-5; relative scale <= 1e-4; quaternion comparison via `abs(dot)` with angle error <= 1e-3 degrees; projected landmarks <= 0.5 px. Large scenes need both absolute+relative tolerances. Build an independent coordinate/projection computation for oracle, not the same code as the exporter. Track unsupported features as explicit SKIPPED/INCONCLUSIVE, not hidden passing data.

## Visual assertions
First ensure capture integrity (completed status, correct resource, format, dimensions, finite data, ordered capture_index and consecutive engine_frame). For synthetic cases: occupancy/silhouette, projected landmarks, normal orientation/values, UV/texture color marker and alpha clip/blend semantic test. For real scenes use recognizable ROI/geometry and a versioned baseline with ambient/temporal noise controls. Report RMS/P99/SSIM as diagnostics, not proof by themselves. A physically different shading model prevents universal Cycles-vs-Engine pixel equality.

## Performance
Only compare identical packet SHA256 for strict replay and identical scene, binary/graphics settings, hardware, resolution and camera for selected tests. Separate Blender generation and bake, wire/drain, Engine receive/queue/apply, GPU capture overhead. Repeated-run median; thresholds warn before they become hard gates.

## Machine-readable statuses
PASS: every required assertion at declared level has evidence. FAIL: violated assertion, missing End, crash, timeout or failed cleanup. INCONCLUSIVE: expected property lacks independent observable, baseline unstable, GPU/unavailable oracle. SKIPPED: explicitly out of suite/supported features, with reason. Save every failure as `{case_id,layer,subject_id,property,expected,actual,tolerance,artifact_paths,message}`. No automatic golden replacement.

## Evaluated material invariant
Mesh triangle material indices and sparse evaluated material tables share one index space, with explicit OBJECT overrides. Unexpected Engine checker fallback is FAIL. A used source null slot is INCONCLUSIVE. Pair binding checks with controlled GBuffer color/UV markers; see [material-bindings.md](material-bindings.md).
