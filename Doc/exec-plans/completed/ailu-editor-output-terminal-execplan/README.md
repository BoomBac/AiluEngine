# AiluEngine Editor Output + Terminal — ExecPlan

> Source baseline: `AiluEngine_Source(20261020-033219).zip`, supplied on 2026-10-20. This is an implementation plan, **not implemented code**. Work in the actual repository, not inside the archive. All paths below are relative to repository root `AiluEngine/`.
>
> **Read this file and `progress.md` at the start of every session. Read only the CURRENT milestone file; do not preload all milestone files.**

## Purpose

Extend the native AiluEngine Editor with two first-class dockable windows:

- **Output**: structured, searchable, bounded, virtualized engine/editor log viewer (Unity Console usability + UE category filtering).
- **Terminal**: engine-console command runner first, then a Windows pipe-backed one-shot external task runner; output capture, cancellation and build/test integration.

Unify the diagnostic *experience* without conflating engine logs, external process output and undoable editor actions. Favor reliability, reusability and headless compatibility over emulating a full terminal.

## Scope

1. Capture structured `LogMessage` records from `LogMgr`, retain a bounded history, permit batched UI consumption.
2. Add `OutputWindow : DockWindow` with level/module filters, search, collapse, details, follow-tail, clear and copy; optional open-in-editor action if a reliable source-file resolver is available.
3. Implement a typed engine command registry/parser/dispatcher; add a `TerminalWindow` for commands, history and completions.
4. Extend Windows process execution with *opt-in* redirected stdout/stderr, asynchronous nonblocking draining, graceful ownership and cancellation.
5. Integrate build/package and explicit external commands into Terminal and preserve exit status; validate full flow and shutdown.
6. Support restored Dock layouts and noninteractive modes without GUI dependencies.

## Non-goals

- Full VT100/ANSI terminal emulator, ConPTY, ncurses, interactive PowerShell session, interactive stdin, terminal mouse support, ANSI 24-bit colors, tabs of persistent shells.
- Replacing `LogMgr` globally, changing all `LOG_*` call sites, shipping a network console, remote arbitrary-command API, or making Output a logging *source*.
- Changing `SceneCommand`/Undo semantics, modifying render/thread scheduling architecture, implementing a general text editor or rebuilding DockManager.
- Changes to game runtime UI outside a small, justified reusable helper; Windows 7 ConPTY support is impossible and not attempted.
- New heavy third-party dependencies or breaking existing non-redirected process callers.

## Architecture Constraints

