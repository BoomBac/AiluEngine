# Lamp partial raster transmission

## Purpose
Diagnose and repair the user's lamp cover: transparent upper strip, gray opaque lower region.

## Scope / Non-goals
BMW lamp import/material/forward shading correction, before/after captures at multiple viewing angles.
Do not extend this into physical thick-glass refraction or multilayer rendering.

## Architecture constraints
Preserve existing dirty changes, alpha coverage, TAA ordering and per-view HDR snapshot ownership.
Keep user Blender untouched; isolate owned Editor/headless exporter on port 17779 and restore runtime state.

## Relevant files
material_bake.py, material_sync.py, LiveLinkSession.cpp, forwardlit.hlsl, CommonPasses.cpp,
Test/DiagnoseBmwLamp.py and existing BMW capture/export scripts.

## Current milestone
Diagnosis and isolated fresh-module acceptance complete. Await user Blender restart and Full Sync for their live scene.

## Progress
- [x] M1 source and actual BMW evidence; old bake module produces a gray lamp overlay.
- [x] M2 existing production repair verified; no further production code/build needed.
- [x] M3 fresh/old module four-angle capture comparison, 58/58 integrity/control gates.
- [ ] User live-scene confirmation after Blender restart and Full Sync.

## Decision Log
Prior single-camera evidence does not establish correct lamp transmission at the user's viewing angle.

## Surprises & Discoveries
The outer lightsGlass mesh has one HeadlightGlass material, not separate top/bottom materials.
Its closure is Glass(IOR=1, color=0.8) + Glossy, with a displacement connection.
User confirmed Blender was not restarted after the preceding material module update.
Installed material_bake.py matches the checkout; the existing Blender process predates that update.
Fresh bake metallic texture is zero everywhere and normal texture is neutral. Old backup bake has covered metallic
pixels at 128/255 (~0.5), which reduces background throughput and adds gray surface shading.
Old-module control reproduces gray attenuation; it does not reproduce every exact boundary in the user's screenshot.

## Validation
Fresh_1 / OldBake_1: same engine, four camera angles, geometry and lighting; only material bake module changes.
64 total captured frames (4 frames x 2 resources x 4 angles x 2 runs), contiguous and finite; 58/58 gates.
Pre-glass snapshot RMS difference across controls is 2.06e-5 to 3.62e-5; background geometry unchanged.
Fresh visual inspection confirms lower lamp rings/interior visible. Original user session is not yet visually accepted.
Both isolated runs restore camera/settings and eight files. Only original user Blender PID 40632 remains.

## Outcomes
Evidence points to the user's stale Blender module/bake cache. Restart Blender and execute Full Sync to reimport
the already-fixed glass bake. Do not close the user's Blender or discard any unsaved scene state.
Report and comparison: artifacts/raster_lamp_20261011/result.md.
