#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Events/WindowEvent.h"
#include "Platform/WinInput.h"
#include "pch.h"
#include <dwmapi.h> //dark theme
#include <imm.h>

#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/Application.h"


//#define _USE_CONSOLE

#ifdef DEAR_IMGUI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif// DEAR_IMGUI

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{
    class MyDropTarget : public IDropTarget
    {
    public:
        // Implement IUnknown methods
        ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
        ULONG STDMETHODCALLTYPE Release() override { return 1; }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
        {
            if (riid == IID_IUnknown || riid == IID_IDropTarget)
            {
                *ppvObject = static_cast<IDropTarget *>(this);
                AddRef();
                return S_OK;
            }
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        HRESULT DragEnter(IDataObject *, DWORD, POINTL, DWORD *pdwEffect) override
        {
            Ailu::Input::BlockInput(true);
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }

        HRESULT DragLeave() override
        {
            Ailu::Input::BlockInput(false);
            return S_OK;
        }

        HRESULT Drop(IDataObject *, DWORD, POINTL, DWORD *pdwEffect) override
        {
            Ailu::Input::BlockInput(false);
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }

        HRESULT DragOver(DWORD, POINTL, DWORD *pdwEffect) override
        {
            *pdwEffect = DROPEFFECT_COPY;
            return S_OK;
        }
    };
}

namespace Ailu
{
    // static const wchar_t *kAppTitleIconPath = ToWChar(kEngineResRootPath + "Icons/app_title_icon.ico");
    // static const wchar_t *kAppIconPath = ToWChar(kEngineResRootPath + "Icons/app_icon.ico");

    static void HideCursor()
    {
        while (::ShowCursor(FALSE) >= 0);// 隐藏鼠标指针，直到它不再可见
    }

    static void ShowCursor()
    {
        while (::ShowCursor(TRUE) < 0);// 显示鼠标指针，直到它可见
    }

    // 禁用输入法
    static void DisableIME(HWND hWnd)
    {
        ImmAssociateContext(hWnd, NULL);
    }

    // 恢复输入法
    static void EnableIME(HWND hWnd)
    {
        HIMC hIMC = ImmGetContext(hWnd);
        if (hIMC)
        {
            ImmAssociateContext(hWnd, hIMC);
            ImmReleaseContext(hWnd, hIMC);
        }
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
#ifdef _USE_CONSOLE
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
#endif
        _data.Width = prop.Width;
        _data.Height = prop.Height;
        _data.Title = prop.Title;
        _data.Handler = [](Event &e)
        {
            //LOG_INFO("Default Event Handle: {}", e.ToString())
            return true;
        };
        _data._flags = prop._flag;
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
        u32 style_flags = WS_OVERLAPPEDWINDOW;
        if (prop._flag & EWindowFlags::kWindow_NoTitleBar)
        {
            style_flags = WS_POPUP | WS_VISIBLE;
        }
        auto main_win = Application::Get().GetWindowPtr();
        bool is_sub_window = false;//main_win != nullptr;
        RECT windowRect = {0, 0, static_cast<LONG>(prop.Width), static_cast<LONG>(prop.Height)};
        if (style_flags & WS_OVERLAPPEDWINDOW)
            AdjustWindowRect(&windowRect, style_flags, FALSE);
        // Create the window and store a handle to it.
        _hwnd = CreateWindowEx(
                is_sub_window? WS_EX_TOOLWINDOW: WS_EX_APPWINDOW,
                windowClass.lpszClassName,
                _data.Title.c_str(),
                style_flags,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                windowRect.right - windowRect.left,
                windowRect.bottom - windowRect.top,
                nullptr,// We have no parent window.
                nullptr,// We aren't using menus.
                hinstance,
                this);
        //dark mode
        //BOOL value = TRUE;
        //::DwmSetWindowAttribute(_hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&value,sizeof(value));
        //移除调整窗口大小的样式
        LONG_PTR style = GetWindowLongPtr(_hwnd, GWL_STYLE);
        //style = style & ~WS_THICKFRAME;
        SetWindowLongPtr(_hwnd, GWL_STYLE, style);
        ShowWindow(_hwnd, SW_SHOW);
        UpdateWindow(_hwnd);
        SetWindowText(_hwnd, prop.Title.c_str());
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
        Input::SetupPlatformInput(MakeScope<WinInput>(_hwnd));
        DragAcceptFiles(_hwnd, true);
        _is_focused = true;
        //关闭输入法
        DisableIME(_hwnd);
        _reserver_area = {0.0f,0.0f,100.0f,20.0f};
        //注册拖动事件，拖动文件时屏蔽输入
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        MyDropTarget *target = new MyDropTarget();
        RegisterDragDrop(_hwnd, target);
    }
    void WinWindow::OnUpdate()
    {
        //MSG msg = {};
        //if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        //{
        //    TranslateMessage(&msg);
        //    DispatchMessage(&msg);
        //}
        DWORD result = MsgWaitForMultipleObjects(
                0,// 不等其他内核对象
                nullptr,
                FALSE,
                INFINITE,  // 无限等待
                QS_ALLINPUT// 只要有消息就唤醒
        );

        if (result == WAIT_OBJECT_0)
        {
            MSG msg;
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
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
    void WinWindow::SetWindowSize(u16 w, u16 h)
    {
        SetWindowPos(_hwnd, nullptr, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void WinWindow::SetClientSize(u16 w, u16 h)
    {
        RECT rc = {0, 0, (LONG) w, (LONG) h};
        DWORD style = GetWindowLong(_hwnd, GWL_STYLE);
        DWORD exStyle = GetWindowLong(_hwnd, GWL_EXSTYLE);

        AdjustWindowRectEx(&rc, style, FALSE, exStyle);

        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        SetWindowPos(_hwnd, nullptr, 0, 0, winW, winH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    std::tuple<u16, u16> Ailu::WinWindow::GetWindowSize()
    {
        return std::tuple<u16, u16>((u16)_data.Width, (u16)_data.Height);
    }
    std::tuple<u16, u16> Ailu::WinWindow::GetClientSize()
    {
        RECT rc;
        GetClientRect(_hwnd, &rc);
        u16 w = static_cast<u16>(rc.right - rc.left);
        u16 h = static_cast<u16>(rc.bottom - rc.top);
        return {w, h};
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

    std::tuple<i32, i32> WinWindow::GetClientPosition() const 
    {
        POINT pt = { 0, 0 };
        ClientToScreen(_hwnd, &pt);
        return { static_cast<i32>(pt.x), static_cast<i32>(pt.y) };
    }

    std::tuple<i32, i32> WinWindow::GetWindowPosition() const
    {
        RECT rc;
        GetWindowRect(_hwnd, &rc);
        return {rc.left, rc.top};// 窗口左上角在屏幕坐标的位置
    }

    void WinWindow::SetTitle(const WString& title)
    {
        _data.Title = title;
        SetWindowText(_hwnd,_data.Title.c_str());
    }

    WString WinWindow::GetTitle()
    {
        return _data.Title;
    }

    void WinWindow::SetPosition(i32 x, i32 y)
    {
        SetWindowPos(_hwnd, HWND_TOP, x, y, (i32)_data.Width, (i32)_data.Height, SWP_SHOWWINDOW);
    }

    std::tuple<f32, f32, f32, f32> WinWindow::ReserveArea() const
    {
        return std::tuple<f32, f32, f32, f32>(_reserver_area[0], _reserver_area[1], _reserver_area[2], _reserver_area[3]);
    }

    void WinWindow::ReserveArea(f32 x, f32 y, f32 w, f32 h)
    {
        _reserver_area[0] = x;
        _reserver_area[1] = y;
        _reserver_area[2] = w;
        _reserver_area[3] = h;
    }

    void WinWindow::Shutdown()
    {
    }

    LRESULT WinWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
#ifdef DEAR_IMGUI
        //ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam);
        if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
            return true;
#endif// DEAR_IMGUI
        return ((WinWindow *) GetWindowLongPtr(hWnd, GWLP_USERDATA))->WindowProcImpl(hWnd, message, wParam, lParam);
    }

#pragma warning(push)
#pragma warning(disable : 4244)
    LRESULT WinWindow::WindowProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        static bool s_is_track = false;
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
                Vector<WString> draged_files(fileCount);
                for (UINT i = 0; i < fileCount; ++i)
                {
                    UINT pathLength = DragQueryFile(hDrop, i, NULL, 0);
                    wchar_t *filePath = new wchar_t[pathLength + 1];
                    DragQueryFile(hDrop, i, filePath, pathLength + 1);
                    draged_files[i] = filePath;
                    delete[] filePath;
                }
                DragFileEvent e(draged_files);
                e._window = this;
                _data.Handler(e);

                DragFinish(hDrop);
            }
                return 0;
            case WM_KEYDOWN:
            {
                KeyPressedEvent e(static_cast<u8>(wParam), lParam & 0xFFFF);
                e._window = this;
                //LOG_INFO("WM_KEYDOWN: {}", e.GetKeyCode());
                _data.Handler(e);
            }
                return 0;
            case WM_KEYUP:
            {
                KeyReleasedEvent e(wParam);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_PAINT:
            {
            }
                return 0;
            case WM_DESTROY:
            {
                WindowCloseEvent e(_hwnd);
                e._window = this;
                _data.Handler(e);
                PostQuitMessage(0);
            }
                return 0;
            case WM_SETFOCUS:
            {
                WindowFocusEvent e;
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_KILLFOCUS:
            {
                WindowLostFocusEvent e;
                e._window = this;
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
                    WindowResizeEvent e(hWnd, w, h);
                    e._window = this;
                    _data.Handler(e);
                }
            }
                return 0;
            case WM_MOUSEMOVE:
            {
                if (!s_is_track)
                {
                    TRACKMOUSEEVENT tme;
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;// 请求接收 WM_MOUSELEAVE
                    tme.hwndTrack = _hwnd;
                    tme.dwHoverTime = HOVER_DEFAULT;
                    TrackMouseEvent(&tme);
                    s_is_track = true;
                }
                MouseMovedEvent e(static_cast<float>(LOW_BIT(lParam, 16)), static_cast<float>(HIGH_BIT(lParam, 16)));
                e._window = this;
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
                MouseButtonPressedEvent e(EKey::kMBUTTON);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_MBUTTONUP:
            {
                MouseButtonReleasedEvent e(EKey::kMBUTTON);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_LBUTTONDOWN:
            {
                MouseButtonPressedEvent e(EKey::kLBUTTON);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_LBUTTONUP:
            {
                MouseButtonReleasedEvent e(EKey::kLBUTTON);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_RBUTTONDOWN:
            {
                MouseButtonPressedEvent e(EKey::kRBUTTON);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_RBUTTONUP:
            {
                MouseButtonReleasedEvent e(EKey::kRBUTTON);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_MOUSEWHEEL:
            {
                auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                MouseScrollEvent e(static_cast<float>(zDelta));
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_ENTERSIZEMOVE:
            {
                WindowMovedEvent e(true);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_EXITSIZEMOVE:
            {
                WindowMovedEvent e(false);
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_MOUSELEAVE:
            {
                s_is_track = false;
                MouseExitWindowEvent e;
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_MOUSEHOVER:
            {
                MouseEnterWindowEvent e;
                e._window = this;
                _data.Handler(e);
            }
                return 0;
            case WM_NCHITTEST://无边框窗口的移动/缩放
            {
                static const auto is_point_inside = [](Vector2f point, Vector4f rect) {
                    return point.x >= rect.x && point.x <= rect.x + rect.z && point.y >= rect.y && point.y <= rect.y + rect.w;
                };
                if (_data._flags & EWindowFlags::kWindow_NoTitleBar)
                {
                    Vector2f pos = Input::GetMousePos(this);
                    if (is_point_inside(pos, {(f32) (_data.Width - _reserver_area[3]), 0.0f, (f32) _reserver_area[3], (f32) _reserver_area[3]}))
                    {
                        Application::Get().SetCursor(ECursorType::kArrow);
                        return HTCLIENT;
                    }
                    f32 border = 4.0f;
                    if (pos.y < border)
                    {
                        if (pos.x < border)
                        {
                            Application::Get().SetCursor(ECursorType::kSizeNWSE);
                            return HTTOPLEFT;
                        }
                        else if (pos.x > _data.Width - border)
                        {
                            Application::Get().SetCursor(ECursorType::kSizeNWSE);
                            return HTTOPRIGHT;
                        }
                        Application::Get().SetCursor(ECursorType::kSizeNS);
                        return HTTOP;
                    }
                    if (pos.y > _data.Height - border)
                    {
                        if (pos.x < border)
                        {
                            Application::Get().SetCursor(ECursorType::kSizeNWSE);
                            return HTBOTTOMLEFT;
                        }
                        else if (pos.x > _data.Width - border)
                        {
                            Application::Get().SetCursor(ECursorType::kSizeNWSE);
                            return HTBOTTOMRIGHT;
                        }
                        Application::Get().SetCursor(ECursorType::kSizeNS);
                        return HTBOTTOM;
                    }
                    if (pos.x < border)
                    {
                        Application::Get().SetCursor(ECursorType::kSizeEW);
                        return HTLEFT;
                    }
                    if (pos.x > _data.Width - border)
                    {
                        Application::Get().SetCursor(ECursorType::kSizeEW);
                        return HTRIGHT;
                    }
                    Application::Get().SetCursor(ECursorType::kArrow);
                    //kTitleBarHeight = 20.0f = _reserver_area[3];
                    if (pos.y > border && pos.y < _reserver_area[3] && pos.x > _reserver_area[0] + _reserver_area[2])
                        return HTCAPTION;
                }
            }
            break;
            case WM_SETCURSOR:
            {
                MouseSetCursorEvent e(0u);//暂时不使用事件里的光标，dock窗口无法产生这个事件，都使用Application::Get().SetCursor的值
                if (LOWORD(lParam) == HTCLIENT)
                {
                    Application::Get().SetCursor(ECursorType::kArrow);
                }
                else if (LOWORD(lParam) == HTLEFT || LOWORD(lParam) == HTRIGHT)
                {
                    Application::Get().SetCursor(ECursorType::kSizeEW);
                }
                else if (LOWORD(lParam) == HTTOP || LOWORD(lParam) == HTBOTTOM)
                {
                    Application::Get().SetCursor(ECursorType::kSizeNS);
                }
                else if (LOWORD(lParam) == HTTOPLEFT || LOWORD(lParam) == HTBOTTOMRIGHT)
                {
                    Application::Get().SetCursor(ECursorType::kSizeNWSE);
                }
                else if (LOWORD(lParam) == HTTOPRIGHT || LOWORD(lParam) == HTBOTTOMLEFT)
                {
                    Application::Get().SetCursor(ECursorType::kSizeNESW);
                }
                else if (LOWORD(lParam) == HTCAPTION)
                {
                    Application::Get().SetCursor(ECursorType::kArrow);
                }
                _data.Handler(e);
            }
        }
        // Handle any messages the switch statement didn't.
        //LOG_WARNING("Unhandled message: " + std::to_string(message));
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

#pragma warning(pop)
}// namespace Ailu
