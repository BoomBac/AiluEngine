# AiluEngine Dynamic Plugin System ExecPlan

> Status: Active  
> Current Milestone: M0 — DynamicLibrary platform wrapper  
> Primary Goal: Build a minimal, dynamically loadable/unloadable plugin foundation with verifiable `FreeLibrary` safety.  
> Blender Live Link is explicitly out of scope for this plan. It is the first intended real consumer after the plugin MVP is proven safe.

---

## 1. Purpose

Implement the first version of AiluEngine's dynamic plugin system.

The initial system must be deliberately small. Its primary success criterion is **lifecycle safety**, especially the correctness of:

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

The plan is intentionally staged. A milestone must pass its acceptance criteria before work continues to the next milestone.

This plan must remain usable after context compaction or a new Codex thread. Do not rely on chat history for task state.

---

## 2. User-visible outcome

After M0–M3, AiluEngine must have a standalone plugin MVP that supports:

```text
Ailu Editor / Test Host
        │
        ▼
   PluginManager
        │
        ├── LoadPlugin(path)
        │      ↓
        │   LoadLibrary
        │      ↓
        │   API Version Check
        │      ↓
        │   Resolve fixed C ABI
        │      ↓
        │   CreatePlugin
        │      ↓
        │   OnLoad
        │
        ├── Tick()
        │      ↓
        │   IPlugin::Tick
        │
        └── UnloadPlugin(id)
               ↓
             OnUnload
               ↓
           DestroyPlugin
               ↓
         clear function ptrs
               ↓
            FreeLibrary
```

The MVP is not complete until repeated threaded load/unload is proven safe.

---

## 3. Scope

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
- First owner-based registration concept required before richer Editor extensions.
- Minimal CMake/test integration.
- Plugin memory tag if it remains a small localized change.

---

## 4. Non-goals

Do **not** expand this task to implement:

- Blender Live Link.
- Plugin dependency graph.
- SemVer dependency resolution.
- Automatic plugin update/download.
- Plugin marketplace.
- Runtime shader backend plugins.
- Runtime RHI backend plugins.
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

These are follow-up tasks. None may block the base plugin system.

---

## 5. Architecture constraints

Relevant long-term architecture documents:

```text
Doc/architecture/runtime-lifecycle.md
Doc/architecture/threading.md
Doc/architecture/editor-runtime-boundary.md
Doc/architecture/reflection-codegen.md
```

Repository-wide constraints from `AGENTS.md` also apply.

### 5.1 Dependency direction

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

Future Editor features must be exposed through narrow host/service interfaces instead of plugins including private `EditorApp` implementation headers.

### 5.2 Ownership

`PluginManager` owns `PluginModule`.

`PluginModule` owns:

```text
DynamicLibrary
resolved export pointers
IPlugin* instance
copied PluginInfo
plugin state
```

The plugin DLL owns the concrete plugin object's allocation and destruction.

Engine must never directly `delete` a plugin instance.

### 5.3 Threading

The first version is owner-thread-only.

These calls must occur on the `PluginManager` owner thread:

```text
LoadPlugin
UnloadPlugin
Tick
Shutdown
```

No cross-thread Load/Unload is part of this task.

Plugin worker threads must marshal data back to the plugin's main-thread `Tick()` through plugin-owned thread-safe queues.

### 5.4 Shutdown

`FreeLibrary` is always the final operation.

The required order is:

```text
OnUnload
→ stop threads / wait jobs / release registrations
→ DestroyPlugin
→ instance = nullptr
→ clear all saved plugin function pointers
→ FreeLibrary
```

### 5.5 No plugin callback escape in MVP

During M0–M5, plugin-owned function/lambda targets must not be stored long-term by arbitrary Engine/Editor global systems.

In particular, do not allow a plugin to register:

```cpp
some_global_delegate += [this]()
{
    ...
};
```

unless the registration is fully host-owned and safely removable before DLL unload.

### 5.6 No reflection registration in MVP

The first version must not register plugin-defined:

```text
ACLASS
ASTRUCT
AENUM
```

Dynamic unload ownership for reflection/type registration is not part of the existing infrastructure.

---

## 6. Current repository baseline

At the start of this ExecPlan:

- `Engine` is built as a `SHARED` DLL.
- Engine already has the `AILU_API` export/import mechanism.
- `Editor` is an EXE linking `Engine`.
- `Application` already provides frame hooks/lifecycle infrastructure.
- `MainThreadDispatcher` exists.
- Delegate infrastructure supports handle-based subscribe/unsubscribe.
- `Application::BeginFrame()` invokes the before-update delegate.
- `Application::Finalize()` eventually tears down EngineRuntime and core global systems.
- `JobSystem` can wait for tasks, but has no plugin ownership tracking.
- No `PluginManager`, `IPlugin`, `PluginModule`, or `DynamicLibrary` infrastructure currently exists.

The first implementation must not require Editor or Graphics initialization to execute the standalone plugin smoke test.

