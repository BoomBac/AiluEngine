# Inspector multi-entity editing

## Purpose / Scope
First version: common component sections and Transform multi-edit, per-axis mixed values,
one undo entry per typing/drag session, persistence dirty tracking, safe target resolution.
Other custom/reflected editors show an explicit unsupported message in multi-selection.
Multi-selection component add/remove, enabled flags and Prefab actions are outside this version.

## Architecture constraints / Relevant files
Selection remains Editor-owned. ObjectDetail aggregates selection; component editors opt in.
Transform edits use existing setters and reset render interpolation. Commands hold weak Scene ownership
and persistent entity GUIDs, never component pointers. Engine serialization is unchanged.
Relevant: ObjectDetail, ComponentEditorContext, IComponentEditor, TransformComponentEditor,
TransformPropertyCommand, Test/InspectorMultiEditSmoke.cpp.

## Current milestone / Progress
- Implementation, final Debug build, regression rerun and targeted diff review complete.

## Decision Log
- Keep single-selection behavior; display only component intersection for multi-selection.
- Aggregate each axis independently; absolute local-coordinate writes preserve other axes.
- Coalesce live input updates until Return, focus loss, drag release, or editor teardown.
- Commit pending edits before Undo/Redo through a CommandManager event, so Ctrl+Z cannot undo an older edit.
- Play-mode Scene copies lack shared ownership; preserve direct setter-based editing and clearly label
  runtime edits as excluded from Undo. Editor scene commands continue to use weak ownership.

## Surprises & Discoveries
- WorldOutline and Selection already support multi-selection; ObjectDetail used FirstEntity only.
- InputBlock sends changes during typing and dragging; no generic commit event exists.
- Repository ExecPlan template path is absent; this plan includes the required sections directly.

## Validation / Outcomes
- Final `cmake --build --preset build-debug` passed, including Editor and InspectorMultiEditSmoke.
- InspectorMultiEditSmoke passed: actual nine-input property panel construction, per-axis mixed display,
  refresh and editing-text preservation; position, scale and rotation edits; batched undo/redo;
  pending-edit commit before Undo; scene edit revision; entity-document JSON round trip; deleted entity/reused storage, removed component
  and expired scene safety. Runtime shared setter path also checked.
- Evidence: Saved/InspectorMultiEditValidation/20261007/smoke.txt.
- Full Editor mouse/keyboard interaction, linked-asset Ctrl+S and actual Play-mode lifecycle remain
  unverified; headless panel/serialization checks do not claim those acceptance levels.
- Outcome: first-version common-component Inspector and Transform multi-edit are implemented and
  available for interactive review. No scene/assets, editor configuration or runtime state were changed.
