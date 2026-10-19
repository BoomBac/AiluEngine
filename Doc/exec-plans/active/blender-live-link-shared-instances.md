# Blender Live Link Shared Instances

## Purpose

Import evaluated Blender mesh instances as compact batches that reference one shared prototype mesh. Remove
per-instance software ray-tracing geometry duplication and let the existing renderer represent large scenes.
For very large Geometry Nodes self-instance sets, merge compatible evaluated meshes into bounded static batches
grouped by material-slot layout.

## Scope

- Add an additive protocol-v1 instance-batch message carrying ordinary object metadata and transforms.
- Keep one prototype mesh packet per source mesh; apply each batch on the Engine owner thread.
- Reuse the prototype `Render::Mesh` and per-instance material bindings.
- Pack software ray-tracing triangles and BLAS nodes once per unique mesh while keeping per-entity offsets.
- Raise the primitive capacity to a bounded 65,536 entries across its fixed CPU/GPU buffers.
- Keep ordinary object and mesh updates compatible.
- For at least 4,096 generated self-instances, merge world-transformed geometry by material-slot layout and
  visibility, splitting batches at vertex, triangle and source-instance budgets.
- Track generated source states separately from synthetic batch entities so incremental updates rebuild stable
  batches and remove obsolete batch entities.
- Extend the independent material oracle to validate aggregate batch geometry and bindings.

Non-goals: General Geometry Nodes graph translation, dynamic capacity growth, new per-instance material overrides,
and changes to DXR's geometry cache.

## Architecture constraints

- Blender RNA access and Engine scene mutation remain on their existing owner threads.
- No Plugin ABI change; message is additive to protocol v1 and both bundled endpoints must be updated together.
- Existing `StaticMeshComponent` instances remain the scene representation.
- Keep per-instance transforms, visibility, entity identity, GI sampling and TLAS bounds.
- Static merge applies only to high-count Geometry Nodes self-instances. Ordinary objects and smaller instance sets
  retain their existing entity-level representation.
- Synthetic batch meshes bake the Blender instance world matrix into vertex data; batch entities use identity
  transforms and stable IDs derived from scene, material layout, visibility and chunk index.
- Do not modify unrelated dirty files or generated code.

## Relevant files

- `Plugins/BlenderLiveLink/Inc/LiveLinkProtocol.h`
- `Plugins/BlenderLiveLink/Inc/LiveLinkSession.h`
- `Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp`
- `Tools/Blender/ailu_live_link/protocol.py`
- `Tools/Blender/ailu_live_link/object_sync.py`
- `Tools/Blender/ailu_live_link/mesh_sync.py`
- `Tools/Blender/ailu_live_link/dirty_tracker.py`
- `Test/BlenderLinkPerformanceExport.py`
- `Test/BlenderLinkValidation/make_material_fixture.py`
- `Test/BlenderLinkValidation/validate_material_bindings.py`
- `Test/BlenderLinkValidation/capture_material_bindings.mjs`
- `Engine/Inc/Render/RenderConstants.h`
- `Engine/Src/Scene/Scene.cpp`
- Renderer and frame-resource fixed-capacity buffers

## Milestones

### M1 — Shared prototype transport and Engine application

Send generated instances in bounded batches, apply each instance against a previously imported prototype, preserve
incremental transform/delete behavior, and retain existing ordinary object messages.

### M2 — Shared software ray geometry and primitive capacity

Deduplicate software triangle/BLAS buffers by mesh pointer and raise every active fixed primitive buffer/capacity
to the bounded scene limit. Confirm DXR continues using its existing per-mesh geometry cache and per-instance TLAS.

### M3 - Material-compatible static batches for dense Geometry Nodes output

Merge generated self-instance mesh payloads after baking each instance transform into engine-space vertices.
Group only identical ordered material-slot layouts and visibility states. Bound each merged payload by 200,000 loop
vertices, 400,000 triangles and 1,024 source instances. Preserve the existing path below the 4,096-instance
threshold, refresh all affected batches on source changes, and validate batch IDs, aggregate geometry and material
bindings through the isolated Blender/Editor runner.

## Validation

- Inspect source and targeted diff for protocol pairing, bounds, and dirty-work preservation.
- Run Python syntax compilation if a Python executable is available.
- Run `cmake --build --preset build-debug`.
- Run a transformed-payload smoke check covering translation, negative scale, winding, normals, UVs and submesh indices.
- Runtime acceptance remains separate: synchronize the Pine Forest file, confirm stable progress/completion, inspect
  visible batch geometry, compare aggregate material/triangle/loop counts and captures, and verify incremental batch
  replacement after source changes.

## Progress

- 2026-10-15: Started M1. Existing protocol already caches equal Mesh resources, but Blender sends one object and
  mesh message per generated instance; Scene software ray data is currently duplicated per StaticMeshComponent.
- 2026-10-15: Worktree contains unrelated dirty changes. Relevant existing Live Link edits include bounded queue
  backpressure in the server and Blender sender; these will be preserved.
- 2026-10-15: M1 implemented. Blender emits 128-record batches referencing source mesh GUIDs; the Engine applies
  object state and assigns the prototype mesh/material slots. HELLO negotiates `instance_batch_v1` so an older
  Engine fails with an actionable connection error instead of silently omitting generated instances. Batch
  commands bypass queue coalescing because one message contains many object identities.
