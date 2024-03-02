#include "pch.h"	
#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Platform/WinInput.h"

#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Utils.h"

#ifdef DEAR_IMGUI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // DEAR_IMGUI
namespace Ailu
{
	static const wchar_t* kAppTitleIconPath = ToWChar(kEngineResRootPath + "Ico/app_title_icon.ico");
	static const wchar_t* kAppIconPath = ToWChar(kEngineResRootPath + "Ico/app_icon.ico");

	static void HideCursor()
	{
		while (::ShowCursor(FALSE) >= 0); // 隐藏鼠标指针，直到它不再可见
	}

	static void ShowCursor()
	{
		while (::ShowCursor(TRUE) < 0); // 显示鼠标指针，直到它可见
	}

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

	void WinWindow::Init(const WindowProps& prop)
	{
		_data.Width = prop.Width;
		_data.Height = prop.Height;
		_data.Title = prop.Title;
		_data.Handler = [](Event& e) {
			LOG_INFO("Default Event Handle: {}", e.ToString())
				return true;
			};
		auto hinstance = GetModuleHandle(NULL);
		LOG_INFO(L"Create window {}, ({},{})", prop.Title, prop.Width, prop.Height)
			// Initialize the window class.
			WNDCLASSEX windowClass = { 0 };
		windowClass.cbSize = sizeof(WNDCLASSEX);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = WindowProc;
		windowClass.hInstance = hinstance;
		windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		windowClass.lpszClassName = L"AiluEngineClass";
		RegisterClassEx(&windowClass);

		RECT windowRect = { 0, 0, static_cast<LONG>(prop.Width), static_cast<LONG>(prop.Height) };
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
			nullptr,        // We have no parent window.
			nullptr,        // We aren't using menus.
			hinstance,
			this);

		//移除调整窗口大小的样式
		LONG_PTR style = GetWindowLongPtr(_hwnd, GWL_STYLE);
		style = style & ~WS_THICKFRAME;
		SetWindowLongPtr(_hwnd, GWL_STYLE, style);
		ShowWindow(_hwnd, SW_SHOW);
		UpdateWindow(_hwnd);

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
		WinInput::Create(_hwnd);
		DragAcceptFiles(_hwnd, true);
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
	void WinWindow::SetEventHandler(const EventHandler& handler)
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

	void* WinWindow::GetNativeWindowPtr() const
	{
		return static_cast<void*>(_hwnd);
	}

	void WinWindow::Shutdown()
	{

	}

	LRESULT WinWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
#ifdef DEAR_IMGUI
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
			return true;
#endif // DEAR_IMGUI

		//return DefWindowProc(hWnd, message, wParam, lParam);
		//if (message == WM_NCCREATE)
		//{
		//    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
		//    return TRUE;
		//}         
		return ((WinWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA))->WindowProcImpl(hWnd, message, wParam, lParam);
	}

#pragma warning(push)
#pragma warning(disable: 4244)
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
			HDROP hDrop = (HDROP)wParam;
			UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

			for (UINT i = 0; i < fileCount; ++i)
			{
				UINT pathLength = DragQueryFile(hDrop, i, NULL, 0);
				wchar_t* filePath = new wchar_t[pathLength + 1];
				DragQueryFile(hDrop, i, filePath, pathLength + 1);
				DragFileEvent e(WString{ filePath });
				_data.Handler(e);
				delete[] filePath;
			}

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
			u32 w = static_cast<u32>(lParam & 0XFFFF), h = static_cast<u32>(lParam >> 16);
			_data.Width = w;
			_data.Height = h;
			WindowResizeEvent e(w, h);
			_data.Handler(e);
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
			MouseButtonPressedEvent e(wParam);
			_data.Handler(e);
		}
		return 0;
		case WM_MBUTTONUP:
		{
			MouseButtonReleasedEvent e(wParam);
			_data.Handler(e);
		}
		return 0;
		case WM_LBUTTONDOWN:
		{
			MouseButtonPressedEvent e(wParam);
			_data.Handler(e);
		}
		return 0;
		case WM_LBUTTONUP:
		{
			MouseButtonReleasedEvent e(wParam);
			_data.Handler(e);
		}
		return 0;
		case WM_RBUTTONDOWN:
		{
			MouseButtonPressedEvent e(wParam);
			_data.Handler(e);
		}
		return 0;
		case WM_RBUTTONUP:
		{
			MouseButtonReleasedEvent e(wParam);
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
		}
		// Handle any messages the switch statement didn't.
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

#pragma warning(pop)
}
