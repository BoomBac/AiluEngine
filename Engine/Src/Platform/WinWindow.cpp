#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Events/WindowEvent.h"
#include "Platform/WinInput.h"
#include "pch.h"

#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"

#ifdef DEAR_IMGUI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif// DEAR_IMGUI
namespace Ailu
{
    // static const wchar_t *kAppTitleIconPath = ToWStr(kEngineResRootPath + "Icons/app_title_icon.ico");
    // static const wchar_t *kAppIconPath = ToWStr(kEngineResRootPath + "Icons/app_icon.ico");

    static void HideCursor()
    {
        while (::ShowCursor(FALSE) >= 0);// 隐藏鼠标指针，直到它不再可见
    }

    static void ShowCursor()
    {
        while (::ShowCursor(TRUE) < 0);// 显示鼠标指针，直到它可见
    }

    DWORD cmd_id;
    static void CreateConsoleProcess()
    {
        // 创建用于重定向的管道
        HANDLE hStdOutputRead, hStdOutputWrite;
        HANDLE hStdInputRead, hStdInputWrite;
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = nullptr;
        sa.bInheritHandle = TRUE;

        // 创建输出管道
        CreatePipe(&hStdOutputRead, &hStdOutputWrite, &sa, 0);
        SetHandleInformation(hStdOutputRead, HANDLE_FLAG_INHERIT, 0);

        // 创建输入管道
        CreatePipe(&hStdInputRead, &hStdInputWrite, &sa, 0);
        SetHandleInformation(hStdInputWrite, HANDLE_FLAG_INHERIT, 0);

        // 设置启动信息
        STARTUPINFO si = {sizeof(STARTUPINFO)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdOutputWrite;
        si.hStdError = hStdOutputWrite;
        si.hStdInput = hStdInputRead;

        // 进程信息
        PROCESS_INFORMATION pi;
        wchar_t commandLine[] = L"cmd.exe";
        // 创建控制台进程
        if (CreateProcess(
                    nullptr,           // No module name (use command line)
                    commandLine,       // Command line
                    nullptr,           // Process handle not inheritable
                    nullptr,           // Thread handle not inheritable
                    TRUE,              // Set handle inheritance to TRUE
                    CREATE_NEW_CONSOLE,// Create a new console
                    nullptr,           // Use parent's environment block
                    nullptr,           // Use parent's starting directory
                    &si,               // Pointer to STARTUPINFO structure
                    &pi)               // Pointer to PROCESS_INFORMATION structure
        )
        {
            // 关闭不必要的管道句柄
            CloseHandle(hStdOutputWrite);
            CloseHandle(hStdInputRead);
            //::WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            cmd_id = pi.dwProcessId;
        }
        else
        {
            std::cerr << "Failed to create console process" << std::endl;
        }
    }

    // Function to redirect standard input/output to the console
    static void RedirectIOToConsole()
    {
        // Open the console's standard input, output, and error streams
        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        // Set the console code page to UTF-8 so console known how to interpret string data
        SetConsoleOutputCP(CP_UTF8);

        std::cout << "Console attached and ready for output!" << std::endl;
    }

    WinWindow::WinWindow(const WindowProps &prop)
    {
        Init(prop);
    }

    WinWindow::~WinWindow()
    {
    }

    void WinWindow::Init(const WindowProps &prop)
    {
        //调用 alloc分配的console无法对字符进行着色，需要手动创建进程，但是
        //需要等待控制台创建完成 attach才能成功，复杂的等待机制以后再写
        // //https://stackoverflow.com/questions/5907917/how-to-use-win32-createprocess-function-to-wait-until-the-child-done-to-write-to
        // CreateConsoleProcess();
        // Sleep(1000);
        // if (AttachConsole(cmd_id))
        // {
        //     RedirectIOToConsole();
        //     std::cout << "Hello from attached console!" << std::endl;
        // }
        // Try to attach to the parent process console
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            RedirectIOToConsole();
            std::cout << "Hello from attached console!" << std::endl;
        }
        else
        {
            // If attaching failed, allocate a new console
            //CreateConsoleProcess();
            AllocConsole();
            RedirectIOToConsole();
            std::cout << "Hello from new console!" << std::endl;
        }

        _data.Width = prop.Width;
        _data.Height = prop.Height;
        _data.Title = prop.Title;
        _data.Handler = [](Event &e)
        {
            LOG_INFO("Default Event Handle: {}", e.ToString())
            return true;
        };
        auto hinstance = GetModuleHandle(NULL);
        LOG_INFO(L"Create window {}, ({},{})", prop.Title, prop.Width, prop.Height);
        // Initialize the window class.
        WNDCLASSEX windowClass = {0};
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = hinstance;
        windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = L"AiluEngineClass";
        RegisterClassEx(&windowClass);

        RECT windowRect = {0, 0, static_cast<LONG>(prop.Width), static_cast<LONG>(prop.Height)};
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
        // Create the window and store a handle to it.
        _hwnd = CreateWindowEx(
                WS_EX_APPWINDOW,
                windowClass.lpszClassName,
                _data.Title.c_str(),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                windowRect.right - windowRect.left,
                windowRect.bottom - windowRect.top,
                nullptr,// We have no parent window.
                nullptr,// We aren't using menus.
                hinstance,
                this);

        //移除调整窗口大小的样式
        LONG_PTR style = GetWindowLongPtr(_hwnd, GWL_STYLE);
        //style = style & ~WS_THICKFRAME;
        SetWindowLongPtr(_hwnd, GWL_STYLE, style);
        ShowWindow(_hwnd, SW_SHOW);
        UpdateWindow(_hwnd);
        HANDLE hTitleIcon = LoadImage(0, ResourceMgr::GetResSysPath(L"Icons/app_title_icon.ico").c_str(), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_LOADFROMFILE);
        HANDLE hIcon = LoadImage(0, ResourceMgr::GetResSysPath(L"Icons/app_icon.ico").c_str(), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_LOADFROMFILE);
        if (hTitleIcon && hIcon)
        {
            //Change both icons to the same icon handle.
            SendMessage(_hwnd, WM_SETICON, ICON_SMALL, (LPARAM) hTitleIcon);
            SendMessage(_hwnd, WM_SETICON, ICON_BIG, (LPARAM) hIcon);

            //This will ensure that the application icon gets changed too.
            SendMessage(GetWindow(_hwnd, GW_OWNER), WM_SETICON, ICON_SMALL, (LPARAM) hTitleIcon);
            SendMessage(GetWindow(_hwnd, GW_OWNER), WM_SETICON, ICON_BIG, (LPARAM) hIcon);
        }
        else
        {
            LOG_WARNING("TitleIcon or AppIcon load failed,please check out the path!")
        }
        WinInput::Create(_hwnd);
        DragAcceptFiles(_hwnd, true);
        _is_focused = true;
    }
    void WinWindow::OnUpdate()
    {
        MSG msg = {};
        //while (msg.message != WM_QUIT)
        //{
        //    // Process any messages in the queue.

        //}
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    u32 WinWindow::GetWidth() const
    {
        return _data.Width;
    }
    u32 WinWindow::GetHeight() const
    {
        return _data.Height;
    }
    void WinWindow::SetEventHandler(const EventHandler &handler)
    {
        _data.Handler = handler;
    }
    void WinWindow::SetVSync(bool enabled)
    {
    }
    bool WinWindow::IsVSync() const
    {
        return false;
    }

    void *WinWindow::GetNativeWindowPtr() const
    {
        return static_cast<void *>(_hwnd);
    }

    std::tuple<i32, i32> WinWindow::GetWindowPosition() const
    {
        RECT rect;
        //GetClientRect(_hwnd, &rect);
        GetWindowRect(_hwnd, &rect);
        // 获取窗口所在的显示器
        HMONITOR hMonitor = MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hMonitor, &monitorInfo);
        // 获取显示器的工作区域
        RECT monitorRect = monitorInfo.rcWork;
        // 计算窗口相对于显示器的坐标
        return std::tuple<u32, u32>(rect.left - monitorRect.left + 8, rect.top - monitorRect.top + 8);
    }

