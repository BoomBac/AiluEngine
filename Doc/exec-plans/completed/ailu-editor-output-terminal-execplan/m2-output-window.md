# M2 — Output Dock Window and Virtualized Console View

> **Prerequisite:** `progress.md` states **M1 PASS** with build/stress evidence. Read global README first. This file covers native UI integration only; do not start ConsoleRegistry/OS pipes here.

## Purpose

Turn the structured records delivered by `EditorLogService` into a Unity-style Output/Console panel: compact rows, severity counts, filters, search, collapse, detail inspector, copy, optional source navigation and stable persistent Dock behavior.

## Scope

- Add reflected `OutputWindow : DockWindow` built using native `UI::*` elements.
- Implement `OutputLogDataSource : UI::TableDataSource`, with stable sequence-based row references.
- UI interactions: severity toggles, module filter, text search, clear view/history, collapse duplicates, follow tail, details, copy, counts. Accessible menu entry with Open-or-Activate semantics.
- Save basic presentation choices without serializing every log line or historical editor data.
- Ensure the window can close, reopen and survive Editor restart from saved `dock_layout.json`.

## Non-goals

No new logger implementation, no ImGui replacement, no process stream in Output, no full VS Code hyperlink engine, no custom text table layout implementation, no new general-purpose log file parser.

## Architecture Constraints

- UI thread owns all widgets and the data source. `EditorLogService` may receive from other threads but only main-thread `DrainPending` changes UI-facing snapshot/view state.
- Use the existing **virtualized** `UI::UITableElement`; `TableDataSource::GetRowCount()` + `GetCellText()` and optional `DrawCell()`; do not instantiate one child widget per record.
- Use immutable record `u64 _sequence` to resolve row to record; don't keep pointers into an evictable queue/history.
- `GetCellText()` must not rebuild an entire filtered log array or allocate huge strings; precompute/reuse visible indices/cache per revision/filter change.
- Allow closed window while collector remains active. Hidden/closed Output stops UI updates, not logging.
- Dock's persisted `Type::Find()` demands `ACLASS()` and `GENERATED_BODY()` + proper registration in actual build/reflection tooling.
- `UI::InputBlock` is a single-line input with `_on_content_changed`; its current `CommitEdit()` logs on edit. Avoid self-feeding search keystroke logs; introduce a narrowly scoped suppression/logging fix only if needed and record why.

## Baseline

1. `Editor/Inc/Dock/DockWindow.h` offers `DockWindow`, `_content_root`, `Update(dt)`, and `SaveDockLayoutState` / `LoadDockLayoutState` / `OnDockLayoutLoaded`.
2. `Editor/Src/Dock/DockManager.cpp`: `HasDock(persistence_id)`, `ActivateDock(type_name)`, `AddDock(Ref<DockWindow>)`, `RequestRemoveDock`; saved leaves reconstructed by reflection in constructor; layout file is `Editor/dock_layout.json`.
3. `Editor/Src/Widgets/EditorLayer.cpp`: `OnAttach()` initializes `DockManager`, `BuildEditorChrome()` creates toolbar `Menu`; `OnUpdate()` calls `DockManager::Get().Update(dt)`.
4. `Editor/Src/Widgets/FrameDebuggerWindow.cpp`: `BuildUI()` uses `_content_root->AddChild<UI::VerticalBox>()`, `UI::SplitView`, `UI::UITableElement`, `SetDataSource`; `FrameDebuggerWindow` itself isn't declared with ACLASS, so use a reflected class like `CommonView` or `PlaceActor` as the reflection model instead.
5. `Engine/Inc/UI/Table/TableDataSource.h`: data source virtuals. `Engine/Inc/UI/Element/UITableElement.h`: `Refresh()`, `SetSelectedRow()`, `_on_selection_changed`, sorting and style customization. `Engine/Src/UI/Element/UITableElement.cpp`: draws visible rows only.
6. `Engine/Inc/UI/Basic.h`: `UI::InputBlock`, `_on_content_changed`, `SetContent(content, trigger_event)`. No ready-made module ComboBox confirmed from archive; implement a simple popup/dropdown with existing `UI::Menu` if necessary.

## Target Outcome

A robust Output window in Editor bottom Dock region (placement best effort; user can dock manually): logs arrive live, filtering/collapse doesn't alter stored events, selections stay correct through ring eviction, scrolling and 100k-record simulation remain usable. Restart restores window and preferences without duplicate docks.

## Relevant Files

**Create:** `Editor/Inc/Widgets/OutputWindow.h`, `Editor/Src/Widgets/OutputWindow.cpp` (or match existing Widget file grouping).

**Modify:** `Editor/Inc/Widgets/EditorLayer.h`, `Editor/Src/Widgets/EditorLayer.cpp`; `Engine/Inc/UI/Basic.h`, `Engine/Src/UI/Basic.cpp` **only if needed** for search log suppression/submit. UI table only if a reproducible missing scroll or double-click behavior blocks required features.

