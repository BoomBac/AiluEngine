# Blender Link Validation Progress

## Current Milestone
M0 — not started.

## Progress
- [x] Existing source/testing capabilities inspected.
- [x] Reusable Codex/Claude Skill, references, example suite and split ExecPlan designed.
- [ ] M0 runner implemented and locally validated.
- [ ] M1 independent structural oracle accepted.
- [ ] M2 GPU visual fixture tests accepted.
- [ ] M3 stateful and real-scene regression accepted.

## Decision Log
Prefer deterministic assertions and a native Windows run-owned Engine process. No automatic saving of original .blend or user Editor state. Keep GPU evidence separate from protocol evidence.

## Surprises & Discoveries
Current native performance test has hard-coded machine paths; exporter intentionally stays alive until a `release` sentinel. The Editor exposes paginated query + GUID inspection already.

## Validation
Source inspection only. No runtime PASS claimed. Validate generated JSON schema separately on implementation; dry-run should not start processes.

## Blockers
Local installed Blender, Editor DX12/GPU session, project and test scenes are not accessible to this design-authoring environment. Implementer must discover these on the actual workstation.

## Next Step
M0: refactor run-owned native test orchestration, current-run End correlation, timeouts and cleanup without assuming a specific install path.

## Known Follow-ups
Bulk scene snapshot RPC only if independently measured necessary. Nightly self-hosted GPU runner after M3.

## Outcomes
Design/handoff prepared; implementation and live validation pending.

## Resume Protocol
Load README, this file and `m0-runner.md`. After acceptance update this file with exact run ID, hash, verdict and next milestone.
