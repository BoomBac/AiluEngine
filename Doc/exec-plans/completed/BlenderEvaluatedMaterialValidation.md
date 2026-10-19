# Evaluated geometry/material consistency

## Purpose
Make evaluated geometry, material tables and bake UVs consistent for mesh modifiers, Geometry Nodes and converted curves; add reusable wire/Engine/render validation and retain splash snow as a real-scene regression.

## Scope / Non-goals
Preserve sparse material slots, object overrides and evaluated material assignment; support convertible source geometry. Add bounded read-only material inspection without large mesh arrays, synthetic fixtures, negative assertions and native scene evidence. Lighting, World export and AgX matching are separate work.

## Architecture constraints
Blender evaluation and baking remain on its main thread. Existing ExportSession owns temporary snapshots; temporary meshes are released by their creator. Engine owns render resources; Editor inspection remains read-only. Preserve original assets, open user processes and existing dirty changes.

## Relevant files
Blender `material_bake.py`, `material_sync.py`, `mesh_sync.py`, `object_sync.py`, `dirty_tracker.py`; `AutomationSceneService.cpp`; `Test/BlenderLinkValidation/`; validation skill references.

## Current milestone
Completed both bounded milestones.
1. Repair evaluated material-table transport and add focused native Blender regressions.
2. Debug build and Engine material binding/ROI assertions; splash-scene retest; package/deploy and documentation.

## Progress
- Confirmed source and evaluated Curve object slots can both have one item while `to_mesh().materials` has `[None, snow]`; material index 1 fell back to checker before this repair.
- User closed their Blender process for isolated acceptance.
- Completed production fix, bounded read-only Engine summary, independent synthetic/scene references, negative assertions, incremental Engine/ROI verification, packaging/deployment and documentation.

## Decision Log
- Use the actual evaluated mesh material table, preserving empty positions. Cache metadata only within the existing bounded ExportSession.
- Converted geometry must be published once: use direct geometry or its depsgraph self-instance prototype, never both.
- Unexpected fallback is FAIL; an actually used source empty slot is explicitly unsupported rather than silently PASS.

## Surprises & Discoveries
Replacing original object material slots with evaluated object slots alone does not fix Geometry Nodes on Curve. The mesh table is authoritative.

## Validation
PASS: source-to-wire, four seeded binding corruptions, Debug build, actual Engine summaries, controlled albedo/UV markers and three bounded native mutations. Installed-addon splash run `20261016-171925-e87a8cac`: 197 renderables, 2768 binding checks. Generic final run `20261016-172239-b69bf24e`: 146 baseline and 146 incremental checks. Processes exited 0, shared state restored, original hash unchanged. Full M3 remains open. Existing material_bake_smoke fails identically on HEAD due to its constant-channel image assumption; logs retained.

## Outcomes
Implemented and installed. Skill now routes material cases to the executable generic suite. Report: `F:\AiluEngine\artifacts\out\editor\bin\x64\debug\blender_material_validation\20261016-171925-e87a8cac/material_fix_report.md`. World/lighting/tone mapping parity remains separate.

## Follow-up: legacy bake smoke repair — 2026-10-16
At the user's request, repaired the old smoke to compare effective linear color/alpha and roughness from either factors or textures. Nonuniform checker and bump cases still require real textures. Updated depsgraph update doubles and dependency assertions to seed an actual baked payload, invalidate that payload while retaining reusable UV geometry, then verify explicit cache cleanup. The previous FAIL above records the original run, before this test repair.

Full Blender 5.0.1 `material_bake_smoke.py` now PASS, including mixed colors, scalar masks, alpha/culling, atlas, instances, mapping/bump, transparent pigment, HDR emission, group defaults, cleanup and image/group dependencies. `cmake --build --preset build-debug`, Python syntax and `git diff --check` PASS. Production exporter behavior unchanged. Logs: `Temp/splash_validation/material_bake_updated.log` and `Temp/splash_validation/material_smoke_build.log`. Added a narrow `.gitignore` exception so this previously ignored regression file is reviewable/versionable.
