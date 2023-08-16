#include "pch.h"
#include "Framework/Common/WindowsApp.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/LogMgr.h"
#include "RHI/DX12/BaseRenderer.h"
#include "Framework/Events/KeyEvent.h"


namespace Ailu
{
    static const wchar_t* kAppTitleIconPath = GET_ENGINE_FULL_PATHW(Res/Ico/app_title_icon.ico);
    static const wchar_t* kAppIconPath = GET_ENGINE_FULL_PATHW(Res/Ico/app_icon.ico);

    LogMgr* g_pLogMgr;
    DXBaseRenderer* renderer = new DXBaseRenderer(1280, 720);

    WindowsApp::WindowsApp(HINSTANCE hinstance, int argc) : _hinstance(hinstance), _argc(argc)
    {
        WCHAR** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        _b_exit = false;
        _argv = nullptr;
    }
    int WindowsApp::Initialize()
    {
        g_pLogMgr = new LogMgr();
        g_pLogMgr->Initialize();
        LOG_WARNING(" WindowsApp::Initialize{}  {}", 20, "aaa")
            LOG_WARNING(L" 程序初始化  {}", 20, "aaa")
            g_pLogMgr->LogFormat("fdfs{}", 20);
        // Initialize the window class.
        WNDCLASSEX windowClass = { 0 };
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = _hinstance;
        windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = L"AiluEngineClass";
        RegisterClassEx(&windowClass);

        RECT windowRect = { 0, 0, static_cast<LONG>(GetConfiguration().viewport_width_), static_cast<LONG>(GetConfiguration().viewport_height_) };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
        // Create the window and store a handle to it.
        _hwnd = CreateWindow(
            windowClass.lpszClassName,
            ToWChar(GfxConfiguration().window_name_),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,        // We have no parent window.
            nullptr,        // We aren't using menus.
            _hinstance,
            nullptr);
        // Initialize the sample. OnInit is defined in each child-implementation of DXSample.
        //pSample->OnInit();

        ShowWindow(_hwnd, _argc);
        HANDLE hTitleIcon = LoadImage(0, kAppTitleIconPath, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_LOADFROMFILE);
        HANDLE hIcon = LoadImage(0, kAppIconPath, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_LOADFROMFILE);
        if (hTitleIcon && hIcon) {
            //Change both icons to the same icon handle.
            SendMessage(_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hTitleIcon);
            SendMessage(_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

            //This will ensure that the application icon gets changed too.
            SendMessage(GetWindow(_hwnd, GW_OWNER), WM_SETICON, ICON_SMALL, (LPARAM)hTitleIcon);
            SendMessage(GetWindow(_hwnd, GW_OWNER), WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }
        else
        {
            LOG_WARNING("TitleIcon or AppIcon load failed,please check out the path!")
        }
        _b_exit = false;
        renderer->OnInit();
        return 0;
        // Return this part of the WM_QUIT message to Windows.
        //return static_cast<char>(msg.wParam);
    }
    void WindowsApp::Finalize()
    {
        g_pLogMgr->Finalize();
        renderer->OnDestroy();
    }
    void WindowsApp::Tick()
    {
        // Main sample loop.
        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            // Process any messages in the queue.
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        _b_exit = true;
    }
    void WindowsApp::SetCommandLineParameters(int argc, char** argv)
    {
    }
    int WindowsApp::GetCommandLineArgumentsCount() const
    {
        return 0;
    }
    const char* WindowsApp::GetCommandLineArgument(int index) const
    {
        return nullptr;
    }
    const GfxConfiguration& WindowsApp::GetConfiguration() const
    {
        static GfxConfiguration cfg;
        return cfg;
    }
    void WindowsApp::Run()
    {
        Tick();
    }
    HWND WindowsApp::GetMainWindowHandle()
    {
        return _hwnd;
    }

    LRESULT WindowsApp::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            // Save the DXSample* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
        return 0;

        case WM_KEYDOWN:
        {
            Event* e = new KeyPressedEvent(0,1);
            static EventDispather dispater(*e);
            dispater.CommitEvent(*e);
            dispater.Dispatch<KeyPressedEvent>([e](KeyPressedEvent& e) {
                LOG(e.ToString())
                return true; });           
        }
            return 0;

        case WM_KEYUP:
            return 0;

        case WM_PAINT:
        {
            renderer->OnRender();
        }
        return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        // Handle any messages the switch statement didn't.
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}
