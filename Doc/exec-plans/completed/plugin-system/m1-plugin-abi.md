# M1 — Stable Plugin ABI v1

## Goal

Define the smallest fixed ABI required to discover, create, use, and destroy a plugin safely.

Do not build PluginManager behavior beyond what is needed to test the ABI.

---

## Relevant files

New:

```text
Engine/Inc/Framework/Plugin/PluginApi.h
Test/Plugins/SmokePlugin/...
```

---

## Architecture constraints

Read:

```text
README.md
progress.md
Doc/architecture/runtime-lifecycle.md
Doc/architecture/reflection-codegen.md
```

Important global rules:

- API version must be checked before reading complex ABI structures.
- Concrete plugin allocation/destruction occurs in the plugin DLL.
- No dynamic reflected types.
- Plugin must not depend on Editor private implementation.

---

## API version

```cpp
namespace Ailu
{
    inline constexpr u32 kPluginApiVersion = 1u;
}
```

Increment only for ABI-breaking changes such as:

- `IPlugin` virtual layout/order changes.
- incompatible `PluginContext` layout.
- incompatible `PluginDescriptor` layout.
- calling convention change.

---

## Plugin type

```cpp
enum class EPluginType : u8
{
    kRuntime,
    kEditor,
};
```

Rules:

- Editor host may load both.
- Player auto-loading is not part of this plan.
- Future Blender Live Link will be `kEditor`.

---

## PluginDescriptor

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

Descriptor strings are static plugin-DLL storage.

The host must copy them before unload.

Never persist the raw descriptor pointer.

Example:

```cpp
static const PluginDescriptor s_descriptor{
    "com.ailu.test.smoke",
    "Ailu Smoke Plugin",
    "0.1.0",
    EPluginType::kEditor,
};
```

---

## IPluginHost

Keep v1 deliberately narrow:

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

Do not expose complete:

```text
SceneMgr
ResourceMgr
Renderer
EditorApp
```

---

## PluginContext

```cpp
struct PluginContext
{
    u32 _api_version = kPluginApiVersion;
    IPluginHost* _host = nullptr;
};
```

---

## IPlugin

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

Plugin code catches its own internal exceptions.

---

## Fixed C exports

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

Aliases:

```cpp
using PluginGetApiVersionFn = u32 (*)() noexcept;
using PluginGetDescriptorFn = const PluginDescriptor* (*)() noexcept;
using PluginCreateFn = IPlugin* (*)() noexcept;
using PluginDestroyFn = void (*)(IPlugin*) noexcept;
```

---

## Required discovery order

Resolve and call:

```text
AiluPluginGetApiVersion
```

first.

Only when:

```text
plugin_api_version == kPluginApiVersion
```

may the host call:

```text
AiluPluginGetDescriptor
AiluPluginCreate
```

This prevents interpreting incompatible ABI layouts.

---

## Allocation rule

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

---

## Acceptance

- [x] API version export exists.
- [x] Descriptor export exists.
- [x] Create export exists.
- [x] Destroy export exists.
- [x] Descriptor id/name/version valid.
- [x] Create returns non-null.
- [x] Destroy succeeds.
- [x] API version is checked before descriptor access.
- [x] Plugin object is not directly deleted by Engine.
- [x] Plugin DLL does not include Editor private implementation.
- [x] No reflected plugin types introduced.
- [x] `cmake --build --preset build-debug` passes.

---

## Milestone completion procedure

After acceptance:

1. Mark M1 complete in `progress.md`.
2. Record ABI-related decisions.
3. Set current milestone to M2.
4. Continue with `m2-plugin-manager.md`.

## Validation result — 2026-09-30

PluginSmoke.exe and full cmake --build --preset build-debug passed (exit 0). The runner checks API version before resolving/accessing descriptor/Create/Destroy, validates descriptor values, creates and destroys inside DLL exports, tests OnLoad/Tick/OnUnload and verifies module absence plus copied strings after unload. Source review confirms no host delete, Editor private include or reflected plugin type. Explicit __cdecl and compatible C++ ABI/runtime configuration are required in v1. M2 is pending and was not implemented in this request.
