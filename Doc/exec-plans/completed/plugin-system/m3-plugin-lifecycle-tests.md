# M3 — Threaded Plugin Lifecycle and Stress Tests

## Goal

Prove the lifecycle required by future Blender Live Link:

```text
plugin DLL
+ background worker
+ main-thread Tick
+ safe shutdown/join
+ Destroy
+ FreeLibrary
```

This is the most important Plugin MVP safety milestone.

---

## Relevant files

Expected:

```text
Test/Src/TestPluginSystem.cpp

Test/Plugins/SmokePlugin/CMakeLists.txt
Test/Plugins/SmokePlugin/SmokePlugin.cpp

Test/Plugins/BadVersionPlugin/CMakeLists.txt
Test/Plugins/BadVersionPlugin/BadVersionPlugin.cpp

Test/CMakeLists.txt
```

Standalone executable:

```text
PluginSmoke.exe
```

It must not require full Editor/Graphics initialization.

---

## SmokePlugin

Suggested shape:

```cpp
class SmokePlugin final : public IPlugin
{
public:
    bool OnLoad(const PluginContext& context) noexcept override;
    void OnUnload() noexcept override;
    void Tick(f32 delta_time) noexcept override;

private:
    void WorkerMain();

private:
    const PluginContext* _context = nullptr;
    std::atomic<bool> _stop{false};
    std::atomic<u64> _tick_count{0u};
    std::atomic<u64> _worker_count{0u};
    std::thread _worker;
};
```

`OnLoad()`:

```text
save context
_stop = false
start std::thread
return true
```

Worker:

```text
while (!_stop)
{
    ++_worker_count;
    sleep 1 ms
}
```

`Tick()`:

```cpp
++_tick_count;
```

`OnUnload()`:

```text
_stop = true
if worker.joinable()
    worker.join()
```

Never detach the worker.

Optional destructor assertion:

```text
worker must not still be joinable
```

---

## Test-only exports

SmokePlugin may export:

```cpp
AILU_PLUGIN_EXPORT u64 AiluPluginTestGetTickCount() noexcept;
AILU_PLUGIN_EXPORT u64 AiluPluginTestGetWorkerCount() noexcept;
AILU_PLUGIN_EXPORT bool AiluPluginTestIsWorkerRunning() noexcept;
```

These are not official Plugin ABI.

Rules:

- Call only while the DLL is loaded.
- Never call a cached test export after unload.

---

## BadVersionPlugin

Minimal behavior:

```cpp
AILU_PLUGIN_EXPORT u32 AiluPluginGetApiVersion() noexcept
{
    return kPluginApiVersion + 1u;
}
```

Expected:

```text
LoadLibrary succeeds
→ API mismatch
→ rollback
→ FreeLibrary
→ LoadPlugin returns false
```

After rejection:

```cpp
GetModuleHandleW(...) == nullptr;
```

---

## Test target staging

Expected:

```text
PluginSmoke.exe
Plugins/
├── SmokePlugin/
│   └── AiluPluginSmoke.dll
└── BadVersionPlugin/
    └── AiluPluginBadVersion.dll
```

Paths are relative to executable.

No absolute developer paths.

---

## Required tests

### TestDynamicLibrary

- Load.
- IsLoaded.
- GetSymbol.
- MissingSymbol.
- Unload.
- module absent after unload.

### TestPluginLoad

- Initialize manager.
- Load SmokePlugin.
- loaded count == 1.
- ID lookup succeeds.

### TestPluginTick

- Tick x10.
- `tick_count > 0`.
- `worker_count > 0`.

### TestDuplicatePlugin

Load the same DLL twice:

```text
first succeeds
second fails
loaded count remains one
```

Ensure the second candidate load does not leave an extra module reference.

### TestPluginUnload

Before:

```text
GetModuleHandleW(...) != nullptr
```

After:

```text
GetModuleHandleW(...) == nullptr
```

### TestRepeatedLoadUnload

Default:

```cpp
constexpr u32 kRepeatCount = 100u;
```

Loop:

```text
Load
Tick 3–10 times
sleep a few ms
verify worker/tick activity
Unload
verify module absent
```

Optional manual stress:

```text
1000 iterations
```

### TestBadVersion

- Load returns false.
- loaded count remains zero.
- DLL not resident.

### TestShutdown

- Load SmokePlugin.
- Do not manually unload.
- Call `PluginManager::Shutdown()`.
- loaded count becomes zero.
- module is unloaded.

---

## Target output

```text
Plugin System Tests

[PASS] DynamicLibrary
[PASS] Plugin ABI
[PASS] Plugin load
[PASS] Plugin tick
[PASS] Plugin threaded worker
[PASS] Plugin unload
[PASS] Plugin repeated load/unload x100
[PASS] Duplicate plugin rejection
[PASS] Bad API version rejection
[PASS] PluginManager shutdown

10 passed, 0 failed
```

Additionally:

```text
0 crash
0 hang
0 leaked module
```

---

## Acceptance

- [x] Threaded plugin loads.
- [x] Tick executes.
- [x] Worker executes.
- [x] Worker joins before object destruction.
- [x] Plugin unload succeeds.
- [x] DLL absent after unload.
- [x] repeated load/unload x100 passes.
- [x] duplicate ID path does not leak module references.
- [x] bad API version rejected.
- [x] bad-version DLL absent after rejection.
- [x] Shutdown unloads active plugin.
- [x] 0 crashes.
- [x] 0 hangs.
- [x] 0 leaked modules.
- [x] `cmake --build --preset build-debug` passes.

---

## MVP gate

When M3 passes, the base Plugin MVP is complete.

Only after this gate may work begin on Blender Live Link.

M4–M6 improve host integration and extension ergonomics but must not weaken the M0–M3 lifecycle guarantees.

---

## Milestone completion procedure

After acceptance:

1. Mark M3 complete in `progress.md`.
2. Record final MVP validation.
3. Set current milestone to M4.
4. Continue with `m4-editor-integration.md`.

## Validation — 2026-09-30

PluginSmoke: 10 passed, 0 failed, repeated x100 with worker/tick activity per iteration and module absence after each unload. Exit 0, no crash/hang/module leak. Full build-debug exit 0. Thread startup catches exceptions; OnUnload stops/joins; destructor checks non-joinability; test export pointers never survive unload. BadVersion exports no complex ABI symbols. CMake integration stays in existing Test/CMakeLists.txt rather than per-plugin CMake files.