---

## 7. Relevant files

Expected new files:

```text
Engine/Inc/Framework/Platform/DynamicLibrary.h
Engine/Src/Framework/Platform/DynamicLibrary.cpp

Engine/Inc/Framework/Plugin/PluginApi.h
Engine/Inc/Framework/Plugin/PluginManager.h
Engine/Src/Framework/Plugin/PluginManager.cpp

Test/Src/TestPluginSystem.cpp

Test/Plugins/SmokePlugin/CMakeLists.txt
Test/Plugins/SmokePlugin/SmokePlugin.cpp

Test/Plugins/BadVersionPlugin/CMakeLists.txt
Test/Plugins/BadVersionPlugin/BadVersionPlugin.cpp
```

Expected modified files:

```text
Engine/Inc/Framework/Common/Allocator.hpp
Test/CMakeLists.txt
Editor/Inc/EditorApp.h
Editor/Src/EditorApp.cpp
```

M5 may additionally add:

```text
Engine/Inc/Framework/Plugin/PluginManifest.h
Engine/Src/Framework/Plugin/PluginManifest.cpp
```

Do not treat this list as permission for unrelated refactors.

---

# 8. Milestones

---

## M0 — DynamicLibrary platform wrapper

### Goal

Provide a minimal platform abstraction over Windows DLL loading without leaking `HMODULE` into public headers.

### Files

```text
Engine/Inc/Framework/Platform/DynamicLibrary.h
Engine/Src/Framework/Platform/DynamicLibrary.cpp
```

### Suggested API

```cpp
#pragma once

#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

namespace Ailu
{
    class AILU_API DynamicLibrary final : public NonCopyable
    {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();

        bool Load(const WString& path);
        void Unload();

        [[nodiscard]] bool IsLoaded() const;
        [[nodiscard]] void* GetSymbol(const char* name) const;
        [[nodiscard]] const WString& Path() const { return _path; }

    private:
        void* _handle = nullptr;
        WString _path;
    };
}
```

Windows implementation uses:

```text
LoadLibraryW
GetProcAddress
FreeLibrary
GetLastError
```

### Required behavior

`Load()`:

- Must not silently replace an already loaded module.
- Missing file/path returns `false`.
- `LoadLibraryW` failure logs:
  - DLL path.
  - `GetLastError()`.
- Successful load stores a normalized path.

`GetSymbol()`:

- Returns `nullptr` when unloaded.
- Returns `nullptr` for a missing symbol.
- Must not assert for a missing symbol.

`Unload()`:

- Safe when already unloaded.
- Calls `FreeLibrary`.
- Sets `_handle = nullptr`.
- Clears `_path`.

Destructor:

```cpp
DynamicLibrary::~DynamicLibrary()
{
    Unload();
}
```

Important:

`DynamicLibrary` does not know whether plugin instances or function pointers are still alive. Higher-level code must guarantee correct ordering.

### Acceptance

- [ ] `DynamicLibrary.Load` passes.
- [ ] `DynamicLibrary.GetSymbol` passes.
- [ ] Missing symbol returns null.
- [ ] `DynamicLibrary.Unload` passes.
- [ ] `GetModuleHandleW(plugin_name) == nullptr` after unload.
- [ ] `cmake --build --preset build-debug` passes.

**Do not start M1 until all M0 checks pass.**

---

## M1 — Stable Plugin ABI v1

### Goal

Define the smallest fixed C ABI needed to discover, create, use, and destroy a plugin.

### File

```text
Engine/Inc/Framework/Plugin/PluginApi.h
```

### API version

```cpp
namespace Ailu
{
    inline constexpr u32 kPluginApiVersion = 1u;
}
```

Increment API version only for ABI-breaking changes such as:

- `IPlugin` virtual layout/order changes.
- `PluginContext` incompatible layout changes.
- `PluginDescriptor` incompatible layout changes.
- Calling convention changes.

### Plugin type

```cpp
enum class EPluginType : u8
{
    kRuntime,
    kEditor,
};
```

Rules:

- Editor host may load both `kEditor` and `kRuntime`.
- Player auto-loading is not part of this plan.
- Blender Live Link will later be `kEditor`.

### PluginDescriptor

Do not pass `std::string` through the DLL ABI.

```cpp
struct PluginDescriptor
{
    const char* _id = nullptr;
    const char* _name = nullptr;
    const char* _version = nullptr;
    EPluginType _type = EPluginType::kRuntime;
};
```

Descriptor strings are owned by static storage in the plugin DLL.

The host must copy them into Engine-owned memory before unloading the DLL.

Never retain the raw descriptor pointer after unload.

Example:

```cpp
static const PluginDescriptor s_descriptor{
    "com.ailu.test.smoke",
    "Ailu Smoke Plugin",
    "0.1.0",
    EPluginType::kEditor,
};
```

### IPluginHost

Keep v1 deliberately narrow.

