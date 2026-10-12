# Classroom Blender Link sync interruption

## Purpose / Scope

Diagnose and repair the supplied classroom.blend Full Sync that remains running at 1333 applied packets.
Preserve source scene, existing dirty work and main-thread-only Blender transport. No unrelated rendering changes.

## Architecture constraints / Relevant files

Blender timer owns export and nonblocking transport; plugin Tick applies commands on the Engine main thread.
FullSyncEnd reconciles entities; never report an incomplete export as a completed snapshot.
Tools/Blender/ailu_live_link/{addon,object_sync,connection,mesh_sync,material_bake}.py.

## Current milestone / Progress

- [x] Inspect protocol, timer, export and Engine progress lifecycle; inspect real classroom collection instances.
- [x] Reproduce the interruption and identify the failing operation.
- [x] Targeted repair and failure-lifecycle regression.
- [x] Native classroom export/transport validation, package and Debug build.

## Decision Log

- Reproduce in a separate background Blender process; never save or change the user's open scene.
- Engine progress count is applied packets, not object count. Queued bytes excludes the active partial packet.

## Surprises & Discoveries

- Scene has 304 direct objects and hundreds of linked collection instances; furniture is mostly instanced.
- Geometry-only instance baseline emits 678 object packets and 673 nonempty mesh packets successfully.
- Root FileAppender_log.txt is an older run (2026-10-10); it does not prove the supplied classroom run's state.
- Installed-add-on baseline reproduces exactly 1333 applied packets: 982 objects, 150 materials, 196 meshes,
  four lights and one camera. Material baking an empty evaluated collection mesh raises
  `RuntimeError: No faces found in the object "AiluBakeObject"`, before FullSyncEnd. The timer has no exception
  handler, so Blender remains usable and connected but export and its timer stop.
- Empty meshes without a source UV also need to bypass smart_project; a focused native fixture caught this path.
- Collection instance geometry is now encoded first; empty geometry never invokes surface material baking.
  Direct empty meshes also skip bake. Failures disconnect without emitting End, preserving the host snapshot;
  the timer catches errors, stays registered and permits a user-triggered reconnect.

## Validation / Outcomes

Evidence: Saved/ClassroomLinkValidation/.

- baseline.log: actual installed Blender 5.0.1 add-on reproduces the exact packet count and missing End.
- fixed.log: source native Full Sync including actual Cycles baking completes with End.
- tcp-result.json / tcp-export.log: real nonblocking TCP transport to an independent receiver succeeds,
  2041 Full Sync packets (+ HELLO), 66,647,719 total wire bytes, 982 unique objects, 838 unique meshes,
  214 material packets. Blender reports no coalesced packets, and the receiver sees End and verifies every
  mesh GUID belongs to an exported object. Source scene/object counts and mesh counts after cache cleanup restore.
- empty-instance-final.log: Blender exit 0; actual procedural cube bake, empty direct and instance geometry,
  injected export failure without End, disconnect, timer survival and successful reconnect all pass.
  The error log's injected failure traceback is expected, not a native crash.
- instance-regression-final.log and mesh-regression-final.log: Blender exit 0 for existing collection instance
  identity/placement/lifetime and binary geometry attribute/submesh regressions.
- build-debug.log: `cmake --build --preset build-debug`, exit 0.
- ZIP rebuilt through the repository PowerShell 7 packaging script; all 13 Python modules compile and match
  the ZIP byte-for-byte. Three installed modules match source; backups are in installed-backup/.
- Initial extra regression attempts were blocked by file access restrictions and rerun successfully elevated.
  Initial Windows PowerShell packaging attempt failed on runtime support; PowerShell 7 packaging passed.

## Outcomes

Root cause reproduced and repaired; native scene export, real TCP, lifecycle regressions, package and Debug
build pass. No Engine-side source changes required. Installed addon files are updated, but the currently
running user's Blender still holds old Python modules: restart Blender before retrying Full Sync.
No source blend was saved and no user's open process was terminated. Fresh live Editor import/visual
acceptance was not executed and is not implied by the independent TCP receiver.