    void WinWindow::Shutdown()
    {
    }

    LRESULT WinWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
#ifdef DEAR_IMGUI
        if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
            return true;
#endif// DEAR_IMGUI

        //return DefWindowProc(hWnd, message, wParam, lParam);
        //if (message == WM_NCCREATE)
        //{
        //    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
        //    return TRUE;
        //}
        return ((WinWindow *) GetWindowLongPtr(hWnd, GWLP_USERDATA))->WindowProcImpl(hWnd, message, wParam, lParam);
    }

#pragma warning(push)
#pragma warning(disable : 4244)
    LRESULT WinWindow::WindowProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
                //case WM_NCCALCSIZE:
                //    // 移除窗口边框，使客户区占据整个窗口
                //    return 0;
            case WM_CREATE:
            {
                // Save the DXSample* passed in to CreateWindow.
                LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
                SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
                //DragAcceptFiles(_hwnd, true);
                //DragAcceptFiles(((LPCREATESTRUCT)lParam)->hwndParent, TRUE);
            }
                return 0;
            case WM_DROPFILES:
            {
                // Handle dropped files
                HDROP hDrop = (HDROP) wParam;
                UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
                List<WString> draged_files;
                for (UINT i = 0; i < fileCount; ++i)
                {
                    UINT pathLength = DragQueryFile(hDrop, i, NULL, 0);
                    wchar_t *filePath = new wchar_t[pathLength + 1];
                    DragQueryFile(hDrop, i, filePath, pathLength + 1);
                    draged_files.emplace_back(filePath);
                    delete[] filePath;
                }
                DragFileEvent e(draged_files);
                _data.Handler(e);

                DragFinish(hDrop);
            }
                return 0;
            case WM_KEYDOWN:
            {
                KeyPressedEvent e(static_cast<u8>(wParam), lParam & 0xFFFF);
                _data.Handler(e);
            }
                return 0;
            case WM_KEYUP:
            {
                KeyReleasedEvent e(wParam);
                _data.Handler(e);
            }
                return 0;
            case WM_PAINT:
            {
            }
                return 0;
            case WM_DESTROY:
            {
                WindowCloseEvent e;
                _data.Handler(e);
                PostQuitMessage(0);
            }
                return 0;
            case WM_SETFOCUS:
            {
                WindowFocusEvent e;
                _data.Handler(e);
            }
                return 0;
            case WM_KILLFOCUS:
            {
                WindowLostFocusEvent e;
                _data.Handler(e);
            }
                return 0;
            case WM_SIZE:
            {
                if (wParam == SIZE_MINIMIZED)
                {
                    WindowMinimizeEvent e;
                    _data.Handler(e);
                }
                else
                {
                    u32 w = static_cast<u32>(lParam & 0XFFFF), h = static_cast<u32>(lParam >> 16);
                    _data.Width = w;
                    _data.Height = h;
                    SetWindowText(_hwnd, std::format(L"{} {}x{}", _data.Title, _data.Width, _data.Height).c_str());
                    WindowResizeEvent e(w, h);
                    _data.Handler(e);
                }
            }
                return 0;
            case WM_MOUSEMOVE:
            {
                MouseMovedEvent e(static_cast<float>(HIGH_BIT(lParam, 16)), static_cast<float>(LOW_BIT(lParam, 16)));
                _data.Handler(e);
            }
                return 0;
            case WM_MBUTTONDOWN:
            {
                /*MK_LBUTTON
			0x0001
			The left mouse button is down.
			MK_MBUTTON
			0x0010
			The middle mouse button is down.
			MK_RBUTTON
			0x0002
			The right mouse button is down.
			*/
                MouseButtonPressedEvent e(AL_KEY_MBUTTON);
                _data.Handler(e);
            }
                return 0;
            case WM_MBUTTONUP:
            {
                MouseButtonReleasedEvent e(AL_KEY_MBUTTON);
                _data.Handler(e);
            }
                return 0;
            case WM_LBUTTONDOWN:
            {
                MouseButtonPressedEvent e(AL_KEY_LBUTTON);
                _data.Handler(e);
            }
                return 0;
            case WM_LBUTTONUP:
            {
                MouseButtonReleasedEvent e(AL_KEY_LBUTTON);
                _data.Handler(e);
            }
                return 0;
            case WM_RBUTTONDOWN:
            {
                MouseButtonPressedEvent e(AL_KEY_RBUTTON);
                _data.Handler(e);
            }
                return 0;
            case WM_RBUTTONUP:
            {
                MouseButtonReleasedEvent e(AL_KEY_RBUTTON);
                _data.Handler(e);
            }
                return 0;
            case WM_MOUSEWHEEL:
            {
                auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                MouseScrollEvent e(static_cast<float>(zDelta));
                _data.Handler(e);
            }
                return 0;
            case WM_ENTERSIZEMOVE:
            {
                WindowMovedEvent e(true);
                _data.Handler(e);
            }
                return 0;
            case WM_EXITSIZEMOVE:
            {
                WindowMovedEvent e(false);
                _data.Handler(e);
            }
                return 0;
        }
        // Handle any messages the switch statement didn't.
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

#pragma warning(pop)
}// namespace Ailu
