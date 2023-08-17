#include "pch.h"	
#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"
#include "Framework/Events/KeyEvent.h"
#include "RHI/DX12/BaseRenderer.h"

namespace Ailu
{
	static const wchar_t* kAppTitleIconPath = GET_ENGINE_FULL_PATHW(Res/Ico/app_title_icon.ico);
	static const wchar_t* kAppIconPath = GET_ENGINE_FULL_PATHW(Res/Ico/app_icon.ico);
    DXBaseRenderer* renderer = new DXBaseRenderer(1280, 720);
	static Window* Create(const WindowProps& props)
	{
		return new WinWindow(props);
	}
	WinWindow::WinWindow(const WindowProps& prop)
	{
		Init(prop);
	}

	WinWindow::~WinWindow()
	{
	}
    HWND WinWindow::GetWindowHwnd()
    {
        return _hwnd;
    }
    void WinWindow::Init(const WindowProps& prop)
	{
		_data.Width = prop.Width;
		_data.Height = prop.Height;
		_data.Title = prop.Title;
		LOG(L"Create window {}, ({},{})",prop.Title,prop.Width,prop.Height)
        // Initialize the window class.
        WNDCLASSEX windowClass = { 0 };
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = _sp_hinstance;
        windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        windowClass.lpszClassName = L"AiluEngineClass";
        RegisterClassEx(&windowClass);

        RECT windowRect = { 0, 0, static_cast<LONG>(prop.Width), static_cast<LONG>(prop.Height) };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
        // Create the window and store a handle to it.
        _hwnd = CreateWindow(
            windowClass.lpszClassName,
            prop.Title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,        // We have no parent window.
            nullptr,        // We aren't using menus.
            _sp_hinstance,
            nullptr);
        // Initialize the sample. OnInit is defined in each child-implementation of DXSample.
        //pSample->OnInit();

        auto a = ShowWindow(_hwnd, _s_argc);
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
        renderer->OnInit();
	}
	void WinWindow::OnUpdate()
	{
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

	}
	uint32_t WinWindow::GetWidth() const
	{
		return 0;
	}
	uint32_t WinWindow::GetHeight() const
	{
		return 0;
	}
	void WinWindow::SetEventHandler(const EventHandler& handler)
	{
	}
	void WinWindow::SetVSync(bool enabled)
	{
	}
	bool WinWindow::IsVSync() const
	{
		return false;
	}

	void WinWindow::Shutdown()
	{
        renderer->OnDestroy();
        delete renderer;
	}
    LRESULT WinWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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
            Event* e = new KeyPressedEvent(0, 1);
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