```cpp
class IPluginHost
{
public:
    virtual ~IPluginHost() = default;

    virtual void LogInfo(const char* message) noexcept = 0;
    virtual void LogWarning(const char* message) noexcept = 0;
    virtual void LogError(const char* message) noexcept = 0;
    virtual bool IsMainThread() const noexcept = 0;
};
```

Do not expose entire:

```text
SceneMgr
ResourceMgr
Renderer
EditorApp
```

Future real plugins get narrow services later.

### PluginContext

```cpp
struct PluginContext
{
    u32 _api_version = kPluginApiVersion;
    IPluginHost* _host = nullptr;
};
```

### IPlugin

```cpp
class IPlugin
{
public:
    virtual ~IPlugin() = default;

    virtual bool OnLoad(const PluginContext& context) noexcept = 0;
    virtual void OnUnload() noexcept = 0;
    virtual void Tick(f32 delta_time) noexcept = 0;
};
```

Exceptions must not cross the DLL boundary.

A plugin must catch its own internal exceptions before returning through ABI methods.

### Fixed exports

```cpp
#if AL_PLATFORM_WINDOWS
    #define AILU_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
    #define AILU_PLUGIN_EXPORT extern "C"
#endif

AILU_PLUGIN_EXPORT Ailu::u32 AiluPluginGetApiVersion() noexcept;
AILU_PLUGIN_EXPORT const Ailu::PluginDescriptor* AiluPluginGetDescriptor() noexcept;
AILU_PLUGIN_EXPORT Ailu::IPlugin* AiluPluginCreate() noexcept;
AILU_PLUGIN_EXPORT void AiluPluginDestroy(Ailu::IPlugin* plugin) noexcept;
```

Function pointer aliases:

```cpp
using PluginGetApiVersionFn = u32 (*)() noexcept;
using PluginGetDescriptorFn = const PluginDescriptor* (*)() noexcept;
using PluginCreateFn = IPlugin* (*)() noexcept;
using PluginDestroyFn = void (*)(IPlugin*) noexcept;
```

### ABI ordering rule

Resolve and call:

```text
AiluPluginGetApiVersion
```

first.

Only when:

```text
plugin_api_version == kPluginApiVersion
```

may the host interpret the descriptor or create the plugin instance.

### Allocation rule

Forbidden:

```cpp
delete plugin;
```

Required:

```text
AiluPluginCreate()
→ plugin-owned new

AiluPluginDestroy(plugin)
→ plugin-owned delete
```

### Acceptance

- [ ] API version export exists.
- [ ] Descriptor export exists.
- [ ] Create export exists.
- [ ] Destroy export exists.
- [ ] Descriptor id/name/version are valid.
- [ ] Create returns non-null.
- [ ] Destroy succeeds.
- [ ] Host checks version before descriptor.
- [ ] DLL does not include Editor private implementation.
- [ ] Engine never directly deletes concrete plugin object.
- [ ] `cmake --build --preset build-debug` passes.

**Do not start M2 until all M1 checks pass.**

---

## M2 — PluginModule and PluginManager

### Goal

Centralize plugin state and lifecycle so every failure path can rollback safely.

### PluginInfo

Engine-owned copy:

```cpp
struct PluginInfo
{
    String _id;
    String _name;
    String _version;
    EPluginType _type = EPluginType::kRuntime;
    WString _library_path;
};
```

Never store:

```cpp
const PluginDescriptor* _descriptor;
```

as persistent state.

### Plugin state

```cpp
enum class EPluginState : u8
{
    kUnloaded,
    kLoading,
    kActive,
    kUnloading,
    kFailed,
};
```

### PluginModule

Suggested shape:

```cpp
class PluginModule final : public NonCopyable
{
public:
    bool Load(const WString& path, const PluginContext& context);
    void Unload();
    void Tick(f32 delta_time);

    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] const PluginInfo& Info() const;

private:
    DynamicLibrary _library;

    PluginGetDescriptorFn _get_descriptor = nullptr;
    PluginCreateFn _create = nullptr;
    PluginDestroyFn _destroy = nullptr;

    IPlugin* _instance = nullptr;
    PluginInfo _info;
    EPluginState _state = EPluginState::kUnloaded;
};
```

All of these operations must stay centralized in `PluginModule`:

```text
LoadLibrary
GetProcAddress
API check
descriptor copy
Create
OnLoad
Tick
OnUnload
Destroy
FreeLibrary
```

### Load state machine

```text
kUnloaded
   ↓
kLoading
   ↓
DynamicLibrary::Load
   ↓
resolve GetApiVersion only
   ↓
version check
   ↓
resolve remaining exports
   ↓
copy descriptor
   ↓
CreatePlugin
   ↓
OnLoad
   ↓
kActive
```

Every failure must rollback all completed steps.

A failed load must not leave:

- plugin instance.
- resolved plugin pointers.
- loaded module.
- half-registered manager entry.

