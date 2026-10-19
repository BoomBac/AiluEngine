---
name: ailu-blender-link-validation
description: Test AiluEngine Blender Live Link autonomously with isolated native Blender/Editor runs, independent scene-state oracles, RenderGraph captures, incremental/reconnect checks, and evidence-backed PASS/FAIL reports.
metadata:
  short-description: Automated Blender Link verification
---

# AiluEngine Blender Live Link Validation

Use this Skill for Blender Link correctness, regressions, source-scene parity or automatic import tests. The Skill chooses tests and diagnoses failures; deterministic test scripts own execution and verdicts. **A screenshot, packet replay, log marker or compile success alone is never proof of full correctness.**

## Load only the needed guidance
- Start with [references/workflow.md](references/workflow.md) for process orchestration and safety.
- Read [references/oracles.md](references/oracles.md) when implementing or evaluating assertions.
- Read [references/cases.md](references/cases.md) to select a focused suite.
- For evaluated geometry, wrong materials or bake UVs, use [references/material-bindings.md](references/material-bindings.md).
- For material visual reports, use [references/visual-report.md](references/visual-report.md). The Markdown report must embed
  a Blender Eevee background render, Engine GBuffer base color, and Engine final output images.
- Use `.agents/skills/ailu-render-frame-validation/SKILL.md` for GPU frame capture; do not duplicate its procedure.
- For incomplete automation infrastructure use `Doc/exec-plans/active/BlenderLinkValidation/README.md` and **only the current milestone** from `progress.md`.

## Execution contract
1. Detect actual repository root, installed Blender, Editor.exe, Live Link DLL, add-on source/installed version, test project and scene. Never assume the hard-coded paths in historical tests are valid.
2. Choose evidence level: `static`, `protocol`, `native`, `structural`, `visual`, `incremental`, `persistence`, or `replay`. Default to small fixture/native/structural; expand only as relevant.
3. Run on the user's local **Windows graphical session with a DX12 adapter** for visual evidence. `blender --background` is supported; **headless Editor GPU execution is not established**. If requirements are absent, mark GPU evidence unavailable, not PASS.
4. Do not terminate or alter user-owned Blender/Editor processes. Run only owned children with a unique localhost port, disposable project and exclusive lock where shared config/GPU would conflict.
5. Never save the user's original `.blend`, overwrite Editor settings, alter global Blender preferences, accept new golden images or discard dirty work. Backup+hash+conditional restoration for any shared state touched.
6. Record exact SHA256 of blend, Editor.exe, Engine DLL, Live Link DLL, add-on, `packets.bin` when present, GPU/driver/settings, code revision, run ID and PIDs.
7. For native Full Sync require both an exported Begin/End with drained Blender send queue AND the CURRENT run's Engine-applied End with no Apply failure, queue overflow or unexpected disconnect. Do not rely on fixed delays or old logs.
8. Compare independent bpy/evaluated-depsgraph expectations to actual Engine entities via `scene.query_entities` (paginated) and `scene.inspect_entity` by GUID; treat missing inspection fields as INCONCLUSIVE.
9. For visual cases, verify capture manifest, selector, actual engine frames and color-space handling before geometric/GBuffer/ROI assertions. Blender Cycles final pixels and Ailu Standard Lit need not match exactly.
10. On failure, preserve repro/packet/capture evidence, classify root cause, repair only if requested, and rerun the smallest affected suite; never self-approve an updated golden.
11. In a `finally` block release the exporter, stop only owned processes, verify source hashes and restoration. Incomplete cleanup is a failure requiring user notice.

## Reporting
Produce `summary.json` with statuses `PASS | FAIL | INCONCLUSIVE | SKIPPED` per case and highest confirmed evidence level; `assertions.json` with object/path/expected/actual/tolerance, `failures.json`, `run.json` with SHA256 provenance, `restoration.json`, current-run logs, and RenderGraph manifests/captures when tested. Store in a unique directory **beside the selected Editor.exe**. State clearly what was not tested.

## Present implementation status
`Test/BlenderLinkValidation/runner.py` implements isolated native runs with provenance/restoration and focused `m1`, `m2` and `materials` fixtures. The material suite compares an independent evaluated-mesh reference against wire and Engine bindings, detects unexpected checker fallback, captures controlled albedo markers and verifies three bounded incremental material edits. A supplied scene uses `--source-blend` and is copied before export. These suites do not establish universal shader parity or full M3 reconnect/persistence acceptance; consult the active split ExecPlan for outstanding gates.
