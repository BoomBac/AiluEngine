# Wasp material export fidelity

## Purpose
Preserve texture scale/detail when syncing wasp_bot.blend without modifying the original asset.

## Scope / Non-goals
Repair AUTO material routing and bake quality. Preserve explicit DIRECT and resolution overrides.
Do not redesign Standard Lit or promise identical layered Blender BSDF lighting.

## Architecture constraints
Blender main thread owns graph evaluation and baking. Reuse existing object-specific bake payloads,
UV atlas ownership and bounded batches. Preserve unrelated dirty files and user processes.

## Relevant files
Tools/Blender/ailu_live_link/material_sync.py, material_bake.py, Tools/Blender/README.md,
Tools/Blender/tests, Test/BlenderLinkValidation/runner.py.

## Current milestone
Complete: material export repair, deployment and native real-scene validation.

## Progress
- Diagnosis: Fiber_cloth, Head and Metal_parts bake at 256px; Pastic incorrectly exports its
  Object/BOX/Mapping/Curves roughness branch directly.
- M1 complete: AUTO rejects omitted Mapping/projection/adjustments and non-default normal-map settings;
  requires_bake uses the same decision. Default resolution is 2048; spatial graphs use independent atlases.
- Native 64px test passed coordinate repetition and color adjustment (linear red 0.1/0.4), roughness
  (0.2/0.8), normal-map routing, explicit DIRECT/resolution overrides and source mesh preservation.
- Original-scene classification: Fiber_cloth/Head/Metal_parts/Pastic now all bake at 2048px.
- M2 complete: source/ZIP/installed SHA256 match for 17 modules; previous installation backed up beside Editor.
- Debug build passed. Installed-add-on real-scene run completed 38 mesh and 40 material packets with one
  Begin/End and no Apply failures. 534 binding and 8 capture-integrity assertions passed.
- Three consecutive frames captured per albedo/final resource. Source blend hash unchanged; owned
  Blender/Editor/oracle exited normally; all ten shared state files restored.

## Decision Log
- AUTO uses native baking whenever primary-image extraction reports omitted graph operations.
- Default baking uses 2048px; existing explicit per-material and call-site overrides remain supported.

## Surprises & Discoveries
- requires_bake repeats a permissive graph traversal after _can_export_direct; both must agree.
- Existing source UVs can remain for UV-driven graphs; spatial graphs need unique atlas islands.
- Repository ExecPlan template is absent; this document follows the prescribed sections directly.

## Validation
Native run: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/wasp_texture_fixed_native/20261018-105226-1362c452.
Report: wasp_texture_fixed_native/skill_report_wasp_bot_20261018/wasp_bot_link_validation_report.md.
The report embeds background Eevee, Engine albedo and Engine final images. Actual image inspection confirms
fine eye bump detail, but skin highlights still differ. Reference and Engine framing/heading differ; matched-view
visual parity remains unproven. No before/after numerical image comparison or lighting parity is claimed.
Native exporter generation/drain was 191953 ms (~192 seconds), with 141 PNGs; higher quality costs sync time.

## Outcomes
AUTO no longer silently drops supported native graph operations into primary-image approximations. Spatial
materials use unique atlas UVs, baking defaults to 2048px, and explicit quality/DIRECT overrides are preserved.
The repaired add-on is installed for Blender 5.0 and packaged. Original blend and user Editor state are preserved.
