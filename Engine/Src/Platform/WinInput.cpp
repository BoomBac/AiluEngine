#include "pch.h"
#include "Platform/WinInput.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
	void WinInput::Create()
	{
		static WinInput input;
		sp_instance = &input;
		return;
	}
	void WinInput::Create(HWND hwnd)
	{
		static WinInput input(hwnd);
		sp_instance = &input;
		return;
	}
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
	bool WinInput::IsKeyPressedImpl(int keycode)
	{
		return GetAsyncKeyState(keycode) & 0x8000;
	}
	bool WinInput::IsMouseButtonPressedImpl(u8 button)
	{
		if(button == 0)
			return GetAsyncKeyState(VK_MBUTTON) & 0x8000;
		else if(button == 1)
			return GetAsyncKeyState(VK_LBUTTON) & 0x8000;
		else
			return GetAsyncKeyState(VK_RBUTTON) & 0x8000;
	}
	float WinInput::GetMouseXImpl()
	{
		return GetMousePosImpl().x;
	}
	float WinInput::GetMouseYImpl()
	{
		return GetMousePosImpl().y;
	}
	Vector2f WinInput::GetGlobalMousePosImpl()
	{
		GetCursorPos(&_mouse_point); // 获取鼠标的屏幕坐标
		return Vector2f((f32)(_mouse_point.x), (f32)(_mouse_point.y));
	}
	Vector2f WinInput::GetMousePosImpl()
	{
		GetCursorPos(&_mouse_point); // 获取鼠标的屏幕坐标
		POINT pt = { 0, 0 };
		ClientToScreen(_hwnd, &pt);
		return Vector2f((f32)(_mouse_point.x - pt.x), (f32)(_mouse_point.y - pt.y));
	}
}
