#pragma once
#ifndef __WININPUT_H__
#define __WININPUT_H__
#include "Framework/Common/Input.h"

namespace Ailu
{
	//https://learn.microsoft.com/zh-CN/windows/win32/api/winuser/nf-winuser-getasynckeystate
	class WinInput : public Input
	{
	public:
		static void Create();
		static void Create(HWND hwnd);
	public:
		bool IsKeyPressedImpl(int keycode) final;
		bool IsMouseButtonPressedImpl(u8 button) final;
		float GetMouseXImpl() final;
		float GetMouseYImpl() final;
		Vector2f GetMousePosImpl() final;
		Vector2f GetGlobalMousePosImpl() final;
	private:
		WinInput();
		WinInput(HWND hwnd);
		HWND _hwnd = nullptr;
		POINT _mouse_point;
	};
}


#endif // !WININPUT_H__