1. **User code style:** identifiers use `lowercase_with_underscores` for locals, `_lowercase_with_underscores` for members, `s...` for statics, `kCamelCase` for constants, Pascal/CamelCase for classes/functions following repository conventions. Aim for 160 columns. Existing API fields such as `ProcessStartInfo::CommandLine` must retain their established names.
2. **UI:** native `UI::*` widgets + `DockWindow`, not a separate ImGui-only implementation. Reuse `UITableElement` virtualization (`TableDataSource`). Never create one `UI::Text` per log entry.
3. **Logging:** `LogMgr::AddAppender(IAppender*)` currently transfers effective ownership to `LogMgr::Finalize()`, which deletes each pointer. The appender cannot refer to a destroyed editor service; no recursive `LOG_*` from `IAppender::Print()`. Preserve serialization under `_output_mutex` and avoid long/contended code in `Print()`.
4. **Timing:** attach log sink directly after `LogMgr::Init()` in `EditorApp::Initialize()` *before* `Application::Initialize()`. Disconnect/invalidate service safely before `LogMgr::Shutdown()`; preserve service lifetime through last appender invocation.
5. **Boundedness:** cap *both* pending queue and retained history by count and approximate payload bytes. Default proposed: history 10,000 records / 16 MiB, pending 2,048 records / 4 MiB; write tests before tuning. Old-record eviction is allowed; all drops are observable without recursive logging.
6. **Thread model:** producers from any thread; data source and all UI mutators only on Editor UI thread. On busy frames cap per-frame draining (e.g. 512 records or 2 ms). No cross-thread widget access. Command callbacks that mutate engine state run on approved thread; external jobs run off UI thread.
7. **Process:** preserve `Process` API default behavior; redirect only with opt-in options. No blocking `ReadFile`/`Wait(INFINITE)` on UI thread. Distinguish process still running / finished and pipes still draining / fully completed. Always drain until EOF to avoid missing output after exit.
8. **Windows support:** pipe + `CreateProcessW` is chosen for compatibility with legacy Windows. No `ConPTY` or other Windows 10-only API on the minimum path. Do not assume Win7 test environment exists; mark Win7 as unverified if unavailable.
9. **Persistence:** `DockManager` restores leaves using `Type::Find(type_id)` + `CreateInstance<DockWindow>()`; persist new dock types with `ACLASS()`, `GENERATED_BODY()` and the project's generation/registration procedure. Don't hand-edit generated reflection files unless the repo's official tool explicitly requires it. Idempotent Open-or-Activate, don't duplicate docks.
10. **Lifecycle:** GUI closed != service stopped. Service survives Output window close. Terminal task lifetime is owned by a service/session independent of window visibility. On shutdown, prevent new work; cancel/wait with bounded policy, drain, then tear down safely.
11. **Security:** external shell execution is manual/local-only. Never run arbitrary OS commands from model/remote automation input, menu URI, imported asset or engine console alias. Quote executable path/arguments carefully. Do not merge shell-interpreted command strings into APIs that promise direct process spawning.
12. **Tests and change control:** single milestone per branch/commit whenever practical, no unrelated refactoring, smoke test on real repository. If needed build files/tests/tooling are missing, report exact blocker; do not invent paths/commands or claim success.

## Baseline — verified in archive

| Existing file | Verified fact / implementation consequence |
|---|---|
| `Engine/Inc/Framework/Common/Log.h` | `LogMessage` has `_level`, `_message`, `_file`, `_line`, `_function`, `_thread_id`, `_time_str`, `_thread_name`; no stored `ELogModule`. `IAppender::Print(const LogMessage&)`; `LogMgr::AddAppender(IAppender*)`. |
| `Engine/Src/Framework/Common/Log.cpp` | Constructor assigns `_timestamp`, `_time_str`, `_thread_name` but not `_thread_id`; `ToDetailString()` calls `TimeMgr::CurrentTime()` again; `Initialize()` registers `OutputAppender`; `Finalize()` deletes appenders; `LogMsgImpl()` holds `_output_mutex` during dispatch. |
| `Editor/Src/EditorApp.cpp` | `LogMgr::Init()` runs in `EditorApp::Initialize()` before `Application::Initialize()`; `LogMgr::Shutdown()` is last stage of `EditorApp::Finalize()`. Batch/offscreen modes exist. |
| `Engine/Inc/UI/Table/TableDataSource.h` | `GetRowCount()`, `GetCellText()`, optional `DrawCell()`, `Sort()` hooks exist. |
| `Engine/Src/UI/Element/UITableElement.cpp` | `DrawVisibleRows()` is virtualized; `Refresh()`, selection and row sort exist. Scroll calculations use `_current_offset` and fixed row height. |
| `Engine/Inc/UI/Basic.h`, `Engine/Src/UI/Basic.cpp` | `UI::InputBlock` has `_on_content_changed` / `SetContent()`; Enter is handled internally (`CommitEdit`, clear focus), **no dedicated submit event**. `CommitEdit()` currently calls `LOG_INFO` on edits; mitigate flood when used for search/terminal. |
| `Editor/Inc/Dock/DockWindow.h` | `DockWindow : Object`, `GetDockPersistenceId()`, `SaveDockLayoutState()`, `LoadDockLayoutState()`, `OnDockLayoutLoaded()`, `RequestClose()` available. |
| `Editor/Src/Dock/DockManager.cpp` | Loads/saves `Editor/dock_layout.json`, creates dock leaves through reflection; `AddDock()`, `HasDock()`, `ActivateDock()` exist. |
| `Editor/Src/Widgets/EditorLayer.cpp` | `DockManager::Init()` before `BuildEditorChrome()`, `DockManager::Shutdown()` in detach; toolbar `Menu` built in `BuildEditorChrome()`; Build/Package Player launch in `OnImguiRender()` using `ProcessFactory`. |
| `Engine/Inc/Platform/Process.h`, `Engine/Src/Platform/WinProcess.cpp` | Existing `ProcessStartInfo` has command line, working directory, console flags, `InheritHandles`, `UseJobObject`; `WinProcess` supports start/wait/exit/terminate but **not redirected standard streams**. |
| `Editor/Src/Widgets/FrameDebuggerWindow.cpp` | Native Dock UI + `UITableElement` + custom data source precedent; use as UI composition reference. |

