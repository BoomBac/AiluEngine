# M4 — Windows Process Pipes and Lifecycle (standalone backend)

> **Prerequisite:** `M3 PASS` in `progress.md`. This milestone provides an independent backend that can be tested without Editor UI. M5 owns Terminal integration. Read global docs + this file only.

## Purpose

Extend existing `Process`/`WinProcess` to support **opt-in** capture of child stdout/stderr and predictable completion, cancellation, handle ownership and output draining. Use ordinary Win32 pipes compatible with legacy Windows rather than depending on ConPTY.

## Scope

- Add redirected stdout/stderr flags to `ProcessStartInfo` without changing old defaults.
- Spawn and own child process, pipes, reader workers; asynchronously drain stdout/stderr into bounded thread-safe output event buffer.
- Preserve exit code, stdout/stderr distinction and EOF status; support explicit bounded cancellation/cleanup, job object when available.
- Add a stable polling/drain API usable by M5 task model and independent tests/harness.
- Validate Unicode/CRLF/very long lines, heavy simultaneous streams, startup errors, fast exit, process-tree cancellation and old API compatibility.

## Non-goals

No Terminal widget changes, no interactive stdin, no persistent shell, no pseudo-console, no ANSI screen state/parser, no full environment/PTY abstraction. Don't force existing Package Player or third-party code to adopt the new API in M4.

## Architecture Constraints

1. **Existing defaults remain unchanged**: `CreateNoWindow=true`, `NewConsole=false`, `InheritHandles=false`, `UseJobObject=true`. New redirect flags default `false`.
2. Use `CreatePipe` + `SetHandleInformation` and Win32 `CreateProcessW`, **not** `system()`, `popen()`, `freopen()`, `ConPTY` or polling stdout file offsets.
3. When redirecting, child write handles are inheritable, parent read handles are NOT inheritable; parent closes its copies of all child write ends immediately after `CreateProcessW` (on both success/failure paths). Reject incompatible `NewConsole` + redirection policy or document tested behavior.
4. `STARTF_USESTDHANDLES` requires valid `hStdInput/hStdOutput/hStdError` and `bInheritHandles=TRUE` for redirection. Avoid inadvertent inheritance of unrelated handles (ideally `STARTUPINFOEXW` explicit handle-list on supported Windows; validate minimum OS, no blanket inherit leaked handles). If no whitelist implementation, the M4 gate requires a documented isolated/validated no-leak strategy.
5. Child output may exceed OS pipe capacity. **Always** have concurrent or otherwise deadlock-free drains of stdout and stderr while child runs; do not do `Wait(INFINITE)` before reading both.
6. Never perform blocking wait/read in Editor UI thread. `IsRunning()/TryGetExitCode()` must remain nonblocking.
7. **Child has exited** and **pipe streams reached EOF** are two different states. Only mark stream complete after drain/EOF or explicit cancel with loss reporting. Exit code `0` alone cannot prove complete output.
8. All Win32 `HANDLE` ownership explicit; handles can be moved once, closed exactly once; no worker uses a handle after close. Move-assign only after old session safely stopped. Respect current semantics that `Close()` only closes handles, historically not a terminate action.
9. Process tree termination is best effort if Job Object assignment fails (e.g. nested jobs on Win7). Report fallback and avoid claiming all descendants were terminated.
10. No log echo loop: process readers do not call `LOG_INFO` for every line; they enqueue channel-tagged output records for owner.

## Baseline

- `Engine/Inc/Platform/Process.h`: `ProcessStartInfo { CommandLine, WorkingDirectory, CreateNoWindow, NewConsole, InheritHandles, UseJobObject }`; `Process` virtuals `Start`, `IsValid`, `IsRunning`, `Wait`, `TryGetExitCode`, `Terminate`, `Close`; `ProcessFactory::Create()`.
- `Engine/Inc/Platform/WinProcess.h`: `WinProcess` holds `_process`, `_thread`, `_job` as `void*`, defines destructor, move constructor/assignment; no pipe state.
- `Engine/Src/Platform/WinProcess.cpp`: `CreateProcessW` with `STARTUPINFOW`, environment inherited, create flags, Job assignment; `Close()` closes thread/process/job, does **not** stop child; `Terminate()` sends Job termination if available else direct process termination. `TryGetExitCode()` returns false for STILL_ACTIVE.
- `Engine/Src/Platform/Process.cpp`: Windows factory returns `WinProcess`, other platforms return null.