### Unload order

Required implementation behavior:

```cpp
void PluginModule::Unload()
{
    if (_state != EPluginState::kActive)
        return;

    _state = EPluginState::kUnloading;

    if (_instance != nullptr)
        _instance->OnUnload();

    if (_instance != nullptr && _destroy != nullptr)
    {
        _destroy(_instance);
        _instance = nullptr;
    }

    _get_descriptor = nullptr;
    _create = nullptr;
    _destroy = nullptr;

    _library.Unload();

    _state = EPluginState::kUnloaded;
}
```

The exact final implementation may add error handling/asserts, but the lifecycle ordering may not change.

### PluginManager

Suggested shape:

```cpp
class AILU_API PluginManager final : public NonCopyable
{
public:
    PluginManager();
    ~PluginManager();

    bool Initialize();
    void Shutdown();

    bool LoadPlugin(const WString& path);
    bool UnloadPlugin(const String& plugin_id);

    void Tick(f32 delta_time);

    [[nodiscard]] bool IsLoaded(const String& plugin_id) const;
    [[nodiscard]] u32 LoadedPluginCount() const;

private:
    class Host final : public IPluginHost
    {
    public:
        void LogInfo(const char* message) noexcept override;
        void LogWarning(const char* message) noexcept override;
        void LogError(const char* message) noexcept override;
        bool IsMainThread() const noexcept override;
    };

private:
    HashMap<String, Scope<PluginModule>> _plugins;
    Vector<String> _load_order;
    Host _host;
    PluginContext _context;
    std::thread::id _owner_thread;
    bool _initialized = false;
    bool _is_ticking = false;
};
```

### Owner-thread rules

Record:

```cpp
_owner_thread = std::this_thread::get_id();
```

during construction or `Initialize()`.

Require owner thread for:

```text
LoadPlugin
UnloadPlugin
Tick
Shutdown
```

### Tick/unload rule

During:

```cpp
PluginManager::Tick()
```

set `_is_ticking = true`.

If `UnloadPlugin()` is called while ticking:

- return `false`.
- log warning.
- do not modify `_plugins`.

Do not add a generalized deferred command system in this task.

### Duplicate ID

If a second DLL reports an already loaded ID, reject it.

Log must contain:

```text
duplicate plugin id
existing library path
new library path
```

The second DLL must be fully unloaded after rejection.

### Shutdown

Destructor:

```cpp
PluginManager::~PluginManager()
{
    Shutdown();
}
```

Shutdown order:

```text
stop Tick
→ unload plugins in reverse load order
→ clear manager containers
```

Use `_load_order` because `HashMap` does not guarantee unload ordering.

### Optional memory tag

If the change is small and localized, add:

```cpp
kPlugin
```

to `EMemoryTag`.

This tag is for Engine-side plugin metadata or plugin code explicitly using the Ailu allocator.

It does not replace DLL lifecycle validation.

### Acceptance

- [ ] `PluginManager.Initialize` passes.
- [ ] `PluginManager.LoadPlugin` passes.
- [ ] Duplicate plugin ID is rejected.
- [ ] Duplicate candidate module is unloaded.
- [ ] Plugin Tick executes.
- [ ] `PluginManager.UnloadPlugin` passes.
- [ ] Module is no longer loaded after unload.
- [ ] `PluginManager.Shutdown` unloads all.
- [ ] Unload order is reverse load order.
- [ ] Every failure path rolls back.
- [ ] `FreeLibrary` occurs after plugin destruction.
- [ ] All stored plugin function pointers are cleared before `FreeLibrary`.
- [ ] `cmake --build --preset build-debug` passes.

**Do not start M3 until all M2 checks pass.**

---

## M3 — Threaded SmokePlugin and repeated unload stress test

### Goal

Prove the exact lifecycle shape required by future Live Link:

```text
plugin DLL
+ background worker
+ main-thread Tick
+ safe shutdown/join
+ FreeLibrary
```

This is the most important MVP safety milestone.

### SmokePlugin

Suggested structure:

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

The worker must never be detached.

### Test-only exports

SmokePlugin may additionally export:

```cpp
AILU_PLUGIN_EXPORT u64 AiluPluginTestGetTickCount() noexcept;
AILU_PLUGIN_EXPORT u64 AiluPluginTestGetWorkerCount() noexcept;
AILU_PLUGIN_EXPORT bool AiluPluginTestIsWorkerRunning() noexcept;
```

These are **not** official Plugin ABI.

They may only be called while the DLL is loaded.

No test may call a cached test export after `UnloadPlugin()`.

### BadVersionPlugin

Provide:

```cpp
AILU_PLUGIN_EXPORT u32 AiluPluginGetApiVersion() noexcept
{
    return kPluginApiVersion + 1u;
}
```

Expected host behavior:

```text
LoadLibrary success
→ resolve/get API version
→ mismatch
→ immediate rollback
→ FreeLibrary
→ LoadPlugin returns false
```