**Source snapshot limitation:** archive contains engine/editor source and generated headers, but does not include the entire build/test environment or guarantee a Windows executable is runnable here. Build/test commands must be discovered in the actual repo. All baseline statements above are static source inspection, not runtime measurements.

## Target Outcome

- Editor has reopenable Output/Terminal Dock windows, with saved/restored layout and filters.
- All logs emitted **after early appender registration** can be browsed without window visibility; high-volume logging does not cause unlimited memory/UI objects.
- Internal commands are typed, discoverable, validated and safely dispatched, with no terminal window required to use the registry.
- External one-shot jobs show live stdout/stderr, final exit code and cancellation state; no UI hang or orphaned redirected handles.
- Build/Package Player can expose its external task status through the new service without regressions to original packaging behavior.

## Relevant Files

Existing: paths in Baseline above plus `Editor/Inc/Widgets/{EditorLayer,CommonView}.h`, `Editor/Src/Widgets/CommonView.cpp`, `Editor/Inc/Dock/DockManager.h`, `Engine/Inc/Platform/WinProcess.h`, `Engine/Src/Platform/Process.cpp`, `Editor/Src/EditorMain.cpp`, `Editor/Inc/EditorApp.h`.

Proposed (create only where justified):

```text
Editor/Inc/Common/EditorLogService.h
Editor/Src/Common/EditorLogService.cpp
Editor/Inc/Widgets/OutputWindow.h
Editor/Src/Widgets/OutputWindow.cpp
Editor/Inc/Common/ConsoleCommandRegistry.h
Editor/Src/Common/ConsoleCommandRegistry.cpp
Editor/Inc/Widgets/TerminalWindow.h
Editor/Src/Widgets/TerminalWindow.cpp
Editor/Inc/Common/EditorTaskService.h
Editor/Src/Common/EditorTaskService.cpp
```

Small API changes as needed to: `Engine/Inc/Framework/Common/Log.h`, `Engine/Src/Framework/Common/Log.cpp`, `Engine/Inc/Platform/Process.h`, `Engine/Inc/Platform/WinProcess.h`, `Engine/Src/Platform/WinProcess.cpp`, `Engine/Inc/UI/Basic.h`, `Engine/Src/UI/Basic.cpp`, `Editor/Inc/EditorApp.h`, `Editor/Src/EditorApp.cpp`, `Editor/Inc/Widgets/EditorLayer.h`, `Editor/Src/Widgets/EditorLayer.cpp`. Do **not** create a generic `TerminalEmulator` abstraction in this plan.

## Milestones

| ID | File | Entry prerequisite | Deliverable & independent acceptance |
|---|---|---|---|
| M1 | `m1-structured-log-service.md` | Baseline | Service collects thread-safe bounded structured events without any UI. Builds + unit/stress validation. |
| M2 | `m2-output-window.md` | M1 **PASS** | Output Dock with virtual list, filtering, detail and restore. UI and layout tests pass. |
| M3 | `m3-engine-console.md` | M2 **PASS** | Registry/parser plus Terminal Dock Engine tab, deterministic commands and tests. |
| M4 | `m4-process-pipes.md` | M3 **PASS** | Opt-in Windows pipe process API with standalone pipe/exit/cancel tests, old paths preserved. |
| M5 | `m5-external-task-integration.md` | M4 **PASS** | Task service, external command UI, Build/Package hookup, shutdown & regression tests. |

