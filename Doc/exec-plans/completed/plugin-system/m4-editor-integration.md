# M4 — Editor Lifecycle Integration

## Goal

Host the already-proven PluginManager inside Editor without changing the lifecycle semantics proven by M0–M3.

Do not add real plugin UI in this milestone.

---

## Relevant files

Expected modifications:

```text
Editor/Inc/EditorApp.h
Editor/Src/EditorApp.cpp
```

---

## Architecture constraints

Read:

```text
README.md
progress.md
Doc/architecture/runtime-lifecycle.md
Doc/architecture/editor-runtime-boundary.md
```

Engine plugin infrastructure must not depend on Editor concrete implementation.

Editor may own/use `PluginManager`.

---

## EditorApp state

Expected additions:

```cpp
Scope<PluginManager> _plugin_manager;
Delegate<>::Handle _plugin_tick_handle = Delegate<>::kInvalidHandle;
```

Use the actual repository Delegate handle type rather than blindly copying this spelling.

---

## Initialize

Initialize only after base:

```text
Application::Initialize(...)
```

succeeds.

At that point:

- EngineRuntime exists.
- ProjectManager exists.
- main thread identity is established.

Expected:

```cpp
_plugin_manager = MakeScope<PluginManager>();
_plugin_manager->Initialize();
```

Do not auto-load a real production plugin yet.

---

## Tick integration

Use the existing Engine/Application frame delegate.

The lambda must be compiled into Editor, not plugin DLL code.

Conceptually:

```cpp
_plugin_tick_handle = _before_update += [this]()
{
    if (_plugin_manager != nullptr)
        _plugin_manager->Tick(TimeMgr::s_delta_time);
};
```

Adjust to actual Delegate API.

This avoids a plugin-owned callable escaping into the host delegate.

---

## Finalize order

Plugins unload early.

Required order:

```text
unsubscribe Editor-owned plugin tick callback
→ PluginManager::Shutdown()
→ _plugin_manager.reset()
→ remaining Editor service shutdown
→ render/application shutdown
→ Application::Finalize()
```

Forbidden:

```text
Application::Finalize
→ allocator/runtime teardown
→ PluginManager::Shutdown
```

---

## Validation

### No plugin loaded

- Editor starts normally.
- PluginManager initializes.
- Editor closes normally.
- no extra errors.

### SmokePlugin loaded

- manually load SmokePlugin.
- close Editor.
- plugin worker joins.
- module unloads.
- no crash.
- no hang.

---

## Acceptance

- [x] Editor starts normally without plugins.
- [x] PluginManager initializes.
- [x] Editor closes normally.
- [x] PluginManager shuts down before base Application teardown.
- [x] Tick callback is unsubscribed.
- [x] no errors when no plugin is loaded.
- [x] loaded SmokePlugin does not crash/hang during Editor exit.
- [x] Engine plugin layer still has no dependency on Editor implementation.
- [x] `cmake --build --preset build-debug` passes.

---

## Milestone completion procedure

After acceptance:

1. Mark M4 complete in `progress.md`.
2. Set current milestone to M5.
3. Continue with `m5-plugin-manifest.md`.

## Validation — 2026-09-30

Final full build-debug passed (exit 0). RunPluginEditorSmoke.ps1 ran empty and threaded cases against final Editor.exe using the existing project descriptor and normal WM_CLOSE; both exit 0. Logs verify host initialization, early callback unsubscription/Shutdown, worker join and Destroy-before-module-release; EditorMain checks activity before teardown and module absence after Finalize. State files are restored byte-for-byte in finally. Evidence and reproduction are in validation-m2-m4.md.

The no-errors item means no added Plugin errors: both cases retain the same 55 existing non-plugin ERROR lines (26 unique missing-resource/etc errors) and allocator warnings. Global Editor resource/leak cleanup is not part of M4. The original malformed path attempt was not acceptance evidence; fixed quoting and preflight validation were followed by successful final runs.