**Reference:** `Editor/Inc/Widgets/CommonView.h`, `Editor/Src/Widgets/FrameDebuggerWindow.cpp`, `Editor/Src/Dock/DockManager.cpp`, `Engine/Inc/UI/Element/UITableElement.h`, `Engine/Inc/UI/Table/TableColumn.h`, `Editor/Src/Widgets/AssetBrowser.cpp` for file-explorer opening patterns.

## Data / view-model design

Keep records in `EditorLogService`; the window maintains a compact `Vector<u64> _visible_sequences` (or `Vector<index+revision>` with explicit invalidation), plus optional `VisibleLogRow{sequence,repeat_count}` for collapse. A separate `OutputFilter` stores flags:

```cpp
struct OutputFilter
{
    bool _show_info = true;
    bool _show_warning = true;
    bool _show_error = true;
    bool _show_debug = false;
    bool _show_trace = false;
    bool _collapse_duplicates = false;
    bool _follow_tail = true;
    String _search_text;
    bool _all_modules = true;
    ELogModule _module = ELogModule::kGeneral;
};
```

Keep `Fatal` included with the Error toggle by default and make the mapping explicit. `_show_debug` and `_show_trace` are UI filtering only: they cannot recover messages suppressed by `LogMgr` at logging-time. Display a succinct hint if this confuses users; don't silently change global trace level.

### Filter / collapse algorithm

- Build view only when `{service revision, filter revision}` changes; use incremental append if only records were appended and no collapse/sort-sensitive filter complicates it.
- Search case-insensitive over message, module label and source path/function (UTF-8/WString conversion handled once per cached record or in the refresh pass). Search input updates are debounced/coalesced **per frame**, no full-table refresh on every key event if not necessary.
- Collapse key: `{level, module, message, file, line}`; **document grouping rule** (adjacent-only or full-window). Prefer adjacent-only initially: deterministic, O(n), preserves chronology. Show repeat count; selected detail displays first/last timestamp and count if available.
- Never mutate/delete original `EditorLogRecord` when collapsing. A new arriving record can update last group without rebuilding the entire history.
- When retained history evicts old records, remove invalid sequence entries and clear selection when its ID disappears, rather than accidentally showing another record occupying the same ring position.
- For sorting: initially disable user sorting on the live log list so chronological streaming remains deterministic. A manually sorted view may be a later feature.

### UI layout

```text
Output (DockWindow)
 ├─ toolbar: Clear | Collapse | Follow | [module filter]
 ├─ levels: Info(count) | Warning(count) | Error/Fatal(count) | Debug | Trace
 ├─ search: [InputBlock]
 ├─ UITableElement: Time | Level | Module | Message | Source (virtual rows)
 └─ details: selected full message + filename/line/function/thread/timestamp
```

Use native `VerticalBox`/`HorizontalBox`/`SplitView` for resizable details if existing controls support correct layout; otherwise a fixed-height lower details panel with clear separator is enough for M2. Set `SizePolicy` intentionally so the table consumes remaining area. Set table source lifetime before window shutdown; detach via `SetDataSource(nullptr)` before destroying source.

`Copy` can use established clipboard utility after verifying it exists; if not, implement a minimal Windows clipboard adapter only if the repo has no abstraction, and record Windows-only scope. **Source navigation is conditional:** search first for a code-editor launch service; `AssetBrowser::OpenInFileExplorer` opens Explorer, not a source editor. Do not claim actual file:line navigation unless validated; if no robust path resolver exists, provide `Copy File:Line` as M2 fallback and mark richer source jump as Known Follow-up.

## Implementation steps

- [ ] **Step 1 — verify gate:** read `progress.md`; if M1 not PASS, stop. Inspect live `Output` service and native widget lifecycle, confirm reflection generation command and current `DockManager` structure.
- [ ] **Step 2 — reflected class:** add `ACLASS()` `OutputWindow` with `GENERATED_BODY()`, default constructor and proper type registration. Derive `DockWindow("Output", {width,height})`; ensure new cpp is included by actual build system.
- [ ] **Step 3 — layout skeleton:** create toolbar, counters, search input, virtual table and details section. Start with fake in-memory records for UI smoke test, then switch to M1 service. Do not create custom row widgets.
- [ ] **Step 4 — data source:** implement `OutputLogDataSource` and explicit row→record-by-sequence mapping. Verify render/update uses `UITableElement::Refresh()` rather than rebuilding widget tree; hook `_on_selection_changed` to populate details. Protect against row count/int overflow (service defaults well below `INT_MAX`).
- [ ] **Step 5 — filters:** add toggle buttons, module menu, search, severity counts. Prefer one `RefreshView()` function called at most once per frame when revision/filter changes. Count meaning must be documented: current retained history vs current filtered rows (recommend retained history per level, plus filtered total indicator).
- [ ] **Step 6 — collapse + follow:** implement adjacency collapse and count badges/cell text; follow tail only when enabled and user has not manually scrolled away from bottom. Check `UITableElement` scroll API: add a narrowly scoped `ScrollToBottom()` public helper if absent. Never reset scroll position on every `Refresh()` if user is reviewing older logs.
- [ ] **Step 7 — commands:** Clear button should clear *view or service history* according to explicit label/tooltip (prefer `Clear History` calls service API and leaves future logging active); add copy row/details using actual clipboard path, and optionally validated source navigation. Ensure buttons' callbacks cannot log every frame or recursively update service from `Print()`.
- [ ] **Step 8 — Dock integration:** in `BuildEditorChrome()` add a `Window` submenu to native `UI::Menu` containing `Output`. Handler: `if (!DockManager::Get().ActivateDock(OutputWindow::StaticType()->FullName())) { if (!DockManager::Get().HasDock(id)) AddDock(MakeRef<OutputWindow>()); }` using correct type ID/persistence ID; confirm idempotence. Do not instantiate if restored. Optionally put Output into default bottom tab region *only for fresh layout*; do not overwrite existing layout.
- [ ] **Step 9 — persistence:** override `SaveDockLayoutState` / `LoadDockLayoutState` for filter/collapse/follow preferences; follow `GraphEditorWindow.cpp` example and defer widget-dependent application to `OnDockLayoutLoaded`. Only persist view settings, not 10k log payloads. Test restart and close/reopen.
- [ ] **Step 10 — test:** run build, GUI smoke, 100k synthetic events, focus and restart tests; update `progress.md` and stop at gate.

