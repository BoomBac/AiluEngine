# M6 — Owner-Based Registration Scope

## Goal

Provide the minimum host ownership model required before plugins can safely register callbacks, Editor menus, panels, or similar extensions.

This is the safety foundation for richer Editor plugins such as Blender Live Link.

---

## Problem

Unsafe pattern:

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
→ jump into unloaded plugin code
→ crash
```

The host must be able to reclaim registrations by plugin ownership even when plugin code is imperfect.

---

## Plugin owner token

Introduce an Engine-owned identity concept such as:

```text
PluginToken
```

The exact representation should be simple and stable.

Do not expose concrete PluginModule pointers as public ownership identity if a smaller token works.

---

## Host-owned registration API

Future extension APIs should follow:

```cpp
PluginRegistrationHandle RegisterXxx(PluginToken owner, ...);
```

Host records:

```text
owner plugin token
registration type
registration handle
Engine-owned cleanup metadata
```

---

## Unload ordering

Required:

```text
PluginManager
→ stop plugin Tick entry
→ ReleaseAllRegistrations(plugin_token)
→ IPlugin::OnUnload()
→ DestroyPlugin
→ clear plugin exports
→ FreeLibrary
```

Host registration cleanup occurs before plugin object destruction.

---

## Avoid plugin-owned cleanup closures

Do not solve ownership by storing arbitrary:

```cpp
std::function<void()> cleanup;
```

provided by plugin code.

The `std::function` closure manager/destructor may live inside the plugin DLL.

Danger:

```text
FreeLibrary
→ Engine later destroys std::function
→ manager/destructor jumps into unloaded DLL
→ crash
```

Persistent cleanup records should use Engine-owned types and Engine-owned cleanup logic.

---

## Initial registration scope

Do not build a huge Editor extension API.

Implement only enough generic ownership/release infrastructure to prove:

```text
register with owner
→ query/track
→ release all by owner
→ unload plugin safely
```

Specific APIs such as:

```text
RegisterMenu
RegisterPanel
RegisterDockWindow
```

can be separate follow-up work after the ownership layer is proven.

---

## Validation

Create a minimal host-owned registration test:

```text
load plugin
→ create registration owned by plugin token
→ verify registration active
→ unload plugin
→ host releases registration
→ destroy plugin
→ FreeLibrary
→ verify no registration remains
```

No persistent cleanup callable may require plugin code after release begins.

---

## Acceptance

- [x] registration records have explicit plugin owner.
- [x] host can release all records by owner.
- [x] release occurs before `DestroyPlugin`.
- [x] no long-lived Engine-owned cleanup closure requires plugin code for destruction.
- [x] unload leaves no registration owned by plugin.
- [x] basic registration cleanup test passes.
- [x] M0–M3 lifecycle invariants remain unchanged.
- [x] `cmake --build --preset build-debug` passes.

---

## Milestone completion procedure

After acceptance:

1. Mark M6 complete in `progress.md`.
2. Fill final Outcomes in `progress.md`.
3. Add/update `Doc/architecture/plugin-system.md`.
4. Update affected runtime/threading/editor architecture docs.
5. Move the plan directory to `Doc/exec-plans/completed/plugin-system/`.
