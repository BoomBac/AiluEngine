#include "Ailu.h"
#include "EditorApp.h"
//#define _DEBUG_MEM_LEAK 1

 #ifdef _DEBUG_MEM_LEAK
 #define _CRTDBG_MAP_ALLOC
 #include <crtdbg.h>
 #include <stdlib.h>
 #endif

using namespace Ailu;

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hInstPrev, _In_ PSTR cmdline, _In_ int cmdshow)
{
#ifdef _DEBUG_MEM_LEAK
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(4627010);
#endif// _DEBUG_MEM_LEAK

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(),&argc);
    WString project_file_path = argc > 1? argv[1] : L"";

    if (project_file_path.empty())
    {
        MessageBoxW(nullptr,L"No project file specified.",L"Ailu Editor",MB_OK | MB_ICONERROR);
        return 1;
    }
    ApplicationInitContext ctx;
    for(auto i = 0; i < argc; i++)
        ctx._arguments.push_back(argv[i]);
    ctx._project_file_path = ctx._arguments[1];
    ctx._require_project = true;
    Editor::EditorApp app;
    if (app.Initialize(ctx) != 0)
    {
        return 1;
    }
    app.Tick(16.6f);
    app.Finalize();
#ifdef _DEBUG_MEM_LEAK
    _CrtDumpMemoryLeaks();// Check for memory leaks
#endif
    return 0;
}