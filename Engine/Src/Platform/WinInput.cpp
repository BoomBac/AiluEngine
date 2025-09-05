#include "pch.h"
#include "Platform/WinInput.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
	WinInput::WinInput()
	{
		_hwnd = static_cast<HWND>(Application::Get().GetWindow().GetNativeWindowPtr());
		_mouse_point = {0,0};
	}
	WinInput::WinInput(HWND hwnd)
	{
		_hwnd = hwnd;
		_mouse_point = { 0,0 };
	}
    bool WinInput::IsKeyPressed(EKey keycode)
	{
		return GetAsyncKeyState(keycode) & 0x8000;
	}
	WString WinInput::GetCharFromKeyCode(EKey key)
	{
		BYTE keyboardState[256];
		if (!GetKeyboardState(keyboardState))
			return L"";

		// 将虚拟键码转为扫描码
		UINT scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);

		// 接收字符输出
		WCHAR buffer[5] = {0};

		int result = ToUnicode(
				key,          // 虚拟键码
				scanCode,     // 扫描码
				keyboardState,// 当前键盘状态
				buffer,       // 输出的字符
				4,            // 缓冲区大小
				0             // 行为标志
		);

		if (result > 0)
			return std::wstring(buffer, result);

		return L"";
	}

	Vector2f WinInput::GetMousePos(Window *w)
	{
		GetCursorPos(&_mouse_point);// 获取鼠标的屏幕坐标
		POINT pt = {0, 0};
		if (w != nullptr)
			ClientToScreen((HWND) w->GetNativeWindowPtr(), &pt);
		else
			ClientToScreen(_hwnd, &pt);
		return Vector2f((f32) (_mouse_point.x - pt.x), (f32) (_mouse_point.y - pt.y));
	}

	Vector2f WinInput::GetGlobalMousePos()
	{
		GetCursorPos(&_mouse_point);// 获取鼠标的屏幕坐标
		return Vector2f((f32) (_mouse_point.x), (f32) (_mouse_point.y));
	}
}
