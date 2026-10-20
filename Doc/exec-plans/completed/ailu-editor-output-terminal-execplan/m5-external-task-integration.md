# M5 — External Task Runner, Terminal Integration and Final Regression

> **Prerequisite:** `M4 PASS` in `progress.md`. Last milestone. The end state is a **one-shot task terminal**, not a full shell emulator. Read README + progress + this file; stop and report final outcomes after M5 PASS.

## Purpose

Bridge the M4 pipe process backend into Editor's native Terminal and the existing Build/Package Player flow, so users can launch local CMake/Git/Python/cmd/PowerShell *one-shot* commands and inspect asynchronous stdout/stderr, exit status and cancellation. No GUI freezes, logging loops or detached orphan tasks.

## Scope

- Add `EditorTaskService` that owns task process sessions, channel-aware output, bounded transcripts, status/exit, cancellation and progress.
- Add Terminal task mode UI to M3 `TerminalWindow`: active task selector/list, command input, working directory, Run/Cancel, stream text and exit status.
- Start local external commands safely using explicit **direct executable** or explicitly chosen **shell wrapper** (`cmd.exe`/`powershell.exe`); shell mode must be visibly labeled, not implicit in Engine commands.
- Adapt current `Build/Package Player` button to use the task service (or add a bridge without changing original functionality) and show output/status in Terminal.
- Harden shutdown, large output handling, Unicode decoding, task-history limits, error reporting and full regression.

## Non-goals

No interactive stdin, no persistent Shell with retained `cd`/environment, no full ANSI/VT terminal emulator, no ConPTY requirement, no remote command endpoint, no automatic editor code modifications based on compiler text, no generic background-work scheduler replacing existing engine JobSystem.

## Architecture Constraints

1. **Never run external commands from `ConsoleCommandRegistry::Execute` by default.** Separate Engine commands (M3) and external Task requests (M5); only direct user action in local Editor UI or explicitly authorized trusted local build routine may spawn OS commands.
2. A task is owned by `EditorTaskService`, *not* by `TerminalWindow` or static local variables in `OnImguiRender()`. Closing Terminal must not silently kill a running build; Editor shutdown should cancel/stop owned tasks using documented bounded policy.
3. Process backend emits raw byte chunks. `EditorTaskService` incrementally decodes bytes to text with persistent UTF-8 carry state per stream; if using Windows console OEM/ACP fallback, make codec policy explicit and test. CR, LF, CRLF, partial chunk boundaries, ANSI escape sequences and embedded NUL must not crash/expand memory.
4. Output display is bounded and virtualized (recommend 5,000–10,000 lines or 8–16 MiB/task). Reader continues draining pipes when UI hidden or output discarded; show truncation counter/status.
5. Process finish is **not** task finish until output streams reach EOF (or forced shutdown with explicit truncation). Keep exit code and reason: natural / canceled / start failure / forced timeout.
6. Preserve original `BuildPackagePlayerCommandLine()` and its working directory/condition checks. Don't duplicate command construction in an unrelated place; move to a small helper only if needed and behavior-equivalent.
7. Do not `LOG_INFO` every stdout line: the structured Output log channel is not a mirror of every Terminal line. Emit a **single** start/end/error diagnostic if appropriate, but make it impossible for appender to recursively invoke itself.
8. Windows process tree: report if Job Object unavailable or assignment failed; don't overclaim cancellation of grandchildren. Child handles and draining workers must be fully cleaned on Editor termination.
9. A one-shot cmd/PowerShell invocation executes one string then exits; show working-directory / environment policy and do not advertise a persistent interactive shell session.

## Baseline