**Gate rule:** do not begin the next milestone until the current one compiles and meets all mandatory acceptance tests. For flaky or environment-dependent checks, record `BLOCKED` rather than falsely marking `PASS`.

## Current Milestone

`M1` — Structured Log Service; `NOT STARTED`. Authoritative working status: `progress.md`.

## Progress

No files modified and no validation executed by this plan author. Checkboxes in each milestone describe future tasks only. Update `progress.md` after each meaningful action.

## Decision Log

- D1: split 5 milestones so the UI and OS process branches stay independently reviewable.
- D2: **two separate data channels**: structured log service versus terminal task stream, with optional bridging as explicit diagnostics only.
- D3: native Dock/UITable rather than ImGui/text-per-row.
- D4: engine-console first, pipe-backed one-shot tasks later; no mandatory ConPTY.
- D5: bounded pending + history, and service-level lifetime separate from visible windows.
- D6: batch/headless mode must never require native UI startup just to gather logs or run registered commands.

## Surprises & Discoveries

- `LogMsgModule(...)` filters by module but doesn't store the module in `LogMessage`.
- `UI::InputBlock` emits edit logs itself; using it naively for every query keystroke causes noisy self-generated Output traffic.
- Existing `FrameDebuggerWindow` demonstrates data-source-backed table composition without per-row UI objects.
- `GetAppenders()` currently exposes raw pointers; no `RemoveAppender()` is present. Lifetime management needs explicit design.
- `DockManager` tries reconstructing docks by reflected type ID; a plain `DockWindow` subclass without proper reflection may work only until restart.

## Validation

Use the project's **actual** Windows build/test entry points. Record exact commands, target, configuration, host, log paths and results in `progress.md`; never insert placeholder commands into a shell. Mandatory global smoke tests: standard Editor start/exit; Editor restart with dock saved; batch/offscreen startup; high-volume log capture; async command/process cancellation; no UI render thread errors; no regression in existing build/package workflow. Additional cases per milestone.

## Global agent rules

1. **Read** `README.md` + `progress.md`; load **one** `mX-*.md` corresponding to `Current Milestone`.
2. **Inspect before edit:** use live repository to confirm source still matches Baseline. If not, document discrepancy and adapt minimally; do not blindly patch old line numbers.
3. **Implement only one milestone**; keep scoped diff small; use existing utilities/typing/registration conventions.
4. **Compile and test** before marking success; do not treat code review or syntax-only reasoning as passing tests.
5. **Track exact results** in `progress.md` after every attempt, including failure, surprising behavior and the specific next action.
6. **Never silently add scope**. Need a prerequisite API fix? Make smallest compatible change and explain it in Decision Log.
7. **Leave unrelated current user changes intact**. Do not clean/revert/format unrelated files.
8. For unavailable runtime environments, mark `BLOCKED`, list the minimal external verification, stop at gate.

## Known Follow-ups

After all M1–M5 gates: optional ConPTY on Win10 1809+, richer diagnostic hyperlinks / HLSL error location, editor localization, export sessions, generalized task presets, keyboard shortcuts and potential remote **read-only** diagnostic stream. These are **not** part of current milestones.

## Outcomes

Pending implementation. On completion, record concrete commits/changed files/test evidence for each Mx in `progress.md`, and a final concise comparison with Target Outcome.

## Resume Protocol

When returning after context compaction or handoff:

1. Read `README.md` and `progress.md` (not all milestone files).
2. Read the current milestone file only. Check live `git status` and diff; do not assume the previous agent committed.
3. Continue precisely from `progress.md / Next Step` and the milestone's unchecked items.
4. Re-run the milestone gate after any change; mark `PASS` only with evidence, update `Current Milestone` to next one.
5. At M5 pass, fill `Outcomes`, report all remaining Follow-ups and stop.