### PluginSmoke executable

Add a small dedicated executable:

```text
PluginSmoke.exe
```

Do not depend on full Editor/Graphics initialization.

### Required tests

#### TestDynamicLibrary

- Load.
- IsLoaded.
- GetSymbol.
- MissingSymbol.
- Unload.
- module absent after unload.

#### TestPluginLoad

- Initialize manager.
- Load SmokePlugin.
- loaded count == 1.
- ID lookup succeeds.

#### TestPluginTick

- Tick x10.
- test export reports `tick_count > 0`.
- worker reports `worker_count > 0`.

#### TestDuplicatePlugin

- load same DLL twice.
- first succeeds.
- second fails.
- loaded count remains one.
- second candidate module does not leak a reference.

#### TestPluginUnload

Before unload:

```text
GetModuleHandleW(...) != nullptr
```

After unload:

```text
GetModuleHandleW(...) == nullptr
```

#### TestRepeatedLoadUnload

Default:

```cpp
constexpr u32 kRepeatCount = 100u;
```

Loop:

```text
Load
Tick several times
sleep a few ms
verify worker/tick activity
Unload
verify module absent
```

Development-only optional stress:

```text
1000 iterations
```

#### TestBadVersion

- Load returns false.
- loaded count remains zero.
- DLL is not resident after rejection.

#### TestShutdown

- Load SmokePlugin.
- Do not manually unload.
- Call `PluginManager::Shutdown()`.
- loaded count becomes zero.
- module is unloaded.

### Acceptance

- [ ] Threaded plugin loads.
- [ ] Tick executes.
- [ ] Worker executes.
- [ ] Worker is joined before plugin object destruction.
- [ ] Plugin unload succeeds.
- [ ] DLL is absent after unload.
- [ ] repeated load/unload x100 passes.
- [ ] bad API version is rejected.
- [ ] bad-version DLL is absent after rejection.
- [ ] shutdown unloads an active plugin.
- [ ] 0 crashes.
- [ ] 0 hangs.
- [ ] 0 leaked modules.
- [ ] `cmake --build --preset build-debug` passes.

**M0–M3 together define the Plugin MVP.**

No Blender Live Link work may start before M3 acceptance is complete.

---

## M4 — Editor lifecycle integration

### Goal

Host the already-proven PluginManager inside Editor without changing plugin lifetime semantics.

### EditorApp state

Expected additions:

```cpp
Scope<PluginManager> _plugin_manager;
Delegate<>::Handle _plugin_tick_handle = Delegate<>::kInvalidHandle;
```

Use the actual existing Delegate handle type.

### Initialize

Initialize the plugin manager only after base `Application::Initialize(...)` succeeds.

At that point:

- EngineRuntime exists.
- ProjectManager exists.
- main thread identity is established.

Do not auto-load a real plugin yet.

Expected shape:

```cpp
_plugin_manager = MakeScope<PluginManager>();
_plugin_manager->Initialize();
```

### Tick integration

Use the existing Engine-side frame delegate.

The registered lambda must live in Editor code, not plugin DLL code.

Expected shape:

```cpp
_plugin_tick_handle = _before_update += [this]()
{
    if (_plugin_manager != nullptr)
        _plugin_manager->Tick(TimeMgr::s_delta_time);
};
```

Adjust to the actual Delegate API.

### Finalize order

Plugins must unload early.

Required conceptual order:

```text
unsubscribe Editor-owned plugin tick callback
→ PluginManager::Shutdown()
→ _plugin_manager.reset()
→ remaining Editor service shutdown
→ render/application shutdown
→ Application::Finalize()
```

Never unload plugins after allocator/runtime teardown.

### Acceptance

- [ ] Editor starts normally without plugins.
- [ ] PluginManager initializes.
- [ ] Editor closes normally.
- [ ] PluginManager shuts down before base Application teardown.
- [ ] No new error when no plugins are loaded.
- [ ] Manually loading SmokePlugin and closing Editor does not crash/hang.
- [ ] `cmake --build --preset build-debug` passes.

---

## M5 — Manifest discovery, scan, and autoload

### Goal

Add plugin discovery without coupling the mechanism to reflection/asset systems.

### Directory layout

Expected real-world layout:

```text
Plugins/
└── BlenderLiveLink/
    ├── plugin.json
    └── AiluBlenderLiveLink.dll
```

### Manifest v1

Example:

```json
{
    "id": "com.ailu.blender_livelink",
    "name": "Blender Live Link",
    "version": "0.1.0",
    "api_version": 1,
    "type": "editor",
    "library": "AiluBlenderLiveLink.dll",
    "autoload": false
}
```

Supported fields only:

```text
id
name
version
api_version
type
library
autoload
```

Do not add:

```text
dependencies
optional_dependencies
load_before
load_after
platform filters
engine version ranges
```

### Parser

Use a lightweight RapidJSON parser.