- `Engine/Inc/Platform/Process.h`, `Engine/Src/Platform/WinProcess.cpp`: M4 supplies opt-in redirected streams and lifecycle. Confirm actual M4 API from `progress.md` rather than assuming the suggested signature.
- `Editor/Src/Widgets/EditorLayer.cpp`: `BuildPackagePlayerCommandLine()` in anonymous namespace near beginning; `static Scope<Process> s_package_player_process` around packaging ImGui panel, launch via `ProcessFactory::Create()` + `Start(psi)` and poll completion/exit code. Original UI has `Build/Package Player` button and status string; maintain visible behavior.
- `Editor/Src/EditorApp.cpp`: initializes subsystems, pushes EditorLayer, finalizes Application before `LogMgr::Shutdown()`. Add task-service ownership before UI and shut down before dependent services/LogMgr become invalid.
- M3 owns `TerminalWindow`, registry and bounded transcript. M5 adds a Tasks mode; it must not regress Engine command entry.

## Target Outcome

From local Editor a user can execute a one-shot external task, see correctly decoded stdout/stderr without blocking, cancel a long-running task, inspect exit code, reopen Terminal and still see recent task status. Build/Package Player keeps working and its diagnostics are visible. Shutdown leaves no owned process/reader leaks.

## Relevant Files

**Create:** `Editor/Inc/Common/EditorTaskService.h`, `Editor/Src/Common/EditorTaskService.cpp`; optional tiny text decoder helper in same module if appropriate.

**Modify:** `Editor/Inc/Widgets/TerminalWindow.h`, `Editor/Src/Widgets/TerminalWindow.cpp`, `Editor/Inc/EditorApp.h`, `Editor/Src/EditorApp.cpp`, `Editor/Src/Widgets/EditorLayer.cpp` and perhaps its header for small integration. M4 Process API may get bug fixes discovered in integration (document as M5 patch).

**Reference:** current `BuildPackagePlayerCommandLine()` implementation, `ProcessStartInfo`, `WinProcess`, existing build status/immediate mode conditions, `EditorProgressService` (`Editor/Src/EditorApp.cpp`, `Editor/Src/Widgets/EditorLayer.cpp`) if task progress integration fits without major scope expansion.

## Task and output data model

A recommended model (adapt types and threading to actual code):

```cpp
enum class EEditorTaskState : u8
{
    kQueued,
    kStarting,
    kRunning,
    kExitedDraining,
    kSucceeded,
    kFailed,
    kCanceled,
    kLaunchFailed
};

struct EditorTaskRequest
{
    WString _executable;
    Vector<WString> _arguments;
    WString _working_directory;
    bool _capture_stdout = true;
    bool _capture_stderr = true;
};

struct EditorTaskStatus
{
    u64 _task_id = 0u;
    EEditorTaskState _state = EEditorTaskState::kQueued;
    i32 _exit_code = 0;
    bool _has_exit_code = false;
    u64 _dropped_output_bytes = 0u;
};
```

Service API candidates: `StartTask(request) -> task_id/error`, `CancelTask(task_id)`, `Tick(max_chunks, budget)`, `GetStatus(task_id)`, `GetTranscriptSnapshot(task_id, ...)`, `BeginShutdown()`, `Finalize()`. Keep task IDs stable and monotonically increasing. Start with **one running task per Terminal session**; if more are requested, either reject with clear message or queue a bounded number, not silent parallel floods. Existing Build/Package task may run concurrently with one user task if separate named sessions are carefully handled; otherwise explicitly enforce global max (2) and document UX.

### Command construction and security

Prefer explicit executable + argv construction with one tested Windows quoting helper. Do not build `CommandLine` via naive string concatenation without quoting `"`, whitespace, backslashes; remember `CreateProcessW` command line escaping differs from `cmd.exe` and PowerShell parsing. UI offers explicit mode selector:

- **Direct task (preferred):** executable path + arguments passed as separate logical fields; service forms quoted command line according to Windows rules and gives `CreateProcessW` the appropriate executable when supported.
- **cmd one-shot:** user explicitly chooses `cmd.exe /d /s /c <command>`. Here characters such as `|`, `&`, `>` have shell meaning because the user selected shell execution. Quote wrapper properly, display mode prominently.
- **PowerShell one-shot:** `powershell.exe -NoProfile -NonInteractive -Command <command>` with correct argument quoting. PowerShell version/encoding differ; test actual installed shell and do not claim persistent state. If `pwsh.exe` unavailable, report this rather than silently using another binary.

