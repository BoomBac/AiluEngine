#pragma once
#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

namespace Ailu
{
    // Callers must release DLL objects and callables before unloading the OS reference.
    class AILU_API DynamicLibrary final : public NonCopyable
    {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();
        bool Load(const WString& path);
        void Unload();
        [[nodiscard]] bool IsLoaded() const { return _handle != nullptr; }
        [[nodiscard]] void* GetSymbol(const char* name) const;
        [[nodiscard]] const WString& Path() const { return _path; }
    private:
        void* _handle = nullptr;
        WString _path;
    };
}
