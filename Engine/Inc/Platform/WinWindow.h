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
		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;
		void SetEventHandler(const EventHandler& handler) override;
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		void* GetNativeWindowPtr() const override;
	private:
		void Init(const WindowProps& prop);
		void Shutdown();
		static LRESULT WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT WindowProcImpl(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	private:
		struct WindowData
		{
			std::wstring Title;
			uint32_t Width, Height;
			bool VSync;
			EventHandler Handler;
		};
		WindowData _data;
		HWND _hwnd;
	};
}



#endif // !WIN_WINDOW_H__
