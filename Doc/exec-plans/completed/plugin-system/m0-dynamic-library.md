# M0 — DynamicLibrary Platform Wrapper

## Goal

Provide a minimal platform abstraction over Windows DLL loading without leaking `HMODULE` into public headers.

Do not implement plugin ABI or PluginManager in this milestone.

---

## Relevant files

New:

```text
Engine/Inc/Framework/Platform/DynamicLibrary.h
Engine/Src/Framework/Platform/DynamicLibrary.cpp
```

Test code may be added wherever the existing Test CMake structure makes the smallest clean integration.

---

## Architecture constraints

Read:

```text
README.md
progress.md
Doc/architecture/runtime-lifecycle.md
Doc/architecture/threading.md
```

Global invariants from `README.md` apply.

`DynamicLibrary` itself does **not** know whether plugin instances/function pointers still exist. It only owns the OS library handle.

Higher layers must enforce lifecycle ordering.

---

## Suggested API

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

Do not expose `HMODULE` in the public header.

---

## Required behavior

### Load

- Already-loaded object must not silently replace its handle.
- Missing path returns `false`.
- `LoadLibraryW` failure logs:
  - DLL path.
  - `GetLastError()`.
- Successful load stores the normalized path.

### GetSymbol

- Unloaded object returns `nullptr`.
- Missing symbol returns `nullptr`.
- Missing symbol is a normal failure mode; do not assert.

### Unload

- Calling when already unloaded is safe.
- Calls `FreeLibrary`.
- Sets `_handle = nullptr`.
- Clears `_path`.

### Destructor

```cpp
DynamicLibrary::~DynamicLibrary()
{
    Unload();
}
```

---

## Validation implementation

Use a minimal DLL/module.

Test:

```text
Load
IsLoaded
GetSymbol
MissingSymbol
Unload
```

After unload:

```cpp
GetModuleHandleW(plugin_name) == nullptr
```

must hold for the test module.

Be careful that another process/module reference can keep a DLL resident; use a dedicated test DLL if needed.

---

## Acceptance

- [x] `DynamicLibrary.Load` passes.
- [x] `DynamicLibrary.IsLoaded` behaves correctly.
- [x] `DynamicLibrary.GetSymbol` resolves a known symbol.
- [x] Missing symbol returns `nullptr`.
- [x] `DynamicLibrary.Unload` passes.
- [x] Repeated `Unload()` is safe.
- [x] Module is absent after unload.
- [x] Failure path logs useful Windows error information.
- [x] `cmake --build --preset build-debug` passes.

---

## Milestone completion procedure

When all acceptance items pass:

1. Mark M0 complete in `progress.md`.
2. Record the exact validation result.
3. Add any meaningful discovery/decision.
4. Change `Current Milestone` in `progress.md` to M1.
5. Start reading `m1-plugin-abi.md`.

Do not start M1 before M0 passes.

## Validation result — 2026-09-30

DynamicLibrarySmoke.exe passed (exit 0): known export returned 42; unloaded/missing/invalid symbol checks, empty/missing path, second Load rejection, repeated Unload and destructor release passed. GetModuleHandleW returned null after both explicit and scoped unload. Failure logs included full path and Windows errors 123/126. Targeted build and cmake --build --preset build-debug passed (exit 0). Initial runner compile required UNICODE/_UNICODE and /utf-8 to match Engine public logging headers; corrected in target settings.
