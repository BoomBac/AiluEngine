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
	public:
		bool IsKeyPressedImpl(int keycode) override;
		bool IsMouseButtonPressedImpl(uint8_t button) override;
		float GetMouseXImpl() override;
		float GetMouseYImpl() override;
	private:
		WinInput();
		HWND _hwnd = nullptr;
		POINT _mouse_point;
	};
}


#endif // !WININPUT_H__