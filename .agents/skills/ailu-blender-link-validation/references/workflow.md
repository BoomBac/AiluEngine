# End-to-end workflow (runner implementation specification)

## 1. Preflight
Inspect original worktree and user processes. Resolve paths from CLI/environment instead of embedded D:/F:/I: paths. Resolve actual debug Editor.exe, Engine DLL and plugin DLL; Blender version, original `.blend`, disposable Editor project, selected localhost port, output beside Editor.exe. Hash source and binaries, check scene permissions, acquire exclusive machine/GPU/config lock, and fail closed if user processes or shared state would be affected. Store `run.json` and process ownership. A dry-run must cause no changes.

## 2. Start fresh Engine and Blender
Reuse lifecycle and backups from `Test/BlenderLinkPerformance.py` after refactoring machine-specific paths; do not create a parallel process controller. Start owned Editor process with unique port; wait for its **new** listening log and RPC session; verify `editor.get_state` matches disposable project. Start Blender, e.g. `blender --background --factory-startup <scene.blend> --python-exit-code 1 --python <repo>/Test/BlenderLinkPerformanceExport.py -- <run-dir>`. This current exporter sends production `object_sync.full_sync` packets directly from Blender main thread and records `packets.bin`; it does **not** fully exercise add-on registration/GUI workflow. Add a fresh-process installed-add-on case separately.

## 3. Gate on actual full completion
Correlate sender export manifest + one protocol FullSyncBegin/FullSyncEnd, drained wire queue, Editor log current-process FullSync application, no Apply failures/queue overflow/disconnect. A mere `FullSync import:` text or elapsed sleep is insufficient if stale or from a different session; tie to current run by new log offset/PID/session. Check Engine is alive; bound all waits. No End is FAIL.

## 4. Independent ground truth
Before or separately from export, run read-only Blender reference extraction using evaluated depsgraph (including evaluated collection instances) with original source IDs/name paths, expected hierarchy/world matrices, source GUID mapping, triangulated mesh summaries/material slots, UV/bounds, supported light/camera/material fields and visibility. Build independent transform/camera/UV expectations instead of calling production conversion helpers for the property under test. Preserve scene/datablocks and compare original `.blend` hash after.

## 5. Read actual imported Engine state
Call `scene.query_entities({offset,limit})` until `has_more=false`, then `scene.inspect_entity({entity_guid,include_properties:true})` for GUIDs or anomalies. Compare linked subset only; note that unrelated Editor scene entities are valid. Check `parent_guid` AFTER End, not while hierarchy packets are in flight. The API may not expose full mesh internals: report INCONCLUSIVE for missing fields or add an explicitly read-only bulk snapshot endpoint only when justified by evidence/performance.

## 6. GPU capture
Reuse `ailu-render-frame-validation`: identify current pass/resource, control camera and AA, settle, capture 3+ consecutive frames with `render.capture_pass` and `render.capture_status`, verify manifest capture_index and contiguous engine_frame, inspect GBuffer (in existing scripts `DeferedGeometryPass` `_GBuffer1`) and final output (`TransparentPass`) only when selectors actually exist. Match camera, aspect, exposure and color semantics; restore camera/properties in finally. Do not equate exact final Blender Cycles RGB with Ailu Standard Lit.

## 7. State transitions
On Blender **main thread**, apply one edit after a passing baseline: transform, geometry, material/texture, add/delete, collection instance, visibility, camera/light. Wait for expected update and Engine state; assert GUID identity and unrelated data unchanged. Then test reconnect after missed edits, incomplete snapshot, and offline re-open. Persistence must save copies of blend/project, never originals.

## 8. Cleanup and report
Always write exporter `release` file (existing performance exporter waits on it), release socket and run-owned Editor/Blender processes, close logs, restore camera and any owned config only if no concurrent conflicting edits, verify file hashes, save `restoration.json`. Keep evidence even after failure. Explicitly name highest evidence level and uncovered features.
