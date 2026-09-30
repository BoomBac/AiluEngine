# M2 — PluginModule and PluginManager

## Goal

Centralize plugin state and lifecycle so every load/unload failure path can rollback safely.

This milestone is the core ownership implementation.

---

## Relevant files

New:

```text
Engine/Inc/Framework/Plugin/PluginManager.h
Engine/Src/Framework/Plugin/PluginManager.cpp
```

Also consumes:

```text
Engine/Inc/Framework/Platform/DynamicLibrary.h
Engine/Inc/Framework/Plugin/PluginApi.h
```

Optional small change:

```text
Engine/Inc/Framework/Common/Allocator.hpp
```

for `EMemoryTag::kPlugin`.

---

## PluginInfo

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

Never persist:

```cpp
const PluginDescriptor* _descriptor;
```

---

## State machine

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

Load:

```text
kUnloaded
→ kLoading
→ DynamicLibrary::Load
→ resolve GetApiVersion only
→ API version check
→ resolve remaining exports
→ copy descriptor
→ CreatePlugin
→ OnLoad
→ kActive
```

Any failure:

```text
rollback all completed steps
→ no instance
→ no saved function pointers
→ no loaded module
→ no manager entry
```

---

## PluginModule

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

Keep these operations centralized in `PluginModule`:

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

---

## Unload order

Required conceptual implementation:

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

The implementation may add diagnostics/asserts, but not change lifetime ordering.

`OnUnload()` is always called while the DLL is still loaded.

---

## PluginManager

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

---

## Owner thread

Record during initialization:

```cpp
_owner_thread = std::this_thread::get_id();
```

Require owner thread for:

```text
LoadPlugin
UnloadPlugin
Tick
Shutdown
```

Do not implement cross-thread load/unload in v1.

---

## Tick/unload rule

While manager iterates plugin Tick:

```text
_is_ticking = true
```

If `UnloadPlugin()` is called:

- return `false`.
- log warning.
- do not mutate `_plugins`.

Do not add a generic deferred lifecycle command queue in this milestone.

---

## Duplicate ID

A second DLL reporting an existing plugin ID must be rejected.

Log:

```text
duplicate plugin id
existing library path
new library path
```

The rejected candidate DLL must be fully unloaded.

---

## Shutdown order

Destructor:

```cpp
PluginManager::~PluginManager()
{
    Shutdown();
}
```

Shutdown:

```text
stop Tick
→ unload plugins in reverse load order
→ clear manager containers
```

Use `_load_order`; `HashMap` does not guarantee unload order.

---

## Memory tag

Optional if localized:

```cpp
kPlugin
```

Use for Engine-side plugin metadata or plugin code explicitly allocating through Ailu Allocator.

It does not replace lifetime checks.

---

## Acceptance

- [x] Initialize passes.
- [x] LoadPlugin passes.
- [x] Duplicate plugin ID is rejected.
- [x] Duplicate candidate DLL is unloaded.
- [x] Tick executes.
- [x] UnloadPlugin passes.
- [x] DLL is absent after unload.
- [x] Shutdown unloads all plugins.
- [x] Shutdown unload order is reverse load order.
- [x] Every load failure rolls back.
- [x] Plugin object is destroyed before `FreeLibrary`.
- [x] All saved plugin function pointers are cleared before `FreeLibrary`.
- [x] Owner-thread checks exist where appropriate.
- [x] Unload during Tick is rejected.
- [x] `cmake --build --preset build-debug` passes.

---

## Milestone completion procedure

After acceptance:

1. Mark M2 complete in `progress.md`.
2. Record any lifecycle discoveries.
3. Set current milestone to M3.
4. Continue with `m3-plugin-lifecycle-tests.md`.

## Validation — 2026-09-30

PluginManagerSmoke and full build-debug passed (exit 0). Dedicated fixtures cover all load failure stages, duplicate same/different DLL, Tick reentrancy, affinity, reverse unload and destruction-before-release. The load-order vector is the owning container (linear lookup for small v1 counts). Optional memory tag was not added. Missing-export diagnostics use wide-string conversion to match existing LogMgr overloads.
