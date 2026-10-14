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
M2 only after M1.

## Progress
Pending.

## Decision Log
Check occupancy, projected landmarks, GBuffer normal and known marker textures before final appearance metrics; track color space and adjacent-frame noise.

## Surprises & Discoveries
Existing code names `DeferedGeometryPass`, `_GBuffer1`, `TransparentPass`; never assume selectors survive future renderer changes. Existing render Skill explicitly requires `capture_index` and consecutive `engine_frame` checks.

## Validation
Tests: known camera landmarks within calibrated pixel tolerance; cube/instance silhouettes; expected GBuffer masks/normals; texture marker regions and clipping/blending; continuous three-frame manifest; seeded empty frame/wrong normal/frame gap must fail; same replay stream produces images within baseline temporal noise.

## Acceptance Criteria
All required synthetic L3 oracles PASS; corruption is detected; capture/current build consistency and cleanup documented. No global Blender render equivalence claim.

## Known Follow-ups
Advanced procedural materials, glass, transmission and path tracer behavior can use extra scene-specific observables later.

## Outcomes
Pending.

## Resume Protocol
Read this milestone and existing render Skill only after M1 completion; update progress before M3.