All modes: set working directory from user input validated against an existing directory; choose default current project root if safely resolvable, else Editor working directory and show it. Never hardcode `F:\AiluEngine` from examples. History may include tokens/secrets: **do not persist arbitrary raw command history or stdout to `dock_layout.json`**. In-memory history cleared at Editor exit unless explicit opt-in added later. Never execute automatically on opening/restoring a tab.

### Decoder / transcript pipeline

```text
WinProcess child stdout/stderr read threads
      → bounded ProcessOutputChunk(raw bytes, channel, seq)
      → EditorTaskService Tick (main thread or dedicated bounded worker)
      → per-channel incremental decoder + CR/LF line assembler
      → bounded TaskTranscript (channel/time/task id/text; dropped counters)
      → TerminalWindow virtualized data source (UI thread only)
```

Incremental decode requirements:

- Maintain pending UTF-8 prefix bytes separately for stdout and stderr. Never decode a chunk in isolation and corrupt split codepoints.
- Handle Windows legacy codepage fallback deliberately: choose per-task explicit option or robust documented auto policy; don't silently strip invalid bytes.
- Treat `\r\n` as one newline; bare `\r` from progress tools should update the current *task line* or emit bounded progress events, not create thousands of rows unnecessarily.
- Preserve stdout/stderr channel and ordering **per stream**; interleaving between streams is approximate due to separate pipes.
- Bound line length (e.g. 16 KiB) and replace middle/append truncation marker instead of unbounded allocation; retain full raw output only if optional file-spooling is explicitly implemented.
- Basic ANSI SGR can be stripped for M5 (safe and bounded) and later colored; do not implement escape-driven cursor movement on general UI widgets.
- On stream EOF flush an incomplete final line exactly once, then task state transitions based on process exit / cancellation / decoder completion.

### Task lifecycle state transitions

```text
Queued → Starting → Running → ExitedDraining → Succeeded / Failed
                      ↘ Canceling → Canceled (after explicit stop/drain cutoff)
           ↘ LaunchFailed
```

Add `kCanceling` if needed; do not mark Canceled until termination was actually requested and final state observed (or report timeout). `CancelTask()` idempotent. Callbacks must not capture destroyed `TerminalWindow`. Closing UI doesn't stop task; Editor shutdown prevents new task start and drains/cancels owned tasks with bounded wait and explicit lost-output accounting.

## Terminal UI changes

- Add **Engine** / **Tasks** tabs to existing `TerminalWindow`; default Engine unless a running build is brought into focus.
- Tasks mode: task selector (e.g. `Build/Package Player`, `User Task #7`), command/executable input, `Working Directory`, execution mode, `[Run] [Cancel] [Clear View]`, virtualized transcript and status footer (`Running`, `Exit 0`, elapsed, dropped bytes).
- `Run` creates task then displays its task ID. `Cancel` only enabled while cancellable. Clear View removes displayed transcript according to policy but **must not terminate the task or discard process pipe handles**.
- Show stderr visually distinct without assuming everything printed to stderr is fatal. `exit_code != 0` → task failed by default; task errors can be promoted to structured diagnostics as an explicit later action, not automatic regex magic in M5.
- UI polling is budgeted and only redraws when revision changed. Keep task service running and draining when Tasks tab hidden.
- Optional `Show in Terminal` action in existing Build/Package panel selects its task. When pressing Build, don't unexpectedly pop up Terminal if user previously closed it unless user choice or documented design calls for that.

## Existing Build/Package Player integration

Locate the exact existing anonymous namespace `BuildPackagePlayerCommandLine()` and the surrounding `OnImguiRender()` button logic. Implement an adapter using **the same command line and `WorkingDirectory` choices**. Preserve:

