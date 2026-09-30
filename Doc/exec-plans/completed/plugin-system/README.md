# AiluEngine Dynamic Plugin System ExecPlan

> Status: Complete  
> Current Milestone: M0–M6 — accepted  
> Primary Goal: Build a minimal, dynamically loadable/unloadable plugin foundation with verifiable `FreeLibrary` safety.  
> Blender Live Link is explicitly out of scope for this plan. It is the first intended real consumer after the Plugin MVP is proven safe.

---

## Purpose

Implement the first version of AiluEngine's dynamic plugin system.

The initial system must stay deliberately small. The primary success criterion is lifecycle safety:

```text
LoadLibrary
→ ABI validation
→ CreatePlugin
→ OnLoad
→ Tick
→ OnUnload
→ DestroyPlugin
→ clear plugin-owned callable/function-pointer state
→ FreeLibrary
```

Every milestone has a hard validation gate. Do not advance when the current milestone has failed acceptance items.

This directory is the durable task state. Do not rely on chat history.

---

## Scope

This ExecPlan includes:

- Windows dynamic library abstraction.
- Stable v1 plugin ABI.
- `PluginModule`.
- `PluginManager`.
- Plugin lifecycle state machine.
- Main-thread ownership rules.
- Threaded smoke plugin.
- Bad-version plugin.
- Repeated load/unload stress test.
- Editor lifecycle integration.
- Manifest discovery/scanning.
- First owner-based registration scope.
- Minimal CMake/test integration.
- Plugin memory tag if it remains a small localized change.

---

## Non-goals

Do **not** expand this task to implement:

- Blender Live Link.
- Plugin dependency graph.
- SemVer dependency resolution.
- Automatic plugin update/download.
- Plugin marketplace.
- Runtime shader/RHI backend plugins.
- Dynamic reflection-type registration.
- Dynamic ECS component registration.
- Arbitrary Editor UI extension APIs.
- Plugin-owned `Layer` injection into `LayerStack`.
- Automatic hot reload with state preservation.
- DLL shadow copy.
- Player automatic Runtime Plugin loading.
- Plugin-aware `JobSystem` ownership.
- Full module/package framework.
- Generic service locator redesign.

These are follow-up tasks and must not block the base plugin system.

---

## Architecture constraints

Read before implementation:

```text
AGENTS.md
Doc/architecture/index.md
Doc/architecture/runtime-lifecycle.md
Doc/architecture/threading.md
Doc/architecture/editor-runtime-boundary.md
Doc/architecture/reflection-codegen.md
```

### Dependency direction

Required:

```text
Plugin.dll
    │
    ▼
 Engine.dll

Editor.exe
    │
    ▼
 Engine.dll
```

Forbidden:

```text
Plugin.dll
    │
    ▼
Editor private implementation
```

The plugin host belongs in Engine.

Editor is a host/consumer of the Engine plugin system.

Future Editor features must be exposed through narrow host/service interfaces rather than private `EditorApp` implementation headers.

### Ownership

```text
PluginManager
    ↓ owns
PluginModule
    ├── DynamicLibrary
    ├── resolved export pointers
    ├── IPlugin* instance
    ├── copied PluginInfo
    └── plugin state
```

The plugin DLL owns allocation and destruction of the concrete plugin object.

Engine must never directly `delete` a plugin instance.

### Threading

The first version is owner-thread-only.

These calls must occur on the `PluginManager` owner thread:

```text
LoadPlugin
UnloadPlugin
Tick
Shutdown
```

No cross-thread Load/Unload is part of this plan.

Plugin worker threads marshal results back through plugin-owned thread-safe queues and apply them from main-thread `Tick()`.

### Shutdown

`FreeLibrary` is always the final step:

```text
OnUnload
→ stop threads / wait jobs / release registrations
→ DestroyPlugin
→ instance = nullptr
→ clear all saved plugin function pointers
→ FreeLibrary
```

### Callback escape

Before owner-based registration exists, plugins must not leave plugin-owned lambdas/function targets in arbitrary Engine/Editor delegates or registries.

### Reflection

The first version must not dynamically register plugin-defined:

```text
ACLASS
ASTRUCT
AENUM
```

---

## Current repository baseline

At the start of this plan:

- `Engine` is built as a `SHARED` DLL.
- Engine already has the `AILU_API` export/import mechanism.
- `Editor` is an EXE linking Engine.
- `Application` provides frame hooks/lifecycle infrastructure.
- `MainThreadDispatcher` exists.
- Delegate infrastructure supports handle-based subscribe/unsubscribe.
- `Application::BeginFrame()` invokes the before-update delegate.
- `Application::Finalize()` tears down EngineRuntime and core global systems.
- `JobSystem` can wait for tasks, but has no plugin ownership tracking.
- No `PluginManager`, `IPlugin`, `PluginModule`, or `DynamicLibrary` infrastructure currently exists.

The standalone plugin smoke test must not require Editor or Graphics initialization.

---

## Milestone map