Do not require AHT reflection or AssetSystem to parse `plugin.json`.

Plugin discovery is startup infrastructure and should keep dependencies narrow.

### Search roots

Future/default roots:

```text
<AiluRoot>/Plugins/
<ProjectRoot>/Plugins/
```

If duplicate IDs are discovered:

- log error.
- retain the first discovered entry.
- skip the later one.
- do not implement override priority.

### Scan vs Load

Keep discovery and loading separate:

```cpp
ScanPlugins();
LoadPlugin(id);
```

`ScanPlugins()` must not load every DLL.

Only manifests with:

```json
"autoload": true
```

are automatically loaded.

### Acceptance

- [ ] valid manifest discovered.
- [ ] malformed manifest ignored with useful log.
- [ ] unsupported `api_version` rejected.
- [ ] duplicate manifest ID rejected.
- [ ] `autoload: true` loads.
- [ ] `autoload: false` discovers only.
- [ ] scanning itself does not require loading every DLL.
- [ ] `cmake --build --preset build-debug` passes.

---

## M6 — Owner-based registration scope

### Goal

Provide the minimum ownership model required before plugins can safely register Editor callbacks, menus, panels, or other host extensions.

This milestone is a prerequisite for rich Live Link Editor integration, but it must not complicate M0–M5.

### Problem

This is unsafe:

```cpp
some_global_delegate += [this]()
{
    ...
};
```

If unsubscribe is missed:

```text
FreeLibrary
→ later delegate invocation
→ jump into unloaded code
→ crash
```

### Host-owned registration

Future extension APIs should follow:

```cpp
PluginRegistrationHandle RegisterXxx(PluginToken owner, ...);
```

Host records:

```text
owner plugin token
registration type
registration handle
```

Unload flow:

```text
PluginManager
→ ReleaseAllRegistrations(plugin_token)
→ IPlugin::OnUnload()
→ DestroyPlugin
→ FreeLibrary
```

The host should release registrations before destroying the plugin instance.

### Important std::function rule

Do not solve cleanup by storing arbitrary plugin-provided:

```cpp
std::function<void()> cleanup;
```

inside Engine.

A closure's manager/destructor may itself reside in the plugin DLL.

That can create:

```text
FreeLibrary
→ later std::function destruction
→ jump into unloaded DLL code
```

Persistent cleanup records must be implemented using Engine-owned types/functionality.

### Acceptance

- [ ] registration records have explicit plugin owner.
- [ ] host can release all records by owner.
- [ ] release happens before `DestroyPlugin`.
- [ ] no long-lived Engine-owned cleanup closure requires plugin code for destruction.
- [ ] basic registration cleanup test passes.
- [ ] `cmake --build --preset build-debug` passes.

---

# 9. CMake and staging requirements

## Plugin test DLLs

Under:

```text
Test/Plugins/
```

add two SHARED targets:

```text
AiluPluginSmoke
AiluPluginBadVersion
```

They may link:

```cmake
target_link_libraries(AiluPluginSmoke PRIVATE Engine)
```

They must not link Editor.

## PluginSmoke executable

Add:

```cmake
add_executable(PluginSmoke
    "Src/TestPluginSystem.cpp")
```

Reuse:

```cmake
ailu_configure_test_target(PluginSmoke)
```

Dependencies:

```cmake
add_dependencies(PluginSmoke
    AiluPluginSmoke
    AiluPluginBadVersion)
```

## Runtime staging

Expected output:

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

# 10. Mandatory safety invariants

These are not optional design suggestions.

## S1 — Plugin object never outlives DLL

Immediately before `FreeLibrary`:

```text
_instance == nullptr
```

must be true.

Debug assert is recommended.

## S2 — Engine never directly destroys concrete plugin object

Engine stores only:

```cpp
IPlugin*
```

and calls the plugin-provided destroy export.

Do not store:

```cpp
Ref<PluginConcreteType>
std::unique_ptr<PluginConcreteType>
```

in Engine.

## S3 — Descriptor pointers never survive unload

Copy descriptor strings and metadata into Engine-owned `PluginInfo`.

## S4 — Function pointers never survive unload

Before `FreeLibrary`:

```cpp
_get_descriptor = nullptr;
_create = nullptr;
_destroy = nullptr;
```

and all other plugin export pointers must also be cleared.

## S5 — Plugin threads stop before OnUnload returns

Required future Live Link pattern:

```text
stop accepting work
→ close socket
→ signal worker
→ join worker
→ drain/clear queue
→ return from OnUnload
```

Never detach plugin worker threads.

## S6 — Plugin-owned JobSystem jobs are the plugin's responsibility

There is no plugin-aware JobSystem ownership in v1.

If plugin code dispatches a job whose callable lives in the DLL, the plugin must wait for that job before `OnUnload()` returns.

Otherwise:

```text
FreeLibrary
→ JobSystem worker executes plugin callable
→ crash
```

## S7 — No dynamic reflection types in v1