## Target Outcome

A child can write huge interleaved stdout/stderr and exit immediately; caller can poll events and eventually observe all output plus exit code. Cancellation works without UI blocking or handle leaks. Existing nonredirected process users compile and preserve behavior.

## Relevant Files

**Modify:** `Engine/Inc/Platform/Process.h`, `Engine/Inc/Platform/WinProcess.h`, `Engine/Src/Platform/WinProcess.cpp`, optionally `Engine/Src/Platform/Process.cpp` if factory changes needed.

**Create only if justified:** `Engine/Inc/Platform/ProcessOutput.h` / `Engine/Src/Platform/ProcessOutput.cpp` for shared records/buffer, rather than bloating `WinProcess.cpp`. Prefer minimal abstraction.

**Reference:** `Editor/Src/Widgets/EditorLayer.cpp` packaging process usage to check compatibility, but don't edit the caller yet. Use actual Windows SDK/compiler and regression tests from live repository.

## API recommendation

Extend existing struct by appended members, leaving defaults false. Use fields matching **current repository** naming (do not rename old fields):

```cpp
struct ProcessStartInfo
{
    // Existing fields remain unchanged...
    bool RedirectStdout = false;
    bool RedirectStderr = false;
    // Optional: bool RedirectStdin = false;  // NOT REQUIRED for M4
};

enum class EProcessOutputStream : u8
{
    kStdout,
    kStderr
};

struct ProcessOutputChunk
{
    u64 _sequence = 0u;
    EProcessOutputStream _stream = EProcessOutputStream::kStdout;
    String _bytes; // raw bytes at this layer; UTF decoding can be in task service
};
```

Prefer a base `Process` virtual default implementation or opt-in capability interface for `DrainOutput(Vector<ProcessOutputChunk>&, u32 max_chunks)` and `StreamsDrained()`; do not require all non-Windows process stubs to implement new pure virtual methods unless necessary. Decide whether output is chunks (simpler correctness) or decoded lines (higher coupling). **Recommend raw bytes** here: preserve incomplete multibyte UTF-8 sequences and leave line decoding to M5. `sequence` reflects arrival order of chunks as observed by readers, **not** guaranteed exact interleaving order of stdout and stderr at source.

Data limits: a bounded in-memory queue of raw chunks (suggest 4 MiB or 4096 chunks; whichever first), with drop counter and an explicit overflow status. A high-volume child must never block infinitely solely because the **consumer** isn't polling; a reader drains OS pipe continuously and discards oldest queued chunks on queue pressure (with counted loss). Buffer writes via mutex; drains by swap/batched dequeue. Don't confuse queued output bytes with pipe bytes.

## Win32 spawn algorithm (follow in code)

