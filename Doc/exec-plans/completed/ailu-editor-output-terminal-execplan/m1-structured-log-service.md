# M1 — Structured Log Service (no UI)

> **Load order:** `README.md` → `progress.md` → this file. Prerequisite: none. **STOP** after this milestone's acceptance gates; do not implement OutputWindow in M1.

## Purpose

Make engine/editor logging consumable as structured, bounded, thread-safe data, starting early enough to see engine startup errors. Define ownership precisely so the existing `LogMgr` synchronization and shutdown paths remain valid.

## Scope

- Enrich `LogMessage` with actual timestamp/thread ID/module while preserving all existing logging macros and constructors.
- Add editor-owned `EditorLogService` + tiny `IAppender` adapter; short producer path; bounded queue and history; main-thread batch drain; stable IDs and retrieval snapshot.
- Install the sink after `LogMgr::Init()` and safely stop it before `LogMgr::Shutdown()`; non-GUI modes must work or opt out deliberately.
- Create headless/unit/stress validation hooks compatible with actual test framework.

## Non-goals

No `OutputWindow`, no table/UI creation, no console command registration, no new log file format, no blanket logging-callsite replacements, no altering runtime default trace level. No arbitrary new logging thread unless measured and justified.

## Architecture Constraints

- `LogMgr::LogMsgImpl()` acquires `_output_mutex`, snapshots appenders under `_appender_mutex`, and invokes their `Print()`. **Never log, dispatch UI, wait, or call back into `LogMgr` from `Print()`**.
- Existing `LogMgr::Finalize()` deletes `IAppender*` that were registered. Do not `delete` adapter from Editor code after registration. Do not leave adapter with dangling editor pointer.
- Cross-thread payload: copy `LogMessage` into a pending ring/queue while holding minimal service mutex; one bounded admission check. Avoid blocking on UI state or lock inversion.
- Timestamp is captured **once** at event creation. Reader-visible sequence ID is monotonically increasing (`u64`) to survive ring index reuse and filter/collapse changes.
- `LogMessage` `_file` may be relative/absolute and may not exist; keep raw path and location, never attempt expensive file I/O during `Print()`.
- Keep config constants named `kCamelCase`; member names `_snake_case`. Prefer repository `String`, `WString`, `Vector`, `Scope` / `Ref` as appropriate.

## Baseline

1. `Engine/Inc/Framework/Common/Log.h`: `LogMessage(ELogLevel, std::string|std::wstring, file, line, function)`. Fields: `_level`, `_message`, `_file`, `_line`, `_function`, `_thread_id`, `_time_str`, `_thread_name`; module absent. `ELogModule` includes General/Asset/Shader/RHI/Editor. `LogMsgModule` receives module but creates `LogMessage` **without it**.
2. `Engine/Src/Framework/Common/Log.cpp`: constructors capture `_timestamp`, `_time_str` and `_thread_name`; missing `_thread_id`; `ToDetailString()` calls current time instead of recorded one. `Initialize()` installs an `OutputAppender` only; `FileAppender` class exists but isn't added here.
3. `Editor/Src/EditorApp.cpp`: `EditorApp::Initialize()` calls `LogMgr::Init()` near start, then `Application::Initialize()`; `EditorApp::Finalize()` calls `Application::Finalize()` and later `LogMgr::Shutdown()`.
4. `Engine/Inc/Framework/Common/Log.h`: `LogMgr::AddAppender(IAppender*)`, no removal method; `GetAppenders()` exposes raw pointers; `LogMsgImpl()` serializes appender printing.

## Target Outcome

After early sink registration, a concurrent logging workload can be queried safely, remains within both count/byte limits and preserves level/module/time/location/thread. Window closure does not affect acquisition. Teardown yields no UAF/deadlock. Existing log output continues.

## Relevant Files

**Modify:** `Engine/Inc/Framework/Common/Log.h`, `Engine/Src/Framework/Common/Log.cpp`, `Editor/Src/EditorApp.cpp`, optionally `Editor/Inc/EditorApp.h` for lifetime field.

**Create:** `Editor/Inc/Common/EditorLogService.h`, `Editor/Src/Common/EditorLogService.cpp` (add to actual build target). Tests in project's established test tree; locate real test path first.

## Implementation design (authoritative)

### Data model

Use an immutable record copied out of `LogMessage`, not a pointer to a stack-local `LogMessage`:

