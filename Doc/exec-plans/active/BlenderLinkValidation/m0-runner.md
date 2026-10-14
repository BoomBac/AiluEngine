# M0 — Safe Native Runner

## Purpose
Perform one genuine native Blender Full Sync into a fresh Editor and collect trustworthy L1 protocol/lifecycle evidence without affecting user state.

## Scope
Generalize machine paths in existing runner; suite/config CLI and dry-run; process ownership, unique port/project, isolated logs, SHA256 manifests, exact current-run Begin/End/apply, timeouts, release sentinel, cleanup and structured report.

## Non-goals
No new independent structural oracle, no final-image correctness claim, no incremental tests.

## Architecture Constraints
Never kill unrelated processes, save source blend or clobber an active Editor config; prefer disposable project and config. If shared config must be touched, exclusive lock, pre-hash backup, and conditional restore. Blender bpy main thread; Editor scenes/GPU owning thread.

## Baseline
`Test/BlenderLinkPerformance.py` and `Test/BlenderLinkPerformanceExport.py` already provide much of the native workflow but have hard-coded paths; current `FileAppender_log.txt` may contain stale data.

## Target Outcome
Proposed `Test/BlenderLinkValidation/runner.py` (not yet existing) can run an owned native fixture, report L1 PASS/FAIL, and prove source untouched and all owned child processes exited.

## Relevant Files
`Test/BlenderLinkPerformance.py`, `Test/BlenderLinkPerformanceExport.py`, `Plugins/BlenderLiveLink/Src/BlenderLiveLinkPlugin.cpp`, `Tools/Blender/ailu_live_link/connection.py`.

## Milestones
M0, independent gate; M1 cannot start until accepted.

## Current Milestone
M0.

## Progress
Pending.

## Decision Log
Use exact packet sender End AND Engine-applied End; do not count a previous log marker as current success. Keep process owner PID list for cleanup.

## Surprises & Discoveries
Current exporter waits for a `release` sentinel after exporting. The Engine log reports `FullSync import:` after End, but that line needs run/session correlation and apply-error accounting.

## Validation
Tests: (1) dry-run has zero side effects; (2) tiny native scene sends/applies End with zero errors; (3) invalid Blender path fails before work; (4) injected timeout/error yields FAIL, releases exporter and owned process; (5) consecutive reruns have unique evidence, no stale-log pass; (6) source hashes and saved config state match or report conflict explicitly.

## Acceptance Criteria
All six cases PASS with exact commands/PIDs/hashes, `summary.json`, logs and restoration evidence; no unrelated process touched. No L2/L3 acceptance claim.

## Known Follow-ups
Only after M0, introduce scene-value oracle.

## Outcomes
Pending.

## Resume Protocol
Update `progress.md` with exact M0 evidence and remaining failures; advance only once all six checks pass.
