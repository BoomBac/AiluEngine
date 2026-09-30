# Volumetric fog density and distance correction

## Purpose
Make fog density control scattering and attenuation consistently; allow near-view fog without a camera-far-plane haze.

## Scope / Non-goals
Correct the existing homogeneous integration source function and low-optical-depth precision. Add an independent
feature fog distance, default 50 meters, shared by generation and composition. Preserve existing density/intensity/g.
No height falloff, local density volumes, distant fog, transparency fog, or general history redesign.

## Architecture constraints
VolumetricFog owns settings/passes. Explicit existing RenderGraph flow and current-view camera. New reflected field
retains source defaults for old data; generator updates through build. Preserve unrelated dirty work; build-debug only.

## Relevant files
VolumetricFog.h/.cpp, Compute/volumetric_light.hlsl, volumetric_fog_composite.hlsl, rendering.md.

## Current milestone
Complete: M1 diagnosis, M2 implementation/Debug build, M3 capture validation and restoration.

## Progress
- M1: fresh Debug captures at density 1, 0.01, 0.0001 and zero, same current user camera and scene.
- Native sky mean rises from ~0.246 to ~2.68 as density falls; zero density returns the unfogged sky exactly.
- Cause: injected light has no density factor but integration divides scattering by density. Fixed sky composition
  exposed the existing discontinuity. Uniform extinction extends to camera far=1000m, dwarfing near-view optical depth.
- Diagnostic shader overrides restored in finally; task-owned Editor instances closed, no scene saved.
- M2: added reflected _fog_distance, shared generation/composition domain, temporal domain guard, corrected source
  integration, and low-optical-depth series. Analytic zero-density/monotonicity/integration checks and build-debug passed.
- M3: density 1, 0.01, 0.0001, 0 and 3 native/PNG captures completed. Runtime reflection exposes the new distance field.
  Low-density sky no longer increases brightness; density 1 keeps sky color, density 3 increases near-object fog.
- M3 final: per-process unfogged backgrounds and explicit camera snap verified density 1, 0.0001 and zero. Zero fog
  exactly matches the same-process background. Default sky T is about 0.60 (packed HDR), versus analytic 0.6066.
- Final 16 stationary frames, 120 continuous moving frames and 16 settled frames captured; no engine-frame gaps.
  Camera restored, diagnostic source overrides restored, all backed-up user config files hash-identical.

## Decision Log
- Treat injected lighting as a source function with unit scattering albedo: slice contribution = light * (1 - T).
- Use a small-optical-depth series for 1 - exp(-tau), ensuring continuous low-density scattering.
- Independent fog distance defaults to 50m. Clamp fog domain to current camera near/far; saturate distant geometry/sky.
- Reject temporal history for a changed near/far fog domain on the first dispatch after the change.

## Surprises & Discoveries
- In the old formula, density 0.0001 produces far scattering ~10 times density 1, despite tiny opacity.
- Runtime source hot-reload cannot be assumed; diagnostic overrides use fresh Editor processes.
- User camera/config changed since prior task; new backups preserve the current user state.

## Validation
Baseline and fixed captures at 1536x832. Evidence beside selected Debug executable:
F:/AiluEngine/artifacts/out/editor/bin/x64/debug/VolumetricFogDensity_20261001.
Analytic integration checks, Debug build and fresh-process density matrix passed. Default stationary sky adjacent RMS
0.215/P99 1 in 8-bit space; all native outputs finite and default depth exactly matches baseline. Density 1 sky mean
RGB is [0.1621,0.2098,0.2855], preserving color instead of the previous [0.2459,0.2459,0.2441]. At density 3, near-object
RMS change is ~0.103 HDR versus ~0.040 at density 1. Moving capture load ~12.08 fps with 4 camera updates over 2 seconds;
filmstrip reviewed, no registered moving-geometry metrics or normal-frame-rate claim. Temporal reprojection enabled
and live UI distance editing remain untested; reflection schema and source domain-reset logic checked.

## Outcomes
Density/intensity and range now have separate roles; default density 1 with a 50m fog domain keeps sky color while
retaining near fog. Very small physical density still produces negligible near extinction, as expected. No height
falloff or local-volume model introduced. Task-started Editors closed, no scene saved, current user config preserved.
Evidence REPORT.md, comparison.png and verified_analysis.json document the tested path and limits.