```cpp
struct EditorLogRecord
{
    u64 _sequence = 0;
    ELogLevel _level = ELogLevel::kInfo;
    ELogModule _module = ELogModule::kGeneral;
    std::wstring _message;
    std::string _file;
    i32 _line = 0;
    std::string _function;
    std::thread::id _thread_id;
    std::string _thread_name;
    std::chrono::system_clock::time_point _timestamp;
};
```

`ELogLevel` is unscoped in current source; use the exact enum syntax supported by the compiler. Store canonical timestamp from message (provide const accessor or friend, do not expose mutable timestamp unnecessarily). Generate a readable time string only for visible rows. Give ordinary logs `kGeneral` by default; when `LogMsgModule` constructs a message pass the actual module. Preserve constructor overloads via a trailing default argument; update format-error fallback paths too.

### Queue and snapshots

A minimal interface candidate (adapt to repo container types, not required verbatim):

```cpp
class EditorLogService
{
public:
    bool Initialize();
    void BeginShutdown();
    void Finalize();
    u32 DrainPending(u32 max_records, std::chrono::microseconds time_budget);
    u64 Revision() const;
    void GetSnapshot(Vector<EditorLogRecord> &out_records) const;
    u64 DroppedPendingCount() const;
    u64 EvictedHistoryCount() const;
};
```

Internal state: `pending` queue (capped 2,048 records / 4 MiB), `history` deque/ring (10,000 records / 16 MiB), mutex(es), monotonic sequence counter and counters for drops/evictions. Include estimated payload size of strings, not just `sizeof(EditorLogRecord)`. Define oversized single-record policy (truncate with marker and count, rather than allowing unbounded allocation). A queue overrun drops the oldest pending record or newest incoming record consistently; record which policy was selected. Do not hold the producer mutex while formatting UI strings.

**Lifetime:** the safest small change is an `EditorLogAppender` holding `std::shared_ptr<SharedState>`, while `EditorLogService` also holds the shared state. Since `LogMgr` owns the *adapter* and deletes it at `Finalize()`, `BeginShutdown()` marks the state inactive and drops editor service's owner only after drain or teardown. Callback checks atomic active state and state mutex. The appender never owns/uses a raw `EditorLogService*`. Avoid a global raw pointer whose destruction precedes `LogMgr::Finalize()`.

**Avoid dangerous API change:** do not switch all appenders to `unique_ptr` during M1 just to add `RemoveAppender`. If the actual repo already has a safe remove operation, it may be used after validation; otherwise use shared-state invalidation.

## Implementation steps (run in order)

- [x] **Step 1 — baseline:** inspect current working tree and real build/test tooling. Record checkout revision and existing changes in `progress.md`. Confirm paths and actual namespace/configuration before coding.
- [x] **Step 2 — accurate message fields:** add `_module` (default `kGeneral`), initialize `_thread_id = std::this_thread::get_id()` at construction, provide access to original `_timestamp`, ensure `ToDetailString()` prints recorded `_time_str` or `_timestamp` rather than current clock time. Preserve `LogMessage` existing call sites and `ELogLevel` filtering.
- [x] **Step 3 — module propagation:** update both `LogMsgModule` templated overloads and their exception branches to pass `module`; non-module `LogMsg` remains `kGeneral`. Do not change global `kLogLevel` to force verbose output.
- [x] **Step 4 — service state:** implement bounded queue/history and immutable record, counter/revision APIs. Document dropping policy; put constraints in constants. Use a sequence that continues across Clear/eviction. `ClearHistory()` if implemented must not clear pending unexpectedly; explicitly define behavior.
- [x] **Step 5 — sink:** implement `IAppender::Print()` with fixed short path (copy → bound check → enqueue). No `LOG_*`, no formatting into GUI rows, no external file IO, no callback into `EditorLogService` after invalidation. Test parallel logging.
- [x] **Step 6 — lifecycle:** create/register `EditorLogService` immediately after `LogMgr::Init()` in `EditorApp::Initialize()`; do not wait until `EditorLayer::OnAttach()`/`DockManager::Init()`. Call `BeginShutdown()` at chosen lifecycle boundary then safely release after `Application::Finalize()` and before or after `LogMgr::Shutdown()` as appropriate for shared-state ownership. Explain exactly which final logs are intentionally retained/dropped. Ensure init-failure paths also unwind cleanly.
- [x] **Step 7 — batch/headless:** permit collection with no Dock/UI. Prefer service independent of `UIManager`. If runtime doesn't need it in batch, decide and record behavior explicitly; do not crash on batch/offscreen.
- [x] **Step 8 — tests and gate:** run actual compilation and tests, record measured allocation count/elapsed time under stress, fix regressions before M2. Commit scoped changes if workflow uses commits.

