#include "Framework/Platform/DynamicLibrary.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <filesystem>

namespace Ailu
{
    DynamicLibrary::~DynamicLibrary() { Unload(); }

    bool DynamicLibrary::Load(const WString& path)
    {
        if (IsLoaded())
        {
            LOG_ERROR(L"[Plugin] Library already loaded: {}", _path);
            return false;
        }
        if (path.empty())
        {
            LOG_ERROR(L"[Plugin] LoadLibrary failed: path='{}', Windows error={}", path, ERROR_INVALID_NAME);
            return false;
        }
        std::error_code error;
        auto normalized_path = std::filesystem::absolute(std::filesystem::path(path), error).lexically_normal();
        if (error)
        {
            LOG_ERROR(L"[Plugin] Invalid library path: '{}', error={}", path, error.value());
            return false;
        }
        auto stored_path = normalized_path.wstring();
        auto handle = LoadLibraryW(stored_path.c_str());
        if (!handle)
        {
            const auto windows_error = GetLastError();
            LOG_ERROR(L"[Plugin] LoadLibrary failed: path='{}', Windows error={}", stored_path, windows_error);
            return false;
        }
        _path.swap(stored_path);
        _handle = handle;
        return true;
    }

    void DynamicLibrary::Unload()
    {
        if (_handle)
        {
            if (!FreeLibrary(static_cast<HMODULE>(_handle)))
            {
                const auto windows_error = GetLastError();
                LOG_ERROR(L"[Plugin] FreeLibrary failed: path='{}', Windows error={}", _path, windows_error);
                return;
            }
            _handle = nullptr;
        }
        _path.clear();
    }

    void* DynamicLibrary::GetSymbol(const char* name) const
    {
        if (!_handle || !name || !*name)
            return nullptr;
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(_handle), name));
    }
}
