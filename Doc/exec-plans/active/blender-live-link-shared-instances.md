# Blender Live Link Shared Instances

## Purpose

Import evaluated Blender mesh instances as compact batches that reference one shared prototype mesh. Remove
per-instance software ray-tracing geometry duplication and let the existing renderer represent large scenes.

## Scope

- Add an additive protocol-v1 instance-batch message carrying ordinary object metadata and transforms.
- Keep one prototype mesh packet per source mesh; apply each batch on the Engine owner thread.
- Reuse the prototype `Render::Mesh` and per-instance material bindings.
- Pack software ray-tracing triangles and BLAS nodes once per unique mesh while keeping per-entity offsets.
- Raise the primitive capacity to a bounded 65,536 entries across its fixed CPU/GPU buffers.
- Keep ordinary object and mesh updates compatible.

Non-goals: Geometry Nodes translation, geometry baking/merging, dynamic capacity growth, per-instance material overrides
beyond the existing source material slots, and changes to DXR's geometry cache.

## Architecture constraints

- Blender RNA access and Engine scene mutation remain on their existing owner threads.
- No Plugin ABI change; message is additive to protocol v1 and both bundled endpoints must be updated together.
- Existing `StaticMeshComponent` instances remain the scene representation.
- Keep per-instance transforms, visibility, entity identity, GI sampling and TLAS bounds.
- Do not modify unrelated dirty files or generated code.

## Relevant files

- `Plugins/BlenderLiveLink/Inc/LiveLinkProtocol.h`
- `Plugins/BlenderLiveLink/Inc/LiveLinkSession.h`
- `Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp`
- `Tools/Blender/ailu_live_link/protocol.py`
- `Tools/Blender/ailu_live_link/object_sync.py`
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

## Validation

- Inspect source and targeted diff for protocol pairing, bounds, and dirty-work preservation.
- Run Python syntax compilation if a Python executable is available.
- Run `cmake --build --preset build-debug`.
- Runtime acceptance remains separate: synchronize the Pine Forest file, confirm stable progress/completion, inspect
  visible instances, and compare scene triangle-buffer counts and memory.

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
