# Plugin System Progress

> Status: Complete  
> Current Milestone: M0–M6 — accepted

---

## Current work

Current milestone:

```text
M0–M6 — accepted
```

M0–M6 implemented and accepted; see validation-m5-m6.md.

---

## Milestone progress

### MVP

- [x] M0 — DynamicLibrary platform wrapper.
- [x] M1 — Stable Plugin ABI v1.
- [x] M2 — PluginModule and PluginManager.
- [x] M3 — Threaded SmokePlugin and repeated unload stress test.

### Integration

- [x] M4 — Editor lifecycle integration.
- [x] M5 — Manifest discovery/scan/autoload.
- [ ] M0–M6 — accepted.

---

## Current milestone checklist

M2, M3 and M4 acceptance is recorded in their milestone files and validation-m2-m4.md.
M5/M6 acceptance is complete, including standalone tests and four live Editor cases.

---

## Latest validation

### Build

```text
2026-09-30: final full cmake --build --preset build-debug passed (exit 0), including Engine, Editor and plugin runners.
```

Required command:

```text
cmake --build --preset build-debug
```

### Runtime / tests

```text
2026-09-30: M2 PluginManagerSmoke passed; PluginSmoke 10/10 with threaded x100 passed. Live Editor empty/threaded normal exits passed; see validation-m2-m4.md. All successful final runs exited 0.
```

---

## Decision Log

### 2026-09-30 — Plugin host belongs in Engine

Decision:

`PluginManager`, `PluginModule`, Plugin ABI, and `DynamicLibrary` belong in Engine.

Rationale:

The plugin mechanism is runtime infrastructure. Editor is a host/consumer, and Blender Live Link will later be an Editor plugin built on top.

### 2026-09-30 — Fixed C export surface

Decision:

The DLL discovery boundary exposes only fixed `extern "C"` exports.

Rationale:

Avoid C++ symbol name mangling and keep initial ABI discovery predictable.

### 2026-09-30 — API version checked first

Decision:

`AiluPluginGetApiVersion` is resolved/called before interpreting `PluginDescriptor` or `IPlugin`.

Rationale:

A mismatched host must not read incompatible C++ layouts.

### 2026-09-30 — Plugin allocates and destroys its instance

Decision:

Concrete plugin objects are created and destroyed inside the plugin DLL.

Rationale:

Avoid cross-module ownership ambiguity and guarantee destruction code is still resident.

### 2026-09-30 — No generic callback registration before owner scope exists

Decision:

Before M6, frame update enters plugin code only through `PluginManager → IPlugin::Tick`.

Rationale:

Arbitrary plugin lambdas escaping into host delegates are a major `FreeLibrary` hazard.

### 2026-09-30 — PluginManager is owner-thread-only in v1

Decision:

Load/Unload/Tick/Shutdown are restricted to one owner thread.

Rationale:

Cross-thread lifecycle control adds unnecessary complexity before unload safety is proven.

### 2026-09-30 — No plugin-aware JobSystem in v1

Decision:

Plugins are responsible for waiting for plugin-code jobs before `OnUnload()` returns.

Rationale:

Plugin job ownership can be added later and is not required for the first Live Link implementation.

---

## Surprises & Discoveries

Record only facts that affect later implementation.

Current:

- Log headers require UNICODE/_UNICODE on standalone hosts. Dedicated test targets avoid the existing Editor-oriented test helper.
- Configuration-specific output variables override generic DLL output properties; plugin staging sets all configuration properties explicitly.

Examples of useful entries:

```text
- Existing Delegate handle type differs from the plan example.
- A module stays resident because another target also holds a DLL reference.
- Test staging path differs from the assumed layout.
- A host registry retains a plugin callback and prevents safe unload.
```

Do not paste complete compiler logs.

---

## Blockers

None.

---

## Final MVP checklist

- [x] DynamicLibrary abstraction complete.
- [x] Windows Load/GetSymbol/Unload verified.
- [x] Plugin ABI v1 fixed.
- [x] API version checked before descriptor access.
- [x] PluginModule encapsulates full lifecycle.
- [x] PluginManager supports Load/Unload/Tick/Shutdown.
- [x] Duplicate ID rejected safely.
- [x] Plugin object destroyed by DLL export.
- [x] `FreeLibrary` always after Destroy.
- [x] Threaded SmokePlugin implemented.
- [x] Worker thread joined in `OnUnload`.
- [x] Repeated Load/Unload x100 passes.
- [x] BadVersionPlugin rejected with no resident module.
- [x] Shutdown unloads all plugins.
- [x] Editor starts/closes normally after integration.
- [x] Plugin manager dies before Application/Allocator shutdown.
- [x] No Plugin → Editor private implementation dependency.
- [x] No dynamic reflection unload problem introduced.
- [x] No long-lived plugin lambda/function pointer survives unload.

---

## Outcomes

Fill only when the full task is complete.

### Final implementation

- Windows DLL lifecycle, ABI v1, manager, threaded stress, Editor integration, strict manifest discovery/autoload and owner registration records.

### Architecture changes

- Engine owns infrastructure; Editor owns manager/frame subscription. Registration scopes close before OnUnload/Destroy. ABI v1 unchanged.

### Validation results

- Full build-debug, five standalone runners, threaded x100 and four live Editor cases passed. Complete script exited 0; all five state files restored byte-for-byte.