- 2026-10-15: M2 implemented. Scene software triangles and BLAS nodes are packed once per unique Mesh pointer, with
  entity/submesh maps sharing offsets. Scene primitive, frame primitive/index, and TLAS capacities now support
  65,536 active primitives; the legacy 2,000-entry RenderPipeline path remains unchanged.
- 2026-10-15: Pine Forest import exposed a stale 16,384-sibling traversal limit in TransformSystem. RebuildOrder
  now bounds sibling traversal by the current transform entity count, preserving cycle protection for larger scenes.
- 2026-10-16: Large-scene runtime logs exposed three scale issues: hierarchy append scanned all previous siblings,
  the renderer dispatched one primitive-build job per 32 entities, and per-pass instance-index tables accumulated
  against a single-pass capacity. Added a transient hierarchy tail cache, increased build chunk size to 256, and
  reset the index table for each emitted draw pass (draw snapshots retain each uploaded data version).
- 2026-10-16: `cmake --build --preset build-debug` and targeted `git diff --check` passed after these fixes.
  Blender/Editor Full Sync runtime acceptance remains pending.
- 2026-10-15: Python AST parse PASS; add-on ZIP rebuilt; `cmake --build --preset build-debug` PASS twice, including
  a final incremental build after handshake and queue changes. Runtime Blender/Editor scene sync and visual/memory
  acceptance remain unverified.
- 2026-10-19: `flower_scattering.blend` produced 116,633 source instances and 984 instance packets; the prior
  capacity-guard retest emitted FullSyncEnd but its Blender owner exited before the Editor oracle completed. The
  exporter held its peer for only 600 seconds, below the observed Full Sync time. M3 now adds bounded static merge,
  aggregate oracle expectations and a 3,600-second peer hold; final build and scene acceptance are pending.
- 2026-10-19: Static merge batches now use the negotiated `static_mesh_batch_v1` message. Their synthetic meshes
  upload raster geometry without CPU triangle/BVH derived data and are kept transient instead of persisted as assets;
  Scene software BLAS packing skips meshes without derived data. Debug build passed before this plan update; Blender/
  Editor Full Sync and visual acceptance remain pending.
- 2026-10-19: The fixed-output `build-debug` rebuild passed again. The machine exposes only inaccessible
  WindowsApps Python aliases and no Blender executable was discoverable, so Python AST and live Full Sync checks remain
  unrun rather than being reported as passed.
- 2026-10-19: Re-ran `flower_scattering.blend` with explicit Blender `D:/blender5.0/blender.exe` and fixed-output
  Editor. Blender completed Full Sync export with 116,633 source instances, 414 static batches, 69,215,584 loop
  vertices and 34,607,792 triangles; Begin/End were each emitted once and the source was restored. Engine apply still
  failed: the first material packet (type 21, sequence 12) failed, then `_needs_full_resync` caused all 414 static
  mesh packets and 414 instance-batch packets to be rejected. Static-batch Engine acceptance remains pending; evidence
  is under `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation_20261019_static_merge/`.
- 2026-10-19: Fixed Live Link material texture asset names by shortening channel suffixes to `albedo`, `normal`,
  `emission`, `roughness` and `metallic`; this preserves the session/material GUID identity while keeping generated
  paths below the Windows path limit under the fixed `F:/AiluEngine` output root. Apply now logs caught exceptions
  with message type and sequence instead of silently cascading them into resync failures.
- 2026-10-19: `cmake --build --preset build-debug` passed. The focused material validation ran twice after the fix;
  both current runs had Engine `type21=5` with zero Apply failures/exceptions and persisted the compact `_albedo`
  texture asset. The runner still reports its existing material image assertion failure, so visual/material oracle
  acceptance remains separate from the now-passing Engine apply gate.
- 2026-10-19: Re-ran the real `flower_scattering.blend` Full Sync after the material-path fix. All 165 material
  packets applied without failure, the run emitted/applied FullSyncEnd, and the Engine summary recorded 414 instance
  batches with zero Apply failures. The overall runner remained FAIL because the independent static-merge reference
  listed 353 batches while the current exporter emitted 414; the oracle therefore stopped before writing the Engine
  snapshot. Material persistence and Engine Apply are now validated; static-merge reference parity remains open.
- 2026-10-19: Fixed the independent material reference to model object-baked material GUIDs, deterministic mesh/data
  identities, spatial/modifier bake identity and generated-component base material bindings without importing the
  production bake helper. A fresh `flower_scattering.blend` run produced 414 expected static batches, 165 material
  packets, one FullSyncBegin/End pair and zero Engine Apply failures; the structural NodeOracle passed.
- 2026-10-19: Fixed `validate_material_bindings.py` to parse the additive static-batch mesh packet (type 32). The
  post-run validation against the fresh packets and Engine snapshot passed 7,011 checks with zero failures or
  unsupported fields; the old runner result retained its pre-fix material assertion failure only as historical evidence.

## Decision log

- Reuse the current ECS instance model to retain compatibility with picking, visibility, raster batching, GI and DXR.
- Bound instance packets at 128 records so one main-thread apply does not block on all 34k instances at once.
- Keep protocol version 1 and introduce an additive message type because the plugin and bundled add-on ship together.
- Negotiate the new message capability in HELLO/HELLO_ACK; this preserves old add-on compatibility while clearly
  rejecting an older Engine when the new add-on requires batching.

## Outcomes

- Implemented shared prototype transport, batched transforms, software geometry reuse and bounded 65,536 primitive
  support. Runtime acceptance remains pending; this plan stays active until the Pine Forest Full Sync is exercised
  in Blender and the Editor.
