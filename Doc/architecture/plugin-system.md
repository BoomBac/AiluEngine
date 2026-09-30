# Plugin foundation

## Current implementation

M0–M6 provide `DynamicLibrary`, Plugin ABI v1, internal `PluginModule`, `PluginManager`, threaded lifecycle
tests, Editor lifecycle integration, manifest discovery/autoload and an Engine-owned registration scope.
The initial scope stores host records; menu, panel, dock and callback service APIs are separate follow-up work.

## Dynamic library ownership

`Engine/Inc/Framework/Platform/DynamicLibrary.h` owns one Windows loader reference without exposing `HMODULE`.
The object is non-copyable and non-movable. `Load` rejects replacing a loaded handle and records an absolute,
lexically normalized path. Missing files report the path and Windows error. Symbol lookup returns null for
unloaded libraries and missing/invalid names. `Unload` is idempotent, and the destructor calls it.
If `FreeLibrary` fails, ownership/path are retained for diagnosis and a possible retry.

This wrapper owns only the library reference. Its caller must destroy DLL instances and release all DLL
function pointers, descriptors and callbacks before unload. Operations on the same wrapper require caller
serialization; it has no internal synchronization. Logging uses `LogMgr`, which must already be initialized
and remain alive for failure reporting.

## ABI v1

`Engine/Inc/Framework/Plugin/PluginApi.h` defines four fixed C exports with explicit Windows `__cdecl`:
`AiluPluginGetApiVersion`, `AiluPluginGetDescriptor`, `AiluPluginCreate`, `AiluPluginDestroy`.
The version export must be resolved and checked before descriptor or object access.
Plugin producers define `AILU_BUILD_PLUGIN` to enable exported declarations; hosts use only the function
pointer aliases and dynamic discovery, so their modules receive no plugin export directives.

The descriptor uses DLL-owned static C strings. The host copies id/name/version before unload and does not
retain the raw descriptor. `PluginContext` provides the API version and a borrowed `IPluginHost` pointer.
The host offers only logging and a main-thread query. It must outlive plugin use, including `OnUnload`.
The plugin interface offers `OnLoad`, `OnUnload` and `Tick`; no exception may cross the DLL boundary.

Concrete objects are allocated and destroyed inside the plugin DLL through Create/Destroy. The host never
directly deletes them. Required order: stop Tick/new registrations, release host registrations, OnUnload, Destroy,
null instance/descriptors/export pointers, FreeLibrary.
There are no reflected plugin types or dependencies on Editor private implementation.

The exported names are C discovery symbols, but descriptors and virtual interfaces still use a C++ ABI.
v1 requires compatible compiler/toolchain, architecture, runtime configuration and structure layout.
Increment `kPluginApiVersion` for incompatible layout, virtual-interface or calling-convention changes.

## Validation

`DynamicLibrarySmoke` uses a dedicated probe DLL to check loader ownership, symbol lookup, errors, explicit
and destructor unload, and `GetModuleHandleW` absence. `PluginSmoke` loads the minimal `AiluPluginSmoke` DLL
from `Plugins/SmokePlugin` relative to its executable, checks ABI discovery/lifecycle and host-owned string
copies after unload. These runners initialize logging only; they do not initialize Editor or Graphics.
The smoke plugin owns an atomic stop flag and a worker thread. OnUnload stops and joins the worker before
Destroy; a still-joinable worker in the destructor is a contract violation. Test-only exports report worker/tick
activity and are not part of ABI v1. The standalone suite runs 100 load/Tick/worker-check/unload cycles and
checks module absence after every unload. The bad-version fixture exports version only and is rejected first.

## PluginManager ownership and threading

`PluginManager` owns a small ordered vector of internal `PluginModule` objects and a narrow host implementation.
Linear ID lookup keeps v1 simple; the owning vector is also the explicit load order, reversed during shutdown.
Modules first load, validate version/exports and copy the descriptor; duplicate IDs are rejected before Create
or OnLoad. The manager reserves its entry storage before activation. Every failed candidate is RAII-cleaned.
If OnLoad has started but returns false, cleanup still calls OnUnload before Destroy and FreeLibrary.

Create/OnLoad/Tick/OnUnload/Destroy run only on the manager's owner thread. Construct, initialize, query, use
and destroy the manager on that thread. Wrong-thread operations are rejected before reading mutable state.
Lifecycle operations and recursive Tick are rejected while the manager is already executing a plugin call;
there is no deferred lifecycle queue. Destruction on another thread or during a plugin call is a contract violation.
The host and logging runtime must outlive all plugin callbacks and destruction.

