#include "Ailu.h"
#include "EditorApp.h"

// #ifdef _DEBUG_MEM_LEAK
// #define _CRTDBG_MAP_ALLOC
// #include <crtdbg.h>
// #include <stdlib.h>
// #endif

using namespace Ailu;
static Ailu::ApplicationDesc LoadApplicationConfig(WString sys_path)
{
    LOG_INFO(sys_path);
    return ApplicationDesc();
}

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hInstPrev, _In_ PSTR cmdline, _In_ int cmdshow)
{
    auto desc = LoadApplicationConfig(Ailu::Application::GetWorkingPath());
    Editor::EditorApp *app = new Editor::EditorApp();
    //Ailu::Application* app = new Ailu::Application();
    app->Initialize();
    app->Tick(16.6f);
    app->Finalize();
    DESTORY_PTR(app)
    // #ifdef _DEBUG_MEM_LEAK
    //     _CrtDumpMemoryLeaks();// Check for memory leaks
    // #endif
    return 0;
}