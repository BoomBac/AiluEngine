# All-component Inspector multi-edit

## Purpose
Extend the first-version Transform multi-edit to every registered Inspector component and its editable fields.

## Scope / Non-goals
Common component intersection, mixed scalar/vector/enum/boolean/asset values, component-specific setters,
dynamic Script properties, Collider2D shapes and mesh material slots/parameters, undo/redo, dirty tracking.
Read-only identity/hierarchy fields remain read-only. Prefab Apply/Revert is outside this change.

## Architecture constraints
Editor owns selection and property bindings. Resolve scene/entity/component for each read/write; never
retain component pointers in commands. Reuse camera, animation, script and material setter/lifecycle APIs.
Preserve all existing dirty work, including EditorConfig, main_widget and dock_layout.

## Relevant files
ObjectDetail, ComponentEditorRegistry, MultiComponentEditor, component schemas, ReflectedPropertyPanel,
UI CheckBox mixed state, InspectorMultiEditSmoke, editor-runtime-boundary architecture.

## Current milestone
Complete: M1-M3 implementation and headless acceptance.

## Progress
- [x] M1: bindings, mixed-value controls and property-scoped undo sessions.
- [x] M2: all registered component schemas, reflected fields and dynamic/custom properties.
- [x] M3: registration coverage, setter/side-effect/array/material/lifetime regressions and build-debug.

## Decision Log
- Keep existing single-selection editors. Multi-selection uses explicit field bindings with the same setters.
- A property applies only where its schema/type exists on every target; mismatched subtype fields are omitted.
- GUID and hierarchy relationships are not writable batch fields.
- Component enabled state is editable; transient bake/probe/spawn actions do not create history entries.
- Material commands retain their original assets; all other bindings re-resolve ECS storage by GUID.

## Surprises & Discoveries
- Several custom components are unreflected. Reflection alone cannot cover their editable properties.
- Dynamic lists and material properties need identity/type-aware intersection, not byte copying.
- A first regression exposed range-clamped Undo changing an original out-of-range value. Input clamps now
  apply only to new edits; undo restores exact snapshots.
- CLightProbe construction allocates GPU resources. Its editor factory is verified headlessly, but its
  actual fields require a running graphics device. The other 24 component panels are instantiated.
- Final test compile initially used the nonexistent ECullMode::kOff; corrected to kNone.

## Validation
Registration source matches the 25-component smoke matrix. Final smoke passed 189 field undo/redo checks,
mixed-value UI, Script intersection, animator coupled state, Collider2D array/profile snapshots, material
slots/shared assets, camera subtypes, vector axes, Transform serialization and deleted/expired scene safety.
Final `cmake --build --preset build-debug` passed; `InspectorMultiEditSmoke.exe` exited 0 after material
asset ownership, exact snapshot restore and schema-refresh fixes. Shared-material reassignment regression
confirms Undo restores the original asset while leaving the replacement untouched. New schema/editor/test
files have no lines over 120 columns; targeted tracked-file `git diff --check` passed. Generated UI header
changes are AiluHeadTool line-number updates, with no manual generated-code edits.
Evidence: `Saved/InspectorMultiEditValidation/20261007-all-components/registration-coverage.json`,
`build-debug.txt`, `smoke.txt`.
Interactive Editor mouse/keyboard, Ctrl+S/reopen, GPU LightProbe and loaded shader/texture asset acceptance
are separate and have not been performed. No new serialization support is added for previously unhandled
component types.

## Outcomes
All 25 current registered component types have multi-edit-capable editor factories. Common fields support
mixed values, per-field and per-axis batch edits, scene/material dirty tracking and GUID-safe Undo/Redo.
Single-selection custom editors remain intact; read-only IDs/hierarchy remain non-editable. Component
enable flags are batch editable. Prefab actions, component add/remove and entity enable batch editing are
outside scope. Existing unrelated configuration/layout/material asset edits were preserved.