- Existing preconditions and disabled state while packaging.
- Existing success criterion (completed process exit code 0) and failure behavior, now augmented with captured text.
- Existing build/package artifacts and invocation arguments; no new `cmake --build` command to replace a scripted packager.
- Existing UI status messages; source of truth becomes `EditorTaskService::GetStatus(task_id)` rather than a static `Scope<Process>`. Avoid double ownership and double start.
- Headless/batch behavior: if packaging is invoked through another code path, don't force UI creation. Provide service call that works independently of Terminal window.

If current function's visibility prevents sharing, move the command builder to a small `EditorBuildTask` helper or keep button as the sole call-site and pass the generated request to task service. Do not duplicate or silently change quoting semantics; add a parity test that compares old vs new effective launch command and working directory.

## Implementation steps

- [ ] **Step 1 — verify:** require M4 PASS, inspect exact completed Process API and existing Terminal/Build button. Identify live build commands, UI threading and Editor shutdown order; record drift.
- [ ] **Step 2 — core task model:** implement `EditorTaskService` task IDs/state machine, ownership, bounded task list/concurrency and output transcripts; add tests without UI.
- [ ] **Step 3 — decode:** per-channel incremental UTF-8/legacy encoding policy, CRLF/bare CR, large line policy, stream EOF flush. Use a small unit corpus with deliberate 1-byte chunk splits.
- [ ] **Step 4 — command launch:** implement direct executable argument quoting and explicit cmd/PowerShell one-shot wrappers. Verify paths with spaces, backslashes and Unicode. Reject blank command, invalid executable/working-directory, unauthorized external execution from engine console/remote automation.
- [ ] **Step 5 — UI:** add Tasks tab, selector, working-directory display, Run/Cancel/Clear, channel-colored status, virtualized transcript; preserve existing Engine tab behavior and saved Dock layout.
- [ ] **Step 6 — package bridge:** route existing Build/Package button through task service, keep effective command/working directory/disable behavior. Expose task status in both old panel and new Terminal; remove static process ownership only after parity checks pass.
- [ ] **Step 7 — lifecycle:** implement shutdown sequence: disallow starts → cancel/terminate running sessions according to policy → bound waits for reader completion → release pipes/job/process → clear callbacks → destroy service. Place before `LogMgr::Shutdown` and after last possible producer use, without making `EditorLayer::OnDetach` block indefinitely.
- [ ] **Step 8 — performance/regression:** run long stdout/stderr bursts with Terminal open/closed, cancel jobs, restart, run build/package on representative project, batch mode. Validate UI frame-time, process/handle counts and memory caps.
- [ ] **Step 9 — final:** document complete command modes, known limitations and actual platform coverage. Update `progress.md` M5 PASS and top-level Outcomes. Do not start out-of-scope ConPTY work.

## Failure behavior matrix

| Case | Required UX / state |
|---|---|
| Command missing | LaunchFailed, visible error + OS code, no dangling session. |
| Nonzero exit | Failed, show numeric exit code; stderr visible but not automatically fatal by itself. |
| Exit 0 but last output delayed | `ExitedDraining` until EOF, then Succeeded; last lines preserved. |
| User cancels build | Show Canceling/Canceled; resolve child/job state; original button re-enabled only after state terminal. |
| Terminal closed during work | Task continues and drains into bounded buffer; reopens to current status. |
| Editor closes during work | Bounded cancel/drain/cleanup; no indefinite wait or lost handles. |
| 20 MiB output and window hidden | Service memory capped, drop count visibly available; process never pipe-blocked forever. |
| Child emits `\r` progress line repeatedly | Current display line updates or bounded event count, no huge row multiplication. |
| Space/Unicode paths | Direct launch correctly quoted and output decoded per selected policy. |
| PowerShell interactive request (`Read-Host`) | One-shot noninteractive mode fails/returns clearly; no UI deadlock. |
| User includes `&`/`|` in Engine command | Not forwarded to OS; parser returns error or ordinary arg, per M3 contract. |