1. Initialize all handles as `nullptr`/`INVALID_HANDLE_VALUE` per exact API; use local RAII wrappers while constructing. Never call `CloseHandle` on invalid handles.
2. For each redirected stream: call `CreatePipe` with inheritable write end and sufficiently default-sized buffer; call `SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0)`. Both pipes independent if both flags enabled.
3. For unredirected std handles: use `GetStdHandle(STD_*_HANDLE)` as appropriate and validate if `STARTF_USESTDHANDLES` is set. Never close a handle returned by `GetStdHandle` unless explicitly owned. If editor process has no console and any std handle is invalid, decide a safe fallback (e.g. child stdin from `NUL`, unredirected output to `NUL` or leave startup behavior isolated with tested fallback); do not pass random invalid handles.
4. When launching with redirection set `STARTUPINFO[EX]W::dwFlags |= STARTF_USESTDHANDLES`, assign `hStdOutput/hStdError`, configure inherited-handle allowlist (preferred: `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` with `STARTUPINFOEXW` where available) and set inheritance TRUE for those explicit handles. Preserve current nonredirect path untouched.
5. Use `CreateProcessW` with mutable null-terminated command line vector and optional working directory; quote executable and arguments robustly when M5 constructs them. For this milestone use a known test child executable/path; return Win32 error for launch failure without leaking pipes.
6. On success: store process/thread handles and job; **close parent-side copies of child write ends** immediately; launch readers promptly before waiting. On failure: close all local owned handles and job, reset `_pid` and state.
7. Worker for stdout and worker for stderr: loop on `ReadFile`, append chunks, detect EOF (`ERROR_BROKEN_PIPE` etc.), publish stream completion. A read can return fewer bytes than requested; don't assume full line. If one stream is disabled, treat it as already drained.
8. `Wait(timeout_ms)` remains a child process wait, not a stream-drain barrier. Expose separate `StreamsDrained()` or `WaitForOutputDrain(timeout)` for non-UI callers. Ensure M5 can check terminal state without blocking.
9. `Terminate()`: try job termination then process fallback; set cancellation intent, allow readers to drain/EOF, then bounded cleanup. Explicitly record where already-buffered output is preserved/discarded.
10. `Close()` semantics: after child exit naturally close handles and join readers safely; when child still runs, do **not** silently terminate if existing API promised detach. If redirection makes safe detach impossible, add/document a separate `StopCapture()/CancelAndClose()` path for sessions and ensure no join waits forever on live child. Preserve backward compatibility for nonredirect `Close()`.
11. `WinProcess` move: transfer process/job handles **and** pipe/worker ownership exactly once. Simplest approach is moving one heap-allocated shared `ProcessSessionState` containing readers/pipes/buffers; forbid or carefully test move while actively reading. Never leave readers capturing the pre-move `this` pointer.
12. During shutdown, observe or cancel any live reader jobs with bounded wait; if descendants inherit writer handles, EOF can be delayed. Report truncated output if forced closure; no indefinite blocking.

### Windows 7 and Win10 paths

Ordinary anonymous pipes and `CreateProcessW` work on older Windows. `STARTUPINFOEXW` + handle-list process attribute is supported on older Windows versions, but **verify SDK/OS prerequisites for targeted Win7 SP1 build**, do not blindly compile Windows 10-only helpers. Make ConPTY explicitly out of scope. If Win7 machine isn't available, compile against intended minimum APIs and mark runtime as unverified, not PASS for Win7 specifically.

### Test child / harness

Use an existing process test harness if available; otherwise add small standalone Windows test executable within actual test target with options:

```text
--stdout-lines 1000
--stderr-lines 1000
--unicode
--long-line 131072
--exit-code 7
--sleep-ms 30000
--spawn-child
```

Avoid testing by invoking unknown shell utilities only, because output encoding and quoting differ. The child should `fflush`/flush to make partial-output timing observable, emit no accidental binary NUL unless test covers it.

## Implementation steps

- [ ] **Step 1:** confirm M3 PASS and actual compiler/Win7 target, inspect `Process` move/close contract and all callers (`ProcessFactory::Create`, `Start`, `Close`, `Terminate`). Record any source drift.
- [ ] **Step 2:** design a small state diagram (`Idle`, `Starting`, `Running`, `ExitedDraining`, `Completed`, `Canceling`, `Failed`) and output event/EOF counters; document each public function's behavior.
- [ ] **Step 3:** extend `ProcessStartInfo` with opt-in `RedirectStdout/RedirectStderr` defaults; preserve legacy path unchanged. Add polling output API with default safe behavior for unsupported backends.
- [ ] **Step 4:** implement RAII Win32 handle creation, inheritance security, CreateProcess failure cleanup. Test one stream and two streams; assert only authorized handles inherited.
- [ ] **Step 5:** start readers and bounded queue; confirm a child emitting stdout/stderr beyond 64KiB cannot deadlock even if UI consumer is slow or absent.
- [ ] **Step 6:** correct exit-versus-EOF reporting, partial chunk retention and queue drop counters. A test child exits before reader drains: all bytes still received.
- [ ] **Step 7:** implement termination/cancel and lifecycle: running child, exited child, failed start, move constructor/assignment, destructor, job assignment failure. Do not claim child tree killed when assignment fails.
- [ ] **Step 8:** run standalone harness and legacy process smoke tests. Repeat 100 times, monitor process/handle counts. Test Win7 SP1 if available; otherwise record precisely as unverified.
- [ ] **Step 9:** publish stable backend contract for M5 (polling/drain API, completion and cancel semantics). Update `progress.md`; stop until M4 gate passes.

