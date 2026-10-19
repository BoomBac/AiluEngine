# M2 — GPU Appearance and Projection Validation

## Purpose
Prove imported scene data renders correctly, separately from protocol and entity metadata success.

## Scope
Reuse existing render-frame-validation Skill and Editor camera/RPC/capture; inspect supported pass/resource; capture GBuffer/final frame sequence; compare deterministic silhouette, known normals, UV marker textures, alpha mode and camera landmarks.

## Non-goals
Do not require exact Cycles↔Ailu Standard Lit final RGB match or general shader equivalence.

## Architecture Constraints
GPU tests require a real Windows graphics session, correct driver and DX12 adapter. One capture at a time. Verify camera/aspect/color/AA and capture manifest, frame continuity, actual pass/resource selection and renderer warmup. Restore camera/settings in finally.

## Baseline
`Test/BlenderLinkPerformanceCapture.mjs`, `Test/AnalyzeBlenderLinkPerformance.py`, `.agents/skills/ailu-render-frame-validation`.

## Target Outcome
Known geometry and color fixtures produce automated L3 PASS/FAIL and capture PNG/manifest + error maps; real-scene images remain semantic/inconclusive where approximation prevents exact oracle.

## Relevant Files
`Test/TaaEditorAcceptanceClient.mjs`, `Test/BlenderLinkPerformanceCapture.mjs`, `Editor/Src/Automation/AutomationCaptureService.cpp`.

## Milestones
M2 gated by accepted M1; M3 gated by accepted M2.

## Current Milestone
M2 accepted on 2026-10-16 after M1 acceptance.

## Progress
Accepted. The hidden runner drives a fixed SceneView camera, captures three contiguous frames each from `DeferedGeometryPass` `_GBuffer0` and `_GBuffer1` plus `TransparentPass`, and restores the camera. The deterministic fixture covers direct/instance silhouettes, four projected material markers, binary alpha clipping, and alpha blending. The suite ran twice with identical canonical Full Sync packets and bit-identical GBuffer/final images on this workstation.

## Decision Log
Check occupancy, projected landmarks, GBuffer normal and known marker textures before final appearance metrics; track color space and adjacent-frame noise.

## Surprises & Discoveries
Existing code names `DeferedGeometryPass`, `_GBuffer1`, `TransparentPass`; never assume selectors survive future renderer changes. Existing render Skill explicitly requires `capture_index` and consecutive `engine_frame` checks.

## Validation
Tests: known camera landmarks within calibrated pixel tolerance; cube/instance silhouettes; expected GBuffer masks/normals; texture marker regions and clipping/blending; continuous three-frame manifest; seeded empty frame/wrong normal/frame gap must fail; same replay stream produces images within baseline temporal noise.

Accepted evidence: aggregate report [m2_suite_summary.json](F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_link_validation/m2_suite_summary.json); run IDs `20261016-145622-238ef9d2` and `20261016-145708-70fafa6a`. Each passed 36 checks at 896x512. Repeated-run RMS/P99 were 0 for normal, albedo, and final frames. The per-process Blender bake temp-directory segment is normalized only for packet-stream comparison; the canonical SHA256 is `1a7db2a517e3a50e01f394931c4823f1f39cb86dedd45f7714552b44d7ea980c`. Exception cleanup evidence: `20261016-145803-0fac8160`.

## Acceptance Criteria
All required synthetic L3 oracles PASS; corruption is detected; capture/current build consistency and cleanup documented. No global Blender render equivalence claim.

## Known Follow-ups
Advanced procedural materials, glass, transmission and path tracer behavior can use extra scene-specific observables later.

## Outcomes
M2 accepted for the synthetic raster path on the captured Windows/DX12/GTX 1050 setup. No Cycles RGB parity or broader real-scene/shader equivalence is claimed.

## Resume Protocol
M2 is complete. Read `progress.md` and the M3 milestone only after the user authorizes M3.