## Validation

| Test ID | Procedure | Expected |
|---|---|---|
| M5-T01 | Build Engine+Editor, normal startup and shutdown | No compile/regeneration/runtime regression. |
| M5-T02 | Run direct short task (`echo` test child / verified executable) | Stdout/stderr arrive, exit=0 and Completed. |
| M5-T03 | Run cmd and PowerShell one-shot commands explicitly | Wrapper launched correctly, quoting and working directory correct. |
| M5-T04 | Paths with spaces/Unicode and args with quotes/backslashes | Exact child argv observed via test harness. |
| M5-T05 | UTF-8 split across chunks, CRLF, bare CR, invalid bytes, long line | Deterministic conversion, bounded rows, no crash. |
| M5-T06 | 20 MiB stdout/stderr while UI hidden; then reopen | Child drains; queue/transcript bounded; overflow surfaced. |
| M5-T07 | Cancel long task and close Editor while task active | No zombie/orphan owned task under supported job policy; bounded shutdown. |
| M5-T08 | Run Build/Package Player from original button | Command and working directory parity; exit state and real artifacts correct. |
| M5-T09 | Failure of packager / invalid working directory | Error visible in both old UI and Terminal, controls reenabled. |
| M5-T10 | Engine tab `help`/`echo` plus Output logging smoke | M1–M3 unaffected; no duplicated flood. |
| M5-T11 | Close/reopen Terminal, save/restart Dock; run batch/offscreen | Sessions behave per stated lifetime; no mandatory GUI. |
| M5-T12 | Repeated 100 process starts and cancels | No growing handle/thread count, stable memory. |
| M5-T13 | Security: remote/automation request attempting OS command | Rejected unless explicitly trusted local caller; no accidental OS execution. |

## Acceptance Criteria (ALL required)

- [ ] Full Engine/Editor build + existing test suite pass; initial requirements of M1–M4 still pass.
- [ ] Local one-shot tasks execute in explicit mode, stream both channels, report exit and completion correctly.
- [ ] Terminal Tasks UI remains responsive, works with closed tab, implements Cancel and bounded output.
- [ ] Build/Package Player command + working directory parity verified, successful artifacts correct, failure reported.
- [ ] Shutdown/cancellation and repeated task launches show no process/pipe/worker leaks.
- [ ] User-facing documentation clarifies **not** an interactive persistent terminal; no OS command injection through Engine Console/remote automation.
- [ ] `progress.md` updated with all 5 PASS gates, real commands/tests, changed files, known limitations and Outcomes.

## Milestones

M5 of M5, terminal milestone. Mark final complete only when all criteria pass. No M6 is implied.

## Current Milestone

M5 pending M4; check `progress.md` live state.

## Progress

No implementation or runtime tests performed during plan authoring; fill in evidence as work proceeds.

## Decision Log

Proposed: process-derived one-shot TaskService; explicit shell mode; UTF-8 incremental decoder per channel; task lifetime independent of Dock; packaging parity first.

## Surprises & Discoveries

Existing Build/Package code tracks its own static `Scope<Process>` in an ImGui panel. A naive Terminal adapter would cause competing ownership; move responsibility to TaskService once in a controlled parity-preserving change.

## Known Follow-ups

Optional interactive ConPTY terminal (Windows 10 1809+ only), full ANSI/VT rendering, VT cursor movement, full multi-shell tabs, persistent task logs, rich compiler error hyperlinks, command-specific task presets, and read-only diagnostics for automation. Not M5 acceptance requirements.

## Outcomes

Pending. Once verified, record final baseline-vs-target evidence and final known limitations in global `progress.md` and notify user with concise delivery summary.

## Resume Protocol

Read README + progress + M5 only; inspect current diff and M4 API, finish checklist/tests, record final PASS and outcomes. Do not implement additional features or return a success claim before actual test evidence exists.
