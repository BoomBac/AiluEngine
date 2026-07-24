#include "Ailu.h"
#include "EditorApp.h"

#include <shellapi.h>

#pragma comment(lib, "Shell32.lib")

// #define _DEBUG_MEM_LEAK 1

#ifdef _DEBUG_MEM_LEAK
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <stdlib.h>
#endif

using namespace Ailu;

int WINAPI WinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE previous_instance,
    _In_ PSTR command_line,
    _In_ int command_show)
{
#ifdef _DEBUG_MEM_LEAK
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(4627010);
#endif

    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr)
    {
        MessageBoxW(nullptr, L"Failed to parse command line.", L"Ailu Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    ApplicationInitContext context;
    for (int i = 0; i < argument_count; ++i)
        context._arguments.emplace_back(arguments[i]);

    LocalFree(arguments);
    arguments = nullptr;

    if (context._arguments.size() <= 1)
    {
        MessageBoxW(nullptr, L"No project file specified.", L"Ailu Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    context._project_file_path = context._arguments[1];
    context._require_project = true;

    Editor::EditorApp app;
    if (app.Initialize(context) != 0)
        return 1;

    app.Tick(16.6f);
    app.Finalize();

#ifdef _DEBUG_MEM_LEAK
    _CrtDumpMemoryLeaks();
#endif

    return 0;
}