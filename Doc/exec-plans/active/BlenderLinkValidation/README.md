# ExecPlan — Autonomous Blender Live Link Validation

## Purpose
Create an evidence-backed autonomous Blender→AiluEngine validation pipeline reusable by Codex and Claude through a lightweight Skill.

## Scope
Runner and process isolation, real Blender exporter and Editor fullsync, independent bpy reference, Engine scene RPC comparison, deterministic GPU capture, incremental/reconnect/persistence tests and BMW/Classroom regression suites.

## Non-goals
No Live Link protocol redesign, no general-purpose Blender renderer equivalence, no full headless Engine assumption, no automatic golden acceptance, no original `.blend` saves, no changes to users' open processes/preferences and no unbounded auto-repair loop.

## Architecture Constraints
Blender bpy, Cycles and material baking only on Blender main thread. Engine Scene/GPU on its owning thread. Version 1 protocol remains compatible. Isolated project, owned child processes, unique run ID/port and shared-state lock. Artifacts beside actual Editor.exe; validate provenance and restore any state changed. GPU visual tests require compatible Windows desktop/driver/DX12 adapter. Existing render validation Skill is a dependency, not copied code. Do not discard dirty changes.

## Baseline
The repository already contains `Test/BlenderLinkPerformance.py` (native process orchestration, packet recording/replay, log and config restoration), `Test/BlenderLinkPerformanceExport.py` (main-thread fullsync), `Test/BlenderLinkPerformanceCapture.mjs`, `Test/TaaEditorAcceptanceClient.mjs`, `Test/AnalyzeBlenderLinkPerformance.py`, `scene.query_entities` / `scene.inspect_entity`, and `ailu-render-frame-validation` Skill. Missing: general CLI paths, independent source-to-Engine oracle, test-specific visual acceptance, stateful suite orchestration and unified report. Current benchmarks include hardcoded local paths.

## Target Outcome
A repeatable suite command returns `summary.json` statuses `PASS / FAIL / INCONCLUSIVE / SKIPPED`, source-vs-result field diagnostics, unique repro packet/hash manifests, captures where available and verified cleanup. Test modes and scopes are explicit.

## Relevant Files
`.agents/skills/ailu-render-frame-validation`, `Test/BlenderLinkPerformance*.py`, `Test/BlenderLinkPerformanceCapture.mjs`, `Test/TaaEditorAcceptanceClient.mjs`, `Test/AnalyzeBlenderLinkPerformance.py`, `Test/BlenderInstanceSyncSmoke.py`, `Test/BlenderPersistenceSmoke.py/.cpp`, `Editor/Src/Automation/AutomationSceneService.cpp`, `Tools/Blender/ailu_live_link/*`, `Plugins/BlenderLiveLink/*`.

## Milestone Overview
- [M0](m0-runner.md): isolated runner, native fullsync and verified cleanup.
- [M1](m1-scene.md): independent Blender expectations and Engine structure oracle.
- [M2](m2-render.md): deterministic camera/GBuffer/render correctness.
- [M3](m3-behavior.md): incremental/reconnect/persistence and BMW/Classroom regression.

## Milestones
Each has its own failure-injection tests and acceptance gate. **Do not begin the next milestone before the current one's required tests PASS.** Each milestone should change the smallest necessary surface and leave reusable evidence.

## Current Milestone
M3 — stateful and real-scene regression. M2 was accepted on 2026-10-16; M3 implementation is partial. See `progress.md` and `m3-behavior.md`.

## Progress
M0, M1, and M2 are accepted. M2 captured deterministic GBuffer/final frames from hidden native Editor/Blender runs and compared repeated Full Sync outputs. M3 has an actual-Blender dirty-tracker smoke driver; Engine RPC correlation and named real-scene regression remain pending.

## Decision Log
Keep Skill minimal; deterministic scripts enforce pass/fail. Reuse existing native exporter, packet replay and render capture. Add a separate ground-truth oracle to detect bugs shared by Blender exporter and Engine or masked by replay. Keep current render limitations explicit. Prefer local Windows autonomous runs to unsupported cloud GPU assumptions.

## Surprises & Discoveries
`scene.query_entities` supports offset/limit/has_more, and `scene.inspect_entity` exposes components/properties. Historical BMW geometry review found visual screenshots were captured before Engine actually finished the Full Sync; correct acceptance must wait until End applied. The native performance exporter directly calls `object_sync.full_sync`, not the registered Blender addon UI.

## Validation
M0 and M1 were validated with hidden native Editor/Blender runs. Latest M1 structural suite and its run IDs, hashes, Engine RPC evidence, injected corruption results, and cleanup checks are recorded in `progress.md`. This does not imply M2 GPU/render acceptance.

## Known Follow-ups
A read-only bulk Engine Blender-link snapshot endpoint may be needed if inspecting ~1000 entities serially is too slow. More sophisticated visual/material oracles and dedicated self-hosted GPU CI are deliberately deferred.

## Outcomes
M0 passed with hidden native Editor/Blender execution, packet/apply verification and exception cleanup. M1 passed with independent bpy references and a bounded read-only Engine snapshot. M2 passed deterministic GPU appearance, projection, clipping/blending, replay comparison, and cleanup checks. M3 implementation is in progress and is not accepted.

## Resume Protocol
1. Read only README + progress + current milestone file.
2. Inspect worktree/dirty changes, environment and existing harness before editing.
3. Implement only current milestone; record evidence and failure diagnosis in progress.
4. When and only when acceptance criteria PASS, advance `Current Milestone` and `Next Step` in progress.
5. During context compaction, reload these three files, not every completed milestone.