| Milestone | Purpose | Detail |
| --- | --- | --- |
| M0 | DynamicLibrary platform wrapper | `m0-dynamic-library.md` |
| M1 | Stable Plugin ABI v1 | `m1-plugin-abi.md` |
| M2 | PluginModule + PluginManager | `m2-plugin-manager.md` |
| M3 | Threaded lifecycle/stress tests | `m3-plugin-lifecycle-tests.md` |
| M4 | Editor lifecycle integration | `m4-editor-integration.md` |
| M5 | Manifest discovery/scan/autoload | `m5-plugin-manifest.md` |
| M6 | Owner-based registration scope | `m6-registration-scope.md` |

M0–M3 together define the Plugin MVP.

Blender Live Link must not begin before M3 passes.

---

## Global safety invariants

These apply to every milestone.

### S1 — Plugin object never outlives DLL

Immediately before `FreeLibrary`:

```text
_instance == nullptr
```

must be true.

### S2 — Engine never directly destroys a concrete plugin object

Engine stores only:

```cpp
IPlugin*
```

and calls the plugin-provided destroy export.

### S3 — Descriptor pointers never survive unload

Descriptor data must be copied into Engine-owned `PluginInfo`.

### S4 — Plugin function pointers never survive unload

Before `FreeLibrary`, clear all plugin export pointers.

### S5 — Plugin threads stop before `OnUnload()` returns

Never detach plugin worker threads.

### S6 — Plugin-owned jobs are the plugin's responsibility in v1

If a `JobSystem` callable executes plugin code, the plugin must wait for it before `OnUnload()` returns.

### S7 — No dynamic reflection types in v1

No plugin-owned reflection registration.

### S8 — No plugin-owned `LayerStack` object in v1

Do not push plugin concrete `Layer` instances into long-lived host ownership.

### S9 — No unload during manager Tick iteration

`UnloadPlugin()` returns false while `_is_ticking == true`.

### S10 — Every failed load fully rolls back

A failed load must leak none of:

- library reference.
- instance.
- function pointer.
- manager entry.
- registration.
- worker thread.

---

## CMake and staging conventions

Test plugin DLLs live under:

```text
Test/Plugins/
```

Targets:

```text
AiluPluginSmoke
AiluPluginBadVersion
```

They may link Engine but must not link Editor.

Standalone runner:

```text
PluginSmoke.exe
```

Expected staging:

```text
PluginSmoke.exe
Plugins/
├── SmokePlugin/
│   └── AiluPluginSmoke.dll
└── BadVersionPlugin/
    └── AiluPluginBadVersion.dll
```

Tests construct paths relative to the executable.

Never hardcode developer absolute paths.

---

## Logging conventions

Use:

```text
[Plugin]
```

Examples:

```text
[Plugin] Loaded com.ailu.test.smoke (0.1.0)
[Plugin] Unloaded com.ailu.test.smoke

[Plugin] LoadLibrary failed: ...
[Plugin] Missing export AiluPluginCreate: ...
[Plugin] API version mismatch: host=1 plugin=2
[Plugin] Duplicate plugin id: ...
[Plugin] OnLoad failed: ...
```

Do not log every Tick.

---

## Progress and live state

The frequently changing task state lives in:

```text
progress.md
```

Do not duplicate detailed live progress here.

Current milestone:

```text
M0–M6 — accepted
```

---

## Known follow-ups

Do not implement these inside this plan unless the plan is explicitly revised:

- DLL shadow copy / true hot reload.
- Plugin dependency graph.
- Editor UI extension API beyond M6 infrastructure.
- Plugin-aware JobSystem ownership.
- Player runtime plugin autoload.
- Dynamic reflected plugin types.

---

## Blender Live Link handoff

After M0–M3 are fully accepted:

```text
Plugins/
└── BlenderLiveLink/
    ├── plugin.json
    └── AiluBlenderLiveLink.dll
```

Expected lifecycle:

```text
Load Plugin
→ BlenderLiveLinkPlugin::OnLoad
→ create LiveLinkServer
→ start socket worker
→ PluginManager::Tick
→ flush network commands on main thread
→ OnUnload
→ stop socket
→ join worker
→ clear queue
→ release registrations
→ DestroyPlugin
→ FreeLibrary
```

M3 exists specifically to prove the dangerous part of this lifecycle before Live Link code is introduced.

---

## Resume protocol

When resuming after context compaction or in a new thread:

1. Read `AGENTS.md`.
2. Read `Doc/architecture/index.md`.
3. Read only architecture documents relevant to the current milestone.
4. Read this `README.md`.
5. Read `progress.md`.
6. Read only the current `mX-*.md` milestone file.
7. Inspect current git diff and source files relevant to that milestone.
8. Continue from the first unchecked acceptance item.
9. Update `progress.md` and current milestone file before advancing.

Do not reconstruct task state from old conversation history.

---

## Completion

When the whole task completes:

1. Fill the final Outcomes section in `progress.md`.
2. Add/update `Doc/architecture/plugin-system.md`.
3. Update any affected runtime/threading/editor architecture docs.
4. Move this directory from:

```text
Doc/exec-plans/active/plugin-system/
```

to:

```text
Doc/exec-plans/completed/plugin-system/
```