### Performance impact

- Explicit startup scanning and small per-module metadata storage; performance was not benchmarked.

### Known remaining issues

- Existing Editor missing-asset errors/allocator warnings remain. Matching C++ toolchain/runtime required. Menu/panel services and arbitrary delegate cleanup remain follow-ups.

### Architecture docs updated

- [x] `Doc/architecture/runtime-lifecycle.md`
- [x] `Doc/architecture/threading.md`
- [x] `Doc/architecture/editor-runtime-boundary.md`
- [x] `Doc/architecture/plugin-system.md` (M0–M6 current implementation)

## M0 accepted — 2026-09-30

Standalone DynamicLibrarySmoke passed; full build-debug passed, both exit 0. Module absence was verified after explicit and destructor unload. M0 acceptance checklist is complete in m0-dynamic-library.md. M1 was started only after this gate passed; M2 and later are outside this request.

## M1 accepted — 2026-09-30

PluginSmoke.exe passed (exit 0): all four fixed exports resolved, version gated before descriptor access, descriptor values/type validated, Create returned non-null, incompatible context and null host rejected, OnLoad/Tick/OnUnload/Destroy completed. Export/descriptor/instance pointers were cleared before FreeLibrary; module absence and copied strings were checked afterward. Full build-debug passed (exit 0). No Editor include/dependency or reflected plugin types were introduced.

ABI decisions: explicit __cdecl for exports and function pointer aliases; v1 requires matching C++ ABI/toolchain/architecture/runtime settings. Minimal plugin uses non-throwing allocation. Threaded smoke, bad-version DLL and repeated lifecycle stress remain M3 work. M2 was not started.

Final ABI review: export declarations are enabled only for AILU_BUILD_PLUGIN producers; hosts retain only dynamic-discovery aliases and receive no dllexport directives.

## M2 accepted — 2026-09-30

PluginManagerSmoke passed (exit 0), full build-debug passed (exit 0). Tests cover pre-init rejection, ID/count lookup, duplicate same/different DLL with module absence, reverse shutdown/Destroy order, missing DLL/version/descriptor/Create/Destroy, invalid descriptor, null Create and failed OnLoad rollback, wrong-thread rejection, Tick-time Load/Unload/Shutdown rejection and reinitialize. Central module cleanup clears all four exports before library unload. Failed OnLoad receives OnUnload before Destroy. Small v1 plugin list uses one owning vector as explicit load order, avoiding redundant map/order state. Destructor must run on owner thread; destruction during callbacks or on another thread is a contract violation.

## M3 accepted — 2026-09-30

PluginSmoke passed: 10 passed, 0 failed; 100 threaded load/Tick/activity-check/Unload iterations, 0 crashes, 0 hangs, 0 leaked modules. Diagnostic handles borrow GetModuleHandleW (no extra references) and pointers are cleared before unload. BadVersion DLL exports version only, rejected before complex ABI access and absent afterward. Shutdown joined active worker and unloaded module. Worker destructor terminates if still joinable. PluginManagerSmoke also passed including failed OnLoad cleanup sequence. Full build-debug passed (exit 0). M4 may now begin.

## M4 accepted — 2026-09-30

Final Debug build exit 0. RunPluginEditorSmoke.ps1 tested normal WM_CLOSE with no plugin and with threaded SmokePlugin, both exit 0. Before Application teardown: tick callback unsubscribed; worker joined; object destroyed; module unloaded. GetModuleHandleW was null after Finalize. Final evidence: F:/AiluEngine/artifacts/plugin-validation/m4-final/. Editor/EngineConfig.json, EditorConfig.json, dock_layout.json, main_widget.json and project editor_session.json restored in finally. Generated EditorApp.gen.h changes are normal codegen line-number updates only.

Baseline: both final Editor cases had 55 existing non-plugin ERROR lines (26 unique errors, mostly missing external textures), with no new error in threaded case and zero Plugin errors. Existing allocator warnings remain out of scope; this is plugin lifecycle acceptance, not global leak-free Editor acceptance.

Discovery: initial test invocation constructed a DLL path with a leading space; DLL never loaded and the immediate error-finalization run failed. The quoting was corrected, and test CLI now validates/normalizes the DLL path before Engine initialization. Successful final live tests use the fresh final binaries. Do not infer general zero-frame Editor error-teardown acceptance from these tests.

## M5 accepted — 2026-09-30

PluginManifestSmoke and full build-debug passed (exit 0). Scan and load stay separate. Explicit ordered roots, sorted one-level child directories, first duplicate kept. Strict seven-field JSON schema, uint32 API and boolean autoload, nonempty/NUL-free strings, relative contained library paths including symlink resolution. Manifest/DLL id/name/version/type must match before activation; DLL API check remains authoritative. Editor scans Engine/Project Plugins then runs explicit autoload. Missing roots are normal. Current work M6; no menu/panel API is planned.

## M6 accepted — 2026-09-30

Owner tokens/handles and Engine-owned records release before OnUnload/Destroy. PluginRegistrationSmoke passed owner isolation, stale reload/reinitialization tokens, wrong-thread rejection and in-callback cleanup observation. Debug build and lifecycle regressions passed. Four live Editor cases and full restoration/cleanup passed; see validation-m5-m6.md. M0–M6 complete, archived under Doc/exec-plans/completed/plugin-system.
