#pragma once
#ifndef __WIN_WINDOW_H__
#define __WIN_WINDOW_H__
#include "Framework/Common/Window.h"
namespace Ailu
{
	class AILU_API WinWindow : public Window
	{
	public:
		WinWindow(const WindowProps& prop);
		virtual ~WinWindow();
		void OnUpdate() override;
		u32 GetWidth() const override;
		u32 GetHeight() const override;
		void SetEventHandler(const EventHandler& handler) override;
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		void* GetNativeWindowPtr() const override;
		std::tuple<u32, u32> GetWindowPosition() const override;
	private:
		void Init(const WindowProps& prop);
		void Shutdown();
		static LRESULT WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT WindowProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	private:
		struct WindowData
		{
			std::wstring Title;
			u32 Width, Height;
			bool VSync;
			EventHandler Handler;
		};
		WindowData _data;
		HWND _hwnd;
        bool _is_focused;
	};
}



#endif // !WIN_WINDOW_H__
