#pragma once
#ifndef __WINDOW_H__
#define __WINDOW_H__

#include <string>
#include <tuple>
#include <functional>

#include "GlobalMarco.h"
#include "Framework/Events/Event.h"

namespace Ailu
{
	enum EWindowFlags
	{
		kWindow_NoTitleBar = 1 << 0,
		kWindow_NoResize = 1 << 1,
		kWindow_NoMove = 1 << 2,
	};
	struct WindowProps
	{
		std::wstring Title;
		u32 Width;
		u32 Height;
		u32 _flag;
		WindowProps(const std::wstring& title = L"Ailu Engine",
			u32 width = 1600,
			u32 height = 900)
			: Title(title), Width(width), Height(height), _flag(0u)
		{
		}
	};
	class AILU_API Window
	{
	public:
		using EventHandler = std::function<void(Event&)>;
		virtual ~Window() {};
		virtual void OnUpdate() = 0;
		virtual u32 GetWidth() const = 0;
		virtual u32 GetHeight() const = 0;
        // 直接设置整个窗口大小（包含边框/标题栏）
		virtual void SetWindowSize(u16 w,u16 h) = 0;
        // 根据期望的客户区大小调整整个窗口大小
		virtual void SetClientSize(u16 w,u16 h) = 0;
        virtual std::tuple<u16, u16> GetWindowSize() = 0;
        virtual std::tuple<u16, u16> GetClientSize() = 0;
		virtual void SetEventHandler(const EventHandler& handler) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual void* GetNativeWindowPtr() const = 0;
		virtual void SetTitle(const WString& title) = 0;
		virtual WString GetTitle() = 0;
		virtual std::tuple<i32, i32> GetClientPosition() const = 0;
		virtual std::tuple<i32, i32> GetWindowPosition() const = 0;
		virtual void SetPosition(i32 x, i32 y) = 0;
		//用于无边框窗口自绘标题栏，这个区域内当做客户区从而可以响应窗口事件，h一般就是标题栏高度
		//w则是标题文本框或者标签框宽度
        virtual std::tuple<f32, f32, f32, f32> ReserveArea() const = 0;
        virtual void ReserveArea(f32 x, f32 y, f32 w, f32 h) = 0;
	};

	class AILU_API WindowFactory
	{
	public:
		static Scope<Window> Create(const WString &title, u32 width, u32 height,u32 flags = 0u);
	};
}


#endif // !WINDOW_H__
