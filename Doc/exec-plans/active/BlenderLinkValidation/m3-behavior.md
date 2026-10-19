# M3 — Stateful + Real-Scene Regression

## Purpose
Build the full autonomous lifecycle suite across source changes, reconnection, persistence and large Blender scenes.

## Scope
Incremental mutation driver on Blender main thread, idle/no-resend validation, reconnect/partial snapshot error paths, copied `.blend`/project save-reopen, BMW27/Classroom with native export and exact packet replay, repeated performance telemetry, aggregate reports.

## Non-goals
No saving user-owned source scene, no automatic baseline approval, no indefinite agent self-repair.

## Architecture Constraints
Version 1 protocol unchanged; main-thread bpy/Cycles; Engine scene owner thread. Each mutation must correlate to actual Engine update; copy files for persistence; preserve user processes/profile and dirty work. Large scene timeouts are bounded and categorized explicitly.

## Baseline
`Test/BlenderPersistenceSmoke.py/.cpp`, `Test/BlenderInstanceSyncSmoke.py`, `Test/BlenderLinkPerformance.py`, real BMW27/Classroom regression notes.

## Target Outcome
Declarative suite runs isolated stateful and large-scene cases and emits exhaustive coverage/provenance; failure produces a reproducible narrow case with packet/input/output artifacts.

## Relevant Files
`Tools/Blender/ailu_live_link/{addon,dirty_tracker,object_sync}.py`, `Test/BlenderPersistenceSmoke.py/.cpp`, `Test/BlenderLinkPerformance*.py`.

## Milestones
M3 gated by accepted M2; optional nightly self-hosted Windows GPU CI afterwards.

## Current Milestone
M3 only after M2.

## Progress
- [x] Added `Test/BlenderLinkValidation/m3_stateful_smoke.py`, an actual Blender 5.0.1 scene driver for tracker-level idle, transform, material, mesh, add/delete, reconnect-baseline, and copied save/reopen GUID checks. It writes a per-check JSON report and labels that a recording link was used.
- [ ] Native Engine RPC correlation for every transition, packet capture/replay, missing-End reconnect recovery, camera/light/visibility/instance cases, and named BMW27/Classroom regressions remain unimplemented.

## Decision Log
Native testing proves Blender->wire->Engine; replay isolates Engine receiver/regressions and must have exact packet SHA equality. Preserve source and compare identical hardware/settings for performance.

## Surprises & Discoveries
BMW tests previously exposed nested collection-instance, geometry/UV issues and capture-before-import failures. Classroom exposed large baking/empty evaluated meshes and unfinished FullSync. Make each named regression, not a casual screenshot check.

## Validation
Automate idle, transform-only, mesh/material/texture edit, add/delete, instance, visibility, camera/light and GUID stability; reconnect with edits and missing-End injected failure; copied persistence offline reopen and resync. Real BMW/Classroom native L2 assertions + optional L3 captures. Strict replay 2+ trials with identical packet SHA256 and medians, explicit execution settings; deliberately seeded error must FAIL and recovery succeeds.

The initial M3 smoke validates only the dirty-tracker subset. It does not satisfy native Engine, protocol replay, reconnect recovery, or large-scene acceptance.

## Acceptance Criteria
All required synthetic state transitions PASS; BMW/Classroom each either pass supported assertions or are explicitly blocked/inconclusive with artifacts (never false PASS). Cleanup and input hashes pass after repeated runs. Performance labeled separately from correctness.

## Known Follow-ups
Self-hosted GPU CI/nightly scheduling and expanded shader-oracle coverage after M3.

## Outcomes
Partial implementation only. Do not mark M3 accepted until the outstanding Engine and regression gates have repeatable evidence.

## Resume Protocol
Use M0-M2 evidence and `progress.md`; mark M3 accepted only with all required checks and repeatable run manifests.