## Editor integration

EditorApp owns a PluginManager initialized after successful Application initialization. An Editor-compiled
before-update delegate calls Tick with `TimeMgr::s_delta_time`. Finalize unsubscribes that delegate, shuts
down and destroys the manager before saving Editor state, stopping other Editor services and finalizing Application.
The Engine layer has no dependency on Editor implementation. Editor scans `<AiluRoot>/Plugins` followed by
`<ProjectRoot>/Plugins`, then explicitly loads only manifests marked `autoload: true`. Player autoload is absent.

Editor supports explicit lifecycle-validation options after the project argument: `--plugin-smoke <DLL>` and
`--exit-after-frames <positive count>`. The latter posts WM_CLOSE and uses the normal application exit path.
The former manually loads a test DLL and checks activity before shutdown and module absence afterward.
`--plugin-smoke-autoload <DLL>` checks the same fixture after startup autoload, without manually loading it.
Both test modes create a host record and verify it is active before shutdown; scope release precedes OnUnload.
`Test/RunPluginEditorSmoke.ps1` runs empty and threaded cases with a timeout, preserves config/layout/session
bytes in finally, and retains independent logs. Existing non-plugin scene/resource errors and allocator warnings
are not suppressed or interpreted as plugin acceptance failures; compare the two logs when assessing regressions.

## Manifest discovery

`PluginManifest.h/.cpp` uses RapidJSON directly without reflected types or ObjectRegister/asset initialization.
`ScanPlugins(roots)` accepts ordered roots, scans only immediate child directories for `plugin.json`, sorts
candidates within each root and keeps the first duplicate ID. Missing roots are normal; malformed/unreadable
candidates produce path/reason diagnostics and do not prevent other entries from being discovered.
Scanning does not load DLLs. `DiscoveredPlugins()` returns an Engine-owned snapshot.

The strict v1 schema has exactly seven fields: id, name, version, api_version, type, library and autoload.
Strings must be nonempty without embedded NUL; api_version must be a compatible uint32; type is runtime/editor;
autoload is a boolean. Library paths must be relative and remain within the manifest directory, including
symlink resolution. There is no dependency graph, override priority or engine-version resolution.

`LoadPlugin(String id)` loads a discovered entry. `LoadAutoloadPlugins()` tries all marked entries, preserving
successful loads while reporting failures; repeated calls skip already loaded IDs. Manual path loading is
still available. DLL API version remains authoritative; id/name/version/type must match the manifest before
Create/OnLoad. All failures retain the original rollback and unload ordering.

## Owner registration scope

`PluginRegistration.h` defines Engine-only value tokens, handles and records; ABI v1 is unchanged. Tokens
are issued by an atomic identity counter, unique process-wide for each load attempt and never recycled.
This counter owns no subsystem or registry. Handles combine owner token with a non-reused per-module counter;
stale/foreign owners cannot register or affect another owner after reload, reinitialize or across managers.

PluginManager exposes GetPluginToken, RegisterHostRecord, ReleaseRegistration, ReleaseAllRegistrations,
HasRegistration and Registrations (metadata snapshot). Registration operations require the owner thread and
an active module outside a manager call. The initial typed scope stores only Engine-owned strings and POD
metadata. There is no arbitrary cleanup function, std::function, plugin-owned closure manager or opaque
plugin destructor in cleanup records. Future service implementations must add typed Engine-owned detach
logic before exposing callback/menu/panel registration; this record scope does not reclaim arbitrary delegates.

Each PluginModule owns its registration scope. Unload first enters the unloading state and closes/releases
the scope, then calls OnUnload/Destroy and clears exports before FreeLibrary. This also runs on failed-load
RAII cleanup. Owner lookups no longer yield an active token during teardown. Tests query the registry inside
OnUnload and Destroy log observations, proving records are absent before plugin teardown, not only afterward.

## Current acceptance

Standalone M0–M6 runners pass, including M3 threaded x100 and M5/M6 rejection/order/isolation tests. The live
Editor runner's `-TestManifest` option additionally creates a temporary project manifest, checks discover-only
and autoload cases and removes only its verified fixture path in finally. See the completed plugin-system
execution plan for exact evidence, commands and existing non-plugin Editor error/warning limitations.