## Edge cases and risks

- Search changing while logs stream: no double refresh in same frame; selection ID remains stable or clears with explanation.
- Empty log history: all counters zero, table no rows, details shows placeholder; nothing crashes.
- Big multiline log: show one-line truncated summary in table, full text in details; no row height explosion.
- Large WString/non-ASCII path: filtering and copy preserve contents, no invalid UTF split.
- Collapse groups change when old records are evicted: first row consistency and repeat count correct.
- Closing/reopening repeated 10x should not double-register callbacks or create 10 data sources.
- `UI::InputBlock::CommitEdit()` logs after every typed edit in original code: avoid a search → logging → search flood; if changing global behavior, verify other inspector inputs are not broken.
- Dock layout corrupt or a type missing: recover gracefully; don't let missing `OutputWindow` type corrupt layout save path. **Back up an existing user layout in manual tests.**

## Validation

| Test ID | Procedure | Expected |
|---|---|---|
| M2-T01 | Build including reflection regeneration | Output class resolved by `Type::Find` and links correctly. |
| M2-T02 | Open/close/open from Window menu repeatedly | Single instance active; no duplicate dock or dangling UI registration. |
| M2-T03 | Emit known level/module/source logs | Correct rows, counters, details and `kFatal` mapping. |
| M2-T04 | Search / level toggles / module filter | Only expected rows visible; no logger-level changes. |
| M2-T05 | Repeated adjacent lines, toggle Collapse | One visible group with accurate count; uncollapsed events intact. |
| M2-T06 | Scroll to middle; continue streaming | Manual scroll stays; Follow re-enabled scrolls to newest. |
| M2-T07 | Produce 100k synthetic events with M1 history cap | Memory bounded; UI rows virtualized; no freeze / per-row Widget explosion. |
| M2-T08 | Select old event then evict it | Selection clears safely, never binds to a newer record. |
| M2-T09 | Change filters, save/restart Editor, reopen | Dock restores and view preferences load; no missing type errors. |
| M2-T10 | Empty search, multilingual messages, copy and bad path | No crash, copying accurate, unresolvable path handled harmlessly. |
| M2-T11 | Run GUI with Output closed and batch/offscreen | Logger service still works, no mandatory UI dependencies. |

Record test platform, data volume, peak memory / UI frame-time estimate and any performance bottleneck; don't fabricate perf results.

## Acceptance Criteria (ALL required)

- [ ] Native reflected Output dock builds, opens/closes/reopens through native menu and survives restart.
- [ ] Data source uses `UITableElement` virtual rendering; no one-element-per-record implementation.
- [ ] Level/module/search/collapse/detail/clear/copy/follow work; user scroll position respected.
- [ ] No stale record references through buffer eviction; bounded-memory 100k-event test passes.
- [ ] No recursive self-logging from search input and no changing global logger threshold as a UI filter.
- [ ] Exact tests and code diff recorded in `progress.md`; M2 state PASS.

## Milestones

M2 of M5; proceed to `m3-engine-console.md` **only after M2 PASS**.

## Current Milestone

M2, pending completion of M1. Live authoritative state is `progress.md`.

## Progress

Not implemented at plan creation; track completed Step checkboxes plus tests in `progress.md`.

## Decision Log

Proposed: chronological read-only table, adjacent collapse, stable sequence selection, filter preferences only in Dock JSON. Record any deviations.

## Surprises & Discoveries

Reflected dock restoration differs from manually created `FrameDebuggerWindow`; register Output properly. Existing editor has no verified source-editor jump service; do not substitute Explorer and label it code navigation.

## Known Follow-ups

Hyperlink patterns for compiler diagnostics and cross-window standardized virtual terminal text rendering can be improved later. Not a gate unless user explicitly changes scope.

## Outcomes

Pending. Add screenshots or UI walkthrough notes, performance evidence, and commit hash to `progress.md`.

## Resume Protocol

Read only global README, status and M2; inspect existing UI diff; finish remaining unchecked steps, re-run entire M2 validation, then transition to M3.