No plugin-defined reflection registration.

## S8 — No plugin-owned LayerStack object in v1

Do not push plugin concrete `Layer` objects into a host-owned long-lived `LayerStack`.

## S9 — No unload while manager Tick is iterating

`UnloadPlugin()` returns false during Tick.

## S10 — Every load failure rolls back

No load step may leak:

- library reference.
- instance.
- function pointer.
- manager entry.
- registration.
- thread.

---

# 11. Logging requirements

Use prefix:

```text
[Plugin]
```

Successful lifecycle:

```text
[Plugin] Loaded com.ailu.test.smoke (0.1.0)
[Plugin] Unloaded com.ailu.test.smoke
```

Typical failures:

```text
[Plugin] LoadLibrary failed: ...
[Plugin] Missing export AiluPluginCreate: ...
[Plugin] API version mismatch: host=1 plugin=2
[Plugin] Duplicate plugin id: ...
[Plugin] OnLoad failed: ...
```

Do not log every Tick.

---

# 12. Current milestone

Current Milestone:

```text
M0 — DynamicLibrary platform wrapper
```

Immediate next steps:

1. Inspect existing platform abstractions and logging conventions.
2. Add `DynamicLibrary.h/.cpp`.
3. Add the smallest DLL/module test needed for Load/GetSymbol/Unload.
4. Verify `GetModuleHandleW(...) == nullptr` after unload.
5. Run `cmake --build --preset build-debug`.
6. Update `Progress`, `Decision Log`, `Surprises & Discoveries`, and `Validation`.
7. Only then move to M1.

---

# 13. Progress

## MVP

- [ ] M0 — DynamicLibrary platform wrapper.
- [ ] M1 — Stable Plugin ABI v1.
- [ ] M2 — PluginModule and PluginManager.
- [ ] M3 — Threaded SmokePlugin and repeated unload stress test.

## Integration

- [ ] M4 — Editor lifecycle integration.
- [ ] M5 — Manifest discovery/scan/autoload.
- [ ] M6 — Owner-based registration scope.

## Final MVP checklist

- [ ] DynamicLibrary abstraction complete.
- [ ] Windows Load/GetSymbol/Unload verified.
- [ ] Plugin ABI v1 fixed.
- [ ] API version checked before descriptor access.
- [ ] PluginModule encapsulates full lifecycle.
- [ ] PluginManager supports Load/Unload/Tick/Shutdown.
- [ ] Duplicate ID rejected safely.
- [ ] Plugin object destroyed by DLL export.
- [ ] `FreeLibrary` always after Destroy.
- [ ] Threaded SmokePlugin implemented.
- [ ] Worker thread joined in `OnUnload`.
- [ ] Repeated Load/Unload x100 passes.
- [ ] BadVersionPlugin rejected with no resident module.
- [ ] Shutdown unloads all plugins.
- [ ] Editor starts/closes normally after integration.
- [ ] Plugin manager dies before Application/Allocator shutdown.
- [ ] No Plugin → Editor private implementation dependency.
- [ ] No dynamic reflection unload problem introduced.
- [ ] No long-lived plugin lambda/function pointer survives unload.

---

# 14. Decision Log

## 2026-09-30 — Plugin host belongs in Engine

Decision:

`PluginManager`, `PluginModule`, Plugin ABI, and `DynamicLibrary` belong in Engine.

Rationale:

The plugin mechanism is runtime infrastructure. Editor is one host/consumer and Blender Live Link will be an Editor plugin built on top.

Rejected alternative:

Implement the first plugin system entirely inside Editor.

Reason rejected:

That would tie the lifecycle/ABI layer to Editor private implementation and make later runtime/tool reuse harder.

---

## 2026-09-30 — Fixed C export surface

Decision:

The DLL discovery boundary exposes only fixed `extern "C"` exports.

Rationale:

Avoid C++ symbol name mangling and keep initial ABI discovery predictable.

---

## 2026-09-30 — API version is checked first

Decision:

`AiluPluginGetApiVersion` is resolved/called before interpreting `PluginDescriptor` or `IPlugin`.

Rationale:

A mismatched host must not read an incompatible C++ layout.

---

## 2026-09-30 — Plugin allocates and destroys plugin instance

Decision:

The concrete plugin object is created and destroyed inside its own DLL.

Rationale:

Avoid cross-module ownership ambiguity and guarantee destruction code is still resident.

---

## 2026-09-30 — No generic callback registration before owner scope exists

Decision:

The MVP only receives frame updates through `PluginManager → IPlugin::Tick`.

Rationale:

Arbitrary plugin lambdas escaping into host delegates are the largest early `FreeLibrary` hazard.

---

## 2026-09-30 — Plugin Manager is owner-thread-only in v1

Decision:

Load/Unload/Tick/Shutdown are restricted to one owner thread.

Rationale:

Cross-thread plugin lifecycle would add complexity before the base unload invariant is proven.

---

