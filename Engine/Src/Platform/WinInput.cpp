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
		_hwnd = static_cast<HWND>(Application::GetInstance()->GetWindow().GetNativeWindowPtr());
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
		GetCursorPos(&_mouse_point); // 获取鼠标的屏幕坐标
		//ScreenToClient(_hwnd, &_mouse_point); // 将屏幕坐标转换为客户区坐标
		int mouseY = _mouse_point.y; // 鼠标的Y坐标
		return static_cast<float>(_mouse_point.x);
	}
	float WinInput::GetMouseYImpl()
	{
		GetCursorPos(&_mouse_point); // 获取鼠标的屏幕坐标
		//ScreenToClient(_hwnd, &_mouse_point); // 将屏幕坐标转换为客户区坐标
		return static_cast<float>(_mouse_point.y);
	}
	Vector2f WinInput::GetMousePosImpl()
	{
		GetCursorPos(&_mouse_point); // 获取鼠标的屏幕坐标
		//ScreenToClient(_hwnd, &_mouse_point); // 将屏幕坐标转换为客户区坐标
		return Vector2f((float)_mouse_point.x, (float)_mouse_point.y);
	}
}
