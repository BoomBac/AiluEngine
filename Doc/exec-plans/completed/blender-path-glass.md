# Blender BMW glass transport

## Purpose
Restore transmitted views through BMW windows and headlight covers in the DXR path tracer.

## Scope / Non-goals
Preserve primary Glass/Principled transmission and IOR through direct and baked Live Link materials.
Support alpha coverage null events independently of physical refraction. No exact arbitrary Blender closure matching.

## Architecture constraints
Keep LiveLinkSession material ownership and shared Compute/DXR material evaluation. MaterialData is transient;
reuse its unused opacity texture slot as scalar alpha, retaining 92-byte stride and existing offsets.
Preserve unrelated dirty changes and restore runtime camera/configuration. Do not save the Blender scene.

## Relevant files
Tools/Blender/ailu_live_link/material_{sync,bake}.py; Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp;
Engine/Inc/Render/ShaderInterop.h; SceneRayTracingProxy.cpp; path_tracer_{material,core}.hlsli; rt_common.hlsli.

## Current milestone
Primary glass transport implemented, built, deployed and verified in the actual BMW scene.

## Progress
- [x] Inspect original BMW27 Glass, Transparent and Add Shader nodes in an independent read-only Blender.
- [x] Identify omitted transmission/IOR, pre-texture metallic glass classification and omitted alpha coverage.
- [x] Implement primary dielectric transport, resolved-metallic classification and bounded null-event traversal.
- [x] Validate Blender direct/baked payloads, existing bake regression and eight shader variants; build-debug.
- [x] Capture BMW using current addon code, inspect headlights/windows and restore runtime state.
- [x] Back up and deploy the two installed addon material modules without changing preferences.
- [x] Complete ordinary Compute/DXR temporal regression with existing failures recorded below.

## Decision Log
Null coverage events do not consume BSDF depth or alter previous MIS state. Bound total traversal by max_depth + 17.
Layered Add Shader reflection and textured transmission/IOR remain explicit primary-closure approximations.

## Surprises & Discoveries
BMW Glass: Glass IOR 1.45 mixed with Transparent at factor 0.8. HeadlightGlass: Glass IOR 1.0 plus Glossy.
Bake exports metallic coefficient 1 with a texture containing actual metallic 0; classify glass after texture sampling.
Initial runtime attempt was rejected by automatic approval due usage limit; retry on 2026-10-11 succeeded.

## Validation
Original nodes: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/bmw_glass_nodes_20261011.json.
Glass direct/baked parameter smoke and existing material bake smoke pass. Eight shader variants compile with HLSL 2018;
build-debug passes. The initial standalone HLSL 2021 command exposed pre-existing vector ternary incompatibilities;
no unrelated shader expressions changed. The loop attribute is attached to the loop rather than its depth declaration.
PathGlass_20261011_Car_1 used a preloaded installed addon and had no transmission/IOR fields; not accepted as new-code evidence.
The independent exporter now discards those preloaded module references and records its actual checkout paths.
PathGlass_20261011_Car_2: 78 frames, 26/26 numerical gates, Reference lengths 1631..1638 at all pixels, finite contiguous
sequences, normal driver exit and 8/8 configuration files restored. Actual windows and headlight interiors are visible.
Same geometry/camera/light setup, changing only material transmission to 0 and alpha blend to false, recreates white
opaque windows and gray opaque lamp covers. This control demonstrates the material transport cause.
Installed material_sync.py/material_bake.py match checkout byte for byte, with backups in PathGlass_20261011_AddonBackup.
Blender preferences and other installed files unchanged. No scene saved.
Fresh Blender verifies the installed module exposes transmission_parameters. Its two deployed files match the package.
PathGlass_20261011_Regression_1: 640 observations and 4992 captures; 103/105 image gates pass.
Only compute_taa_taa_reset_seed and dxr_taa_taa_reset_seed fail, matching the immediately preceding solar regression.
Debug layer enabled, ERROR/CORRUPTION=0, fixture normal exit, 8/8 files restored. No new temporal failure observed.

## Outcomes
Primary BMW glass/window and lamp-cover transmission passes live visual inspection. Glass shadows, arbitrary closure
fidelity, Transparent BSDF tint and maximum refractive depth remain separate limits. Ordinary temporal regression
has no new failure. This completes the bounded opaque-window/lamp repair; it does not close all firefly classification.
