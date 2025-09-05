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
        void SetWindowSize(u16 w, u16 h) final;
        void SetClientSize(u16 w, u16 h) final;
        std::tuple<u16, u16> GetWindowSize() final;
        std::tuple<u16, u16> GetClientSize() final;
		void SetEventHandler(const EventHandler& handler) override;
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;
		void* GetNativeWindowPtr() const override;
		std::tuple<i32, i32> GetClientPosition() const final;
        std::tuple<i32, i32> GetWindowPosition() const final;
		void SetTitle(const WString& title) final;
        WString GetTitle() final;
        void SetPosition(i32 x, i32 y);
        std::tuple<f32, f32, f32, f32> ReserveArea() const final;
        void ReserveArea(f32 x, f32 y, f32 w, f32 h) final;
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
            u32 _flags;
		};
		WindowData _data;
		HWND _hwnd;
        bool _is_focused;
        Array<f32, 4u> _reserver_area;
	};
}



#endif // !WIN_WINDOW_H__