## Important edge cases / invariants

1. Late producer after `BeginShutdown`: should fail closed (drop) without touching freed service; no debug assert accessing `LogMgr::Get()` after manager shutdown.
2. `ToDetailString()` is called long after event creation: time remains invariant.
3. Module diagnostic `LOG_MODULE_TRACE`/`LOG_MODULE_DEBUG` path records actual module even though global `kLogLevel` may suppress other debug logs.
4. Fatal level counted as error severity for eventual UI counters, but `_level` retains `kFatal`.
5. Zero-cap or unusually long message must not divide by zero, underflow bytes or bypass caps.
6. Pending admission may invoke allocations; catch/handle allocation failure under project policy without recursive diagnostics.
7. Concurrent `DrainPending()` and `Print()` never violate ordering or produce duplicates.
8. Don't assume `FileAppender` is globally registered; preserve its existing behavior and optional use.

## Validation

Perform all checks in the actual repository. The source ZIP does not provide a known test command; use existing test runner and record exact invocation/result.

| Test ID | Procedure | Expected result |
|---|---|---|
| M1-T01 | Compile Engine and Editor, run existing smoke tests | No new compile/link/regeneration errors. |
| M1-T02 | Construct ordinary and module-specific messages in tests | General vs actual module, timestamp, thread ID, file/line/function correct. |
| M1-T03 | Construct a record, delay, call `ToDetailString()` | Original event timestamp, not display time. |
| M1-T04 | Start 8 producer threads, 10k messages each, consume concurrently | No corruption, deadlock, duplication or data races. Sequence strictly increasing for retained events. |
| M1-T05 | Emit >history and >pending caps plus long strings | Both limits respected; drops and history evictions counted. |
| M1-T06 | Start Editor normally, emit before EditorLayer attach | Startup events after sink attachment visible in service snapshot. |
| M1-T07 | Close/reopen GUI (or do not open GUI), continue logs | Acquisition stays active independent of windows. |
| M1-T08 | Start/stop Editor repeatedly and fail initialization intentionally | No UAF, leaked appender, double delete, deadlock or shutdown crash. |
| M1-T09 | Run batch/offscreen with logging | Mode follows documented behavior without GUI initialization dependency. |

Use debugger/ASan if supported; otherwise instrument counts and handle ownership. Save test traces/observations as evidence. Do not substitute unexecuted pseudocode for tests.

## Acceptance Criteria (ALL required)

- [x] Actual Engine/Editor build passes; baseline logging still works.
- [x] `LogMessage` records original timestamp, creator thread ID and module correctly.
- [x] `EditorLogService` has bounded pending and history, stable sequence IDs, drop/eviction telemetry.
- [x] Cross-thread tests pass; sink never recursively logs or manipulates UI.
- [x] Early startup, failure unwind, batch/headless and teardown paths verified.
- [x] `progress.md` updated: changed files, commands, measured results, limitations, decision log and M1=PASS.

## Milestones

This is **M1 of M5**. **No M2 changes allowed** until acceptance passes; then set `Current Milestone` to `M2` and `Next Step` to M2 Step 1 in `progress.md`.

## Current Milestone

M1, `NOT STARTED` at plan creation. Live state in `progress.md` overrides this initial statement.

## Progress

No implementation or tests performed by plan author. Tick steps and acceptance only after executing them.

## Decision Log

Proposed: shared-state adapter lifecycle; bounded admission and retained log buffer; structured module propagation without changing macros. Record deviations in `progress.md`.

## Surprises & Discoveries

The logger serializes *all* appender printing with `_output_mutex`; a slow appender stalls callers. `LogMessage::ToDetailString()` currently changes apparent time on every read. These are priority correctness/performance constraints.

## Known Follow-ups

Deduplicated/collapsed presentation, search UI and source navigation belong to M2. Structured diagnostic import from external tools belongs to future scope after M5.

## Outcomes

Pending. Summarize final M1 code diff, tests, cap measurements and any unresolved risks in `progress.md`.

## Resume Protocol

Read `README.md` + `progress.md` + **this file**; compare live diff to progress; continue from first unchecked Step/failed test, run all acceptance tests, then advance gate exactly once.
