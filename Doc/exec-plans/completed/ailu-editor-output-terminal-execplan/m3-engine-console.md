# M3 — Engine Console Registry + Terminal Dock (Engine Mode)

> **Prerequisite:** `M2 PASS` in `progress.md`. M3 MUST work **without** spawning OS processes; M4/M5 own that functionality. Read README + progress + only this file.

## Purpose

Introduce a small, testable engine command subsystem inspired by UE console commands and add the Terminal's **Engine** view. The subsystem must be callable headlessly for automation, yet it may not silently run external OS commands or bypass existing editor Undo semantics.

## Scope

- Register/unregister typed engine/editor commands with name, help, argument schema, callback and thread policy.
- Tokenize quoted arguments, validate arity/types and present informative errors; support `help`, `clear`, command history and suggestion/completion.
- Add reflected `TerminalWindow : DockWindow`, Engine command input, transcript and status; native Dock menu and persistence.
- Use bounded transcript storage and virtualized drawing for large outputs (reuse `UITableElement` or implement a small *reusable* line-data-source-backed virtual list).
- Provide initial side-effect-free commands and a **small, thread-safe** renderer/scene diagnostic sample only if services are available on approved thread.

## Non-goals

No `CreateProcessW`, no interactive cmd/PowerShell, no pipes, no alias expansion executing shell, no arbitrary reflection writes, no remote command execution, no synchronous heavy scene scans in UI callbacks. Do not route ordinary engine Console commands through undoable `CommandManager` without an explicit appropriate bridge.

## Architecture Constraints

1. `Editor/Src/EditorApp.cpp` currently creates `g_pCommandMgr` (existing Undo/Command manager), separate from new console registry. Name new registry `ConsoleCommandRegistry` to avoid confusion.
2. Parser is intentionally **not a shell**: separators `;`, `|`, `&&`, redirection `>`, environment expansion and `$()` have no control syntax. Treat them as ordinary arguments or explicit errors; never forward to a shell.
3. Registry never depends on `DockWindow` or `UIManager`. A headless caller should `Execute("help")` without creating Editor UI.
4. Engine-state commands execute on the editor/main thread (or queue to the proper domain with acknowledged completion). Enforce thread assertions/dispatcher; no arbitrary background access to render graph or scene.
5. Avoid unbounded output/callback retention. Registry contains validated callbacks with explicitly scoped lifetimes; unregister commands before owning subsystem destruction.
6. The current `InputBlock` constructor registers Enter handler that commits and clears focus, but exposes only `_on_content_changed`; **do not confuse text change with submit**. Add a narrow `on_submit`/`_on_committed` event or equivalent if event ordering cannot be reliably handled externally.
7. `InputBlock::CommitEdit()` currently logs `LOG_INFO("Committed edit...")`, which can flood Output every typed character. Solve in a reusable low-risk way (e.g. restrict logging to debug or remove unconditional edit log) and regression-test existing inspector fields.

## Baseline

- `Editor/Src/EditorApp.cpp`: creates `g_pCommandMgr = ... CommandManager`; shutdown deletes it. New console registry should not reuse transaction state.
- `Editor/Src/Widgets/EditorLayer.cpp`: native `UI::Menu` built via toolbar, `DockManager::Init()` in attach and `Update(dt)` per frame.
- `Editor/Inc/Dock/DockWindow.h`: `DockWindow : Object` with reflection/type-based persistence.
- `Engine/Inc/UI/Basic.h`, `Engine/Src/UI/Basic.cpp`: `InputBlock::SetContent`, `SetCursorToEnd`, `_on_content_changed`; Enter calls `CommitEdit()` then clears focus; `OnKeyDown()` supports arrows, but content getter is not exposed publicly. Plan for a minimal getter/event.
- `Engine/Inc/UI/Element/UITableElement.h`: data source with fixed-height virtualized rows; reuse for transcript if suitable.
- `Engine/Inc/Platform/Process.h`: unrelated to M3 and **must remain untouched**.

## Target Outcome

Typing `help`, `echo "hello world"`, `clear` and unknown command produces deterministic, readable transcript, validation errors and history navigation without freezing UI. Registry works headlessly and all registered commands have safe lifetime/ownership. Restart/reopen Terminal does not duplicate it; input focus/Enter/Up/Down/Tab work predictably.

## Relevant Files

**Create:** `Editor/Inc/Common/ConsoleCommandRegistry.h`, `Editor/Src/Common/ConsoleCommandRegistry.cpp`, `Editor/Inc/Widgets/TerminalWindow.h`, `Editor/Src/Widgets/TerminalWindow.cpp`.

**Modify if necessary:** `Engine/Inc/UI/Basic.h`, `Engine/Src/UI/Basic.cpp` for reusable submit/get-content event and removal of noisy per-keystroke log; `Editor/Inc/EditorApp.h`, `Editor/Src/EditorApp.cpp` for registry ownership; `Editor/Src/Widgets/EditorLayer.cpp` and its header for Dock/menu entry. Build system/reflection generation as needed.

