# M5 — Plugin Manifest Discovery, Scan, and Autoload

## Goal

Add plugin discovery without coupling startup infrastructure to AHT reflection or AssetSystem.

Scanning and loading remain separate operations.

---

## Expected files

Possible new files:

```text
Engine/Inc/Framework/Plugin/PluginManifest.h
Engine/Src/Framework/Plugin/PluginManifest.cpp
```

Adjust naming if the existing Framework layout suggests a smaller integration.

---

## Directory layout

Expected real-world layout:

```text
Plugins/
└── BlenderLiveLink/
    ├── plugin.json
    └── AiluBlenderLiveLink.dll
```

---

## Manifest v1

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

---

## Parser

Use lightweight RapidJSON parsing.

Do not create AHT reflected manifest types.

Plugin discovery should minimize dependencies on:

```text
ObjectRegister
Reflection
AssetSystem
```

---

## Search roots

Initial/future roots:

```text
<AiluRoot>/Plugins/
<ProjectRoot>/Plugins/
```

If duplicate IDs are discovered:

```text
log error
keep first discovered entry
skip later entry
```

Do not add override priority in v1.

---

## Scan vs Load

Required separation:

```cpp
ScanPlugins();
LoadPlugin(id);
```

Forbidden behavior:

```text
ScanPlugins
→ blindly load every DLL
```

Only:

```json
"autoload": true
```

causes automatic load.

This preserves a future Plugin Manager UI and keeps discovery cheap.

---

## API compatibility

Manifest `api_version` can reject an obviously unsupported plugin before loading the DLL.

The DLL export API version check from M1 remains authoritative after the module is loaded.

Do not remove the runtime check.

---

## Acceptance

- [x] valid manifest discovered.
- [x] malformed manifest ignored with useful log.
- [x] unsupported manifest `api_version` rejected.
- [x] duplicate ID rejected.
- [x] `autoload: true` loads plugin.
- [x] `autoload: false` discovers only.
- [x] scan does not load every DLL.
- [x] DLL runtime API check still executes when load occurs.
- [x] no dependency on Editor private implementation.
- [x] `cmake --build --preset build-debug` passes.

---

## Milestone completion procedure

After acceptance:

1. Mark M5 complete in `progress.md`.
2. Set current milestone to M6.
3. Continue with `m6-registration-scope.md`.

## Validation — 2026-09-30

PluginManifestSmoke passed (exit 0), final M5 full build-debug passed (exit 0). Four valid entries, eight malformed/unsupported entries rejected, two duplicate IDs skipped, deterministic first-entry retention across Engine/Project roots. Scan left DLL handles absent. Only autoload true loaded; explicit ID loaded the false entry. Manifest API=1 with runtime DLL API=2 was rejected with useful runtime log and no resident module; descriptor mismatch also rolled back. Parser tests include Unicode root, NUL string, traversal, bad types and duplicate fields. Per-entry filesystem failures are isolated. No reflection/asset-system dependency was introduced.
