#include "Framework/Platform/DynamicLibrary.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace Ailu;

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    static_assert(!std::is_copy_constructible_v<DynamicLibrary>);
    static_assert(!std::is_move_constructible_v<DynamicLibrary>);
    LogMgr::Init();
    LogMgr::Get().AddAppender(new ConsoleAppender);
    int result = 0;
    try
    {
        wchar_t executable_path[32768]{};
        Require(GetModuleFileNameW(nullptr, executable_path, 32768) != 0, "Executable path");
        const auto path = std::filesystem::path(executable_path).parent_path() / L"AiluDynamicLibraryProbe.dll";
        Require(GetModuleHandleW(path.c_str()) == nullptr, "Module initially absent");
        DynamicLibrary library;
        Require(!library.IsLoaded() && library.Path().empty(), "Initial state");
        Require(library.GetSymbol("AiluDynamicLibraryProbe") == nullptr, "Unloaded symbol");
        Require(!library.Load(L""), "Empty path rejected");
        Require(!library.Load(path.wstring() + L".missing"), "Missing path rejected");
        Require(!library.IsLoaded() && library.Path().empty(), "Failure state");
        Require(library.Load(path.wstring()), "Load");
        Require(library.IsLoaded() && library.Path() == path.lexically_normal().wstring(), "Loaded state/path");
        Require(!library.Load(path.wstring()) && library.IsLoaded(), "Second load rejected");
        auto probe = reinterpret_cast<int (*)() noexcept>(library.GetSymbol("AiluDynamicLibraryProbe"));
        Require(probe && probe() == 42, "Known symbol callable");
        Require(library.GetSymbol("MissingSymbol") == nullptr, "Missing symbol");
        Require(library.GetSymbol(nullptr) == nullptr && library.GetSymbol("") == nullptr, "Invalid symbol");
        probe = nullptr;
        library.Unload();
        Require(!library.IsLoaded() && library.Path().empty(), "Unload state");
        library.Unload();
        Require(GetModuleHandleW(path.c_str()) == nullptr, "Module absent after unload");
        {
            DynamicLibrary scoped_library;
            Require(scoped_library.Load(path.wstring()), "Scoped load");
        }
        Require(GetModuleHandleW(path.c_str()) == nullptr, "Destructor releases module");
        std::cout << "M0 DynamicLibrary: PASS\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "M0 DynamicLibrary: FAIL: " << error.what() << '\n';
        result = 1;
    }
    LogMgr::Shutdown();
    return result;
}