## Registry API recommendation

Use typed results, not a single `bool` that hides argument errors. An example skeleton (exact typedefs adjusted to actual project's types):

```cpp
struct ConsoleCommandContext
{
    bool _is_editor = true;
    bool _allow_mutation = false;
};

struct ConsoleCommandResult
{
    bool _success = false;
    String _message;
};

struct ConsoleCommandDesc
{
    String _name;
    String _help;
    Vector<String> _argument_help;
    bool _requires_main_thread = true;
    std::function<ConsoleCommandResult(const Vector<String> &, const ConsoleCommandContext &)> _execute;
};

class ConsoleCommandRegistry
{
public:
    bool RegisterCommand(ConsoleCommandDesc command);
    bool UnregisterCommand(StringView name);
    ConsoleCommandResult Execute(StringView line, const ConsoleCommandContext &context);
    Vector<String> Complete(StringView prefix) const;
};
```

Do not force this exact signature if the repo's delegate/callback standard is materially better, but preserve the invariants. Normalize command names (recommend lower ASCII, dot-separated, e.g. `render.stats`) at registration and lookup; reject duplicates with an error; reject empty command names. `Execute` must never silently swallow parser or callback errors. Avoid access to registry containers without a documented thread policy. If registry only allows main-thread access, enforce that instead of pretending it is thread-safe.

### Parser contract / examples

```text
help                     -> builtin help text and command list
echo "hello world"       -> 1 string argument: hello world
echo "a\\\"b"            -> escaped quote in one argument (define behavior in tests)
render.stats             -> if safely registered, read-only diagnostic
unknown                 -> Unknown command: unknown, suggested matches
                         -> blank input: no-op; no duplicate history
```

Define and test exactly one escape grammar:

- Tokens separated by ASCII spaces/tabs outside double quotes.
- Double quotes group spaces; backslash escapes quote and backslash *inside quoted strings*. Outside quotes, preserve backslash in Windows paths unless explicitly escaping the next double quote.
- Reject unclosed double quote and invalid escape with position/error; no shell syntax evaluation.
- Limit input length (recommend 4 KiB), number of arguments (64), transcript record bytes/count; limits should be easy to adjust.
- Under exception-enabled builds, convert user callback exceptions to error result and record diagnostic; do not let a single command crash the whole Editor.

### Built-in commands / registration lifetime

Minimum M3 builtins: `help`, `echo`, `clear` (clear local transcript UI, not M1 engine logs), `version` if a verified version API exists. Optional small read-only `log.level` shows threshold; changing it is a separate explicit action requiring validated ELogLevel parse. Avoid promising unsupported runtime commands (`rg.dump`, etc.) until you can actually wire the service.

Register builtins in a registry-owned or app-owned initialization phase; attach subsystem-specific handlers only when the corresponding renderer/scene exists, unregister on owner shutdown. If a command potentially takes >10ms, schedule it through an existing engine job path **only if the callback lifetime and result dispatch are correct**; otherwise reject/mark deferred, don't block UI.

### Terminal UI state

```text
Terminal [Engine]                       (other modes added in M5)
 [virtualized transcript: command prompt, output, errors, timestamps]
 > [InputBlock]                  [Run] [Clear]
 [history Up/Down] [Tab completion] [status]
```

Use a separate bounded `TerminalTranscript` model, e.g. 5,000 lines or 8 MiB, with line ID and channel (prompt/output/error/status). The data source is independent of M1 history, though an optional explicit `LOG_INFO` for executed command errors may be added later. **Never pipe Output logs into Terminal by default**, which would create confusing duplicates.

Input handling: when Enter is pressed, take the currently edited text *once*, enqueue execution exactly once, clear the input, and optionally restore focus. Add a public `InputBlock::GetContent()` const and `DECLARE_DELEGATE(on_submit, String)` or equivalent. If internal Enter event clears focus first, emit dedicated callback **before** focus cleanup. `SetContent("", false)` prevents looped text-change callbacks. History Up/Down with a temporary edit buffer; Tab suggestion popup (may begin as cycling exact prefix suggestions without new UI infrastructure). Explicit Run button fallback is mandatory for accessibility/testing.

Terminal Dock: register reflection, add Window > Terminal handler using `ActivateDock()/HasDock()/AddDock()`, persist minimal flags/last tab only, **never persist transcript containing shell output/secrets**. Closing the dock does not destroy the registry. In M3 only the `Engine` tab is present; don't show a nonworking PowerShell tab.

## Implementation steps

- [ ] **Step 1:** verify M2 PASS; inspect current `InputBlock`, Dock and `CommandManager` to confirm API and conflicts. Find project reflection/build generation steps.
- [ ] **Step 2:** create tokenizer and parser tests independently of UI. Implement quotes, escaping, invalid input, hard limits and error positions. Use no `system()`, `popen()`, or `CreateProcess*` in M3.
- [ ] **Step 3:** create registry, result/error type, duplicate handling, typed arg validation, completion and safe register/unregister lifetime. Register `help`/`echo` and test headlessly.
- [ ] **Step 4:** add `TerminalTranscript` bounded model with event IDs, channel and append/get/clear interface. Decide whether it belongs in TerminalWindow implementation or small common module based on actual reuse; avoid premature abstraction.
- [ ] **Step 5:** build reflected `TerminalWindow`, native virtualized transcript, input, Run/Clear and status. Use `UITableElement` if possible with row height/text clipping. Ensure data source detaches during destruction.
- [ ] **Step 6:** implement InputBlock submit event (if required), content getter, Up/Down/Tab semantics without breaking text caret when history is absent. Remove/quiet unconditional input edit logs minimally; test inspector input controls.
- [ ] **Step 7:** wire UI → registry → result transcript. Dispatch on correct thread, use a single submit path shared by Enter and Run to avoid duplicate execution. `clear` should clear the local transcript only, not service event history.
- [ ] **Step 8:** add native Window > Terminal open-or-activate entry, reflection generator integration, close/reopen, saved layout and clean startup without duplicated registrations.
- [ ] **Step 9:** tests: tokenizer, lifetime, builtins, UI and headless. Record results; only then advance to M4.

## Edge cases / decisions

- Duplicate registration: fail with clear diagnostic, do not replace callback from another subsystem silently.
- `help` output lists registered commands deterministically (sorted by name), no random hash map iteration order.
- Unknown command suggests at most a few prefix/edit-distance candidates, bounded work.
- A mutating command must never run in renderer worker context; document correct dispatcher or restrict M3 to safe commands.
- Input may include `中文`, path separators, quotes, history navigation; command name grammar can be ASCII while args remain UTF-8 (test conversion).
- Terminal restored from layout before EditorApp finishes registering commands: show status and defer command execution until registry is available; prefer initialize registry before Dock/UI.
- Terminal view closed during command: model/result should finish safely; avoid capture of raw UI `this` in registry callbacks.
- Empty input not added to history. Deduplicate consecutive identical commands if documented. Limit history length (recommend 100 entries).
- Command handler error does not enter the regular logging pipeline while `EditorLogService` is holding a sink lock; avoid lock inversion, especially with callbacks.

## Validation

| Test ID | Procedure | Expected |
|---|---|---|
| M3-T01 | Build Engine+Editor, reflection generation | Terminal class compiled and restorable. |
| M3-T02 | Parser corpus: empty, quotes, escaped quotes/backslashes, unmatched quotes, max length | Correct tokens or precise error; never executes a shell. |
| M3-T03 | Register 2 names, try duplicate, unregister, invoke again | Duplicate rejected; unregistered handler cannot run. |
| M3-T04 | Run `help`, `echo "hello world"`, unknown from unit test (no GUI) | Correct success/error result and output; deterministic listing. |
| M3-T05 | Run commands via Enter and Run, check history Up/Down, Tab | One execution per submit, correct input/caret behavior. |
| M3-T06 | Type many chars into input and Output search | No excessive `Committed edit` INFO flooding M1 logs. |
| M3-T07 | Generate 20k transcript lines, scroll while appending | Transcript cap respected; virtualized display; no freeze. |
| M3-T08 | Close/reopen Terminal, restart Editor with layout | No duplicate instances/handlers; usable after restore. |
| M3-T09 | Run in batch/headless mode without `UIManager` | Registry commands work with no DockWindow. |
| M3-T10 | Exercise existing inspector `InputBlock` flows | No broken Enter commit, caret selection or property changes. |

## Acceptance Criteria (ALL required)

- [ ] Parser/registry headless tests pass; no OS process execution in M3.
- [ ] `help`/`echo`/`clear` complete; errors and argument mismatches are explicit.
- [ ] Native Terminal Engine tab submits **once** on Enter/Run and keeps history/completion usable.
- [ ] Registry and UI lifetimes are decoupled; bounded transcript and no source log flood.
- [ ] Reflection, Dock close/reopen/restart and existing InputBlock behavior verified.
- [ ] Build/test evidence and M3=PASS recorded in `progress.md`.

## Milestones

M3 of M5. When and only when PASS, open `m4-process-pipes.md`.

## Current Milestone

M3 pending M2; runtime status in `progress.md`.

## Progress

No code changes/test execution in planning session. Keep checkboxes and status updated during implementation.

## Decision Log

ConsoleRegistry is separate from Undo CommandManager; no implicit OS shell grammar; no interactive process in M3; command lifetime is explicit.

## Surprises & Discoveries

`InputBlock` does not currently offer a public submitted-text event. Its `CommitEdit()` logs INFO, making naive command/search UI unexpectedly noisy.

## Known Follow-ups

Task-mode UI, stdout/stderr and OS cancellation belong to M5 after M4. Real renderer live commands can be added incrementally once stable thread-safe hooks exist.

## Outcomes

Pending; store test evidence, supported builtins and any security constraints in `progress.md`.

## Resume Protocol

Read README + progress + M3 only, then finish first unchecked M3 step/test; verify full gate and advance to M4 only after PASS.
