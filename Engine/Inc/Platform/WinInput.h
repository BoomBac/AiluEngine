#pragma once
#ifndef __WININPUT_H__
#define __WININPUT_H__
#include "Framework/Common/Input.h"

namespace Ailu
{
	//https://learn.microsoft.com/zh-CN/windows/win32/api/winuser/nf-winuser-getasynckeystate
	class WinInput : public InputPlatform
	{
	public:
		WinInput();
		WinInput(HWND hwnd);
	private:
		bool IsKeyPressed(EKey keycode) final;
        Vector2f GetMousePos(Window *w) final;
        Vector2f GetGlobalMousePos() final;
        WString GetCharFromKeyCode(EKey key) final;

		HWND _hwnd = nullptr;
		POINT _mouse_point;
	};
}


#endif // !WININPUT_H__
