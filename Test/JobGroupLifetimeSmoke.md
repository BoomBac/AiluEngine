# JobGroup lifetime reproduction

Standalone executable using the real Engine JobSystem; no Editor, renderer or Engine source modifications.

```powershell
cmake --build --preset build-debug --target JobGroupLifetimeSmoke
# Run in the executable directory, with its Engine/runtime DLLs present:
.\JobGroupLifetimeSmoke.exe stack 20000
.\JobGroupLifetimeSmoke.exe guarded 200000
```

Each iteration dispatches one grouped job. A gate ensures the worker has started before the caller releases it and calls
`Wait(group)`. The caller immediately destroys the group when Wait returns. The captured gate/counter outlive job cleanup
through a separate `system.Wait()`. Only one worker is used to isolate the completion/destruction window.

- `stack`: ordinary scoped JobGroup destruction.
- `guarded`: placement construction in VirtualAlloc memory, followed by destruction and immediate VirtualFree. This detects
  late group accesses without adding scheduler hooks or modifying Done/Wait. It increases fault detection; it does not
  represent ordinary stack allocation behavior.

While waiting for the worker to start, a separate ungrouped no-op is dispatched every 10 ms if necessary. This avoids a
worker wake-up stall encountered during the first experiment. It does not execute the grouped job on the caller. The
worker wake-up issue is separate from the lifetime fault and has not been independently diagnosed by this test.

Unhandled exceptions write `JobGroupLifetimeSmoke.dmp` in the working directory. A later crash overwrites this file.

## Validation on 2026-10-01

- Debug target compilation/linking passed.
- Stack mode: 20,000 iterations completed without an observed crash. This does not disprove the race.
- Guarded mode: two runs crashed after reporting 10,000 completed iterations, before reaching 20,000.
- The final executable, using only dump capture in the crash handler, crashed again before the first 10,000-iteration
  progress message. Its fresh dump showed the same notify_all/Done/CompleteJob/WorkerThread return-address chain.
- Exception: access violation `0xc0000005` inside ntdll; dump captured before exception-handler symbol analysis.
- Exception-thread stack return addresses were extracted from the dump and symbolized against the matching Engine DLL/PDB:
  `std::condition_variable::notify_all` (mutex:532), `JobGroup::Done` (JobSystem.cpp:33),
  `JobSystem::CompleteJob` (303), `JobSystem::WorkerThread` (274), worker lambda (136).
  These match the reported Editor crash. This is stack-memory/return-address analysis, not a full debugger unwind.
- Root cause supported by source and reproduction: the last Done publishes remaining=0 before notify_all completes;
  Wait can return and allow destruction of the condition variable while the worker still accesses it.
- Engine JobSystem remains unchanged; no fix or post-fix acceptance is included.

## Fix and revalidation on 2026-10-01

The reproduction above describes the pre-fix implementation. Done now holds the group mutex across both decrement and
notification. IsDone acquires that same mutex, including the initial Wait completion check. Wait uses a direct atomic
predicate while already holding the mutex to avoid recursive locking, and still executes WorkOnce outside the mutex.
The destructor evaluates IsDone outside the assertion so its synchronization is preserved when assertions are disabled.

- Debug reproduction target rebuilt successfully.
- Guarded mode: 200,000 iterations completed, exit code 0, no observed crash.
- Stack mode: 200,000 iterations completed, exit code 0, no observed crash.
- Full `cmake --build --preset build-debug`: compilation and linking passed, including Engine and Editor.
- This accepts the standalone lifetime regression; live Editor behavior has not been tested.