## 2026-09-30 — No plugin-aware JobSystem in v1

Decision:

Plugins are responsible for waiting for all plugin-code jobs before `OnUnload()` returns.

Rationale:

Plugin job ownership is useful later but not required for the first Live Link path, which can own a dedicated socket worker.

---

# 15. Surprises & Discoveries

Use this section during implementation.

Record only facts that affect later work.

Examples:

```text
- Existing Delegate handle type differs from the task package example.
- LoadLibrary increments an existing module refcount because another target also links/loads the same DLL.
- Test target staging path differs from the assumed layout.
- An existing global singleton retains a plugin callback and blocks unload.
```

Do not paste complete compiler logs.

Current discoveries:

- None yet.

---

# 16. Validation

## Build

Required command:

```text
cmake --build --preset build-debug
```

Current result:

```text
Not run for this ExecPlan yet.
```

## M0 validation

- [ ] DynamicLibrary Load.
- [ ] GetSymbol.
- [ ] MissingSymbol.
- [ ] Unload.
- [ ] module absent after unload.

## M1 validation

- [ ] fixed exports.
- [ ] version check first.
- [ ] descriptor copied.
- [ ] create/destroy in plugin module.

## M2 validation

- [ ] manager lifecycle.
- [ ] rollback.
- [ ] duplicate ID.
- [ ] reverse shutdown.
- [ ] function pointer cleanup.

## M3 validation

Target output:

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

## M4 validation

- [ ] Editor normal startup.
- [ ] Editor normal shutdown.
- [ ] SmokePlugin loaded during Editor shutdown does not hang/crash.

## M5 validation

- [ ] scan valid manifest.
- [ ] malformed ignored.
- [ ] duplicate rejected.
- [ ] API mismatch rejected.
- [ ] autoload true loads.
- [ ] autoload false only discovers.

## M6 validation

- [ ] owner registrations released before Destroy.
- [ ] no plugin-owned callable survives unload.

---

# 17. Known follow-ups

Do not implement these inside this ExecPlan unless the plan is explicitly revised.

## DLL shadow copy / true hot reload

First-version workflow:

```text
Unload plugin
→ build DLL
→ load plugin
```

Future shadow-copy model may be:

```text
original DLL
→ copy
→ Cache/Plugins/<id>/<timestamp>.dll
→ load copied DLL
```

## Plugin dependencies

Add only after multiple real plugins require dependency relationships.

## Editor UI extension API

Future possible service:

```text
IEditorPluginService
RegisterMenu
RegisterPanel
RegisterDockWindow
```

Do this after M6 owner-based registration exists.

## Plugin-owned jobs

Potential future design:

```text
PluginToken
PluginJobGroup
WaitAllPluginJobs(token)
```

## Player runtime plugin autoload

Explicitly out of scope here.

## Dynamic reflected plugin types

Requires an owner-aware registration/unregistration design and is a separate architecture task.

---

# 18. Blender Live Link handoff

After M0–M3 are fully accepted, Blender Live Link may begin.

Expected future plugin package:

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

# 19. Outcomes

Fill this section only when the task is complete.

## Final implementation

- TBD.

## Architecture changes

- TBD.

## Validation results

- TBD.

## Performance impact

- TBD.

## Known remaining issues

- TBD.

## Architecture docs updated

- [ ] `Doc/architecture/runtime-lifecycle.md`
- [ ] `Doc/architecture/threading.md`
- [ ] `Doc/architecture/editor-runtime-boundary.md`
- [ ] Add `Doc/architecture/plugin-system.md` once the implementation is stable.

---

# 20. Resume Protocol

When resuming this task after context compaction or in a new thread:

1. Read `AGENTS.md`.
2. Read `Doc/architecture/index.md`.
3. Read only the architecture docs referenced in section 5.
4. Read this ExecPlan.
5. Inspect `Current Milestone`, `Progress`, `Decision Log`, `Surprises & Discoveries`, and `Validation`.
6. Inspect current git diff and only the files relevant to the current milestone.
7. Continue from the first unchecked acceptance item in the current milestone.
8. Update this ExecPlan before advancing milestones.

Do not reconstruct task state from old conversation history.

---

# 21. Rules for the implementation agent

1. Prefer small changes. Do not refactor Application, JobSystem, rendering, Scene, or Asset workflows for this task.
2. PluginManager must work in a standalone test executable without Editor/Graphics initialization.
3. Never depend on absolute developer paths.
4. Every failed load step must rollback all already-created state.
5. Pass the current milestone before starting the next.
6. If a milestone requires a broader public API than described here, record the reason in `Decision Log` before adding it.
7. `FreeLibrary` lifecycle safety has higher priority than feature count.
8. Prefer exposing less capability in v1 over exposing callbacks/resources that cannot be reliably reclaimed.
9. Do not start Blender Live Link until the Plugin MVP acceptance criteria are complete.
10. After every milestone, update this ExecPlan before continuing.