## Safety / failure table

| Scenario | Required behavior |
|---|---|
| Child executable missing | Return failure + Win32 error; no reader threads or leaked handles. |
| Child stdout burst 10 MiB | Reader continues draining OS pipe; bounded queue drops with count; UI remains responsive. |
| stderr fills while stdout quiet | Both readers independent; no deadlock. |
| Parent stops polling output | Memory bounded; reader continues or loses data with explicit counter. |
| Process exits before stdout EOF | Wait returns process exit; data can still drain; report Completed only at EOF. |
| Child spawns descendants | Job tree termination if attached, otherwise explicit limitation and safe cutoff policy. |
| Move active WinProcess | Handle owners and worker state remain valid; no use-after-move. |
| `Close()` on legacy nonredirected child | Preserve original detach semantics (no implicit terminate). |
| `Close()` on active redirected child | Use tested explicit detach/cancel semantics, no blocking join forever. |
| Invalid UTF sequence split across reads | Raw-byte API preserves chunks; M5 incremental decoder handles sequence boundaries. |

## Validation

| Test ID | Procedure | Expected |
|---|---|---|
| M4-T01 | Rebuild Engine/Editor; run existing packaging / process caller | No ABI/source regressions. |
| M4-T02 | Launch child with stdout redirect only, then stderr only | Correct channel and EOF. |
| M4-T03 | Both channels output >OS pipe capacity concurrently | No deadlock, bounded buffer, output/drain state correct. |
| M4-T04 | Child exits 0/7/-like unsigned code and writes final line then exits | Exact exit code and final output preserved. |
| M4-T05 | Launch invalid executable/working directory | Clear OS error; all resources freed. |
| M4-T06 | Cancel long-running child and (if supported) descendants | Bounded cancellation and accurate status; no orphan handles. |
| M4-T07 | Move session before/after Start and close/restart repeatedly | No double close, leak, crash or deadlock. |
| M4-T08 | Repeat 100 launches, count handles/threads | Counts return near baseline; no unbounded accumulation. |
| M4-T09 | Child emits long lines + multibyte UTF bytes across chunk boundaries | Raw chunks preserved; decoder ownership documented. |
| M4-T10 | Win7 compatibility compile/run (where available) | Win7 path uses supported APIs. If unavailable record `NOT TESTED`, not successful runtime claim. |

## Acceptance Criteria (ALL required)

- [ ] Existing process calls preserve behavior when redirect flags are false.
- [ ] Both stdout/stderr drain concurrently, buffer stays bounded, drop counter is accurate.
- [ ] Exit code vs stream EOF fully distinguished; fast exit doesn't lose final data.
- [ ] No leaked inherited handles, move/destructor/cancel path stable, UI never blocks on read.
- [ ] All available backend harness tests executed with evidence; Win7 availability explicitly reported.
- [ ] M4=PASS with precise remaining compatibility caveats recorded in `progress.md`.

## Milestones

M4 of M5. Enter M5 only with completed M4 gate.

## Current Milestone

M4 pending M3; use `progress.md` for current state.

## Progress

No code modifications or runtime tests during planning. Tick steps after executing.

## Decision Log

Chosen: opt-in pipes, raw-byte chunks, process-exit and output-drain separate, explicit ownership, backward compatibility, no ConPTY.

## Surprises & Discoveries

`WinProcess::Close()` currently means handle closure, not termination. Naively joining a blocking pipe reader inside this method can hang forever on a live child. This is the key lifecycle trap.

## Known Follow-ups

Use M5 to decode text, group UI transcript, launch shell commands, integrate Build/Package. Do not implement these in M4.

## Outcomes

Pending; record harness output, OS version, leak/cancel measurements and remaining platform risk.

## Resume Protocol

Read global README/progress/M4 only; re-check live WinProcess state machine, run remaining tests, then mark M4 PASS and proceed to M5.
