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
	struct WindowProps
	{
		std::wstring Title;
		u32 Width;
		u32 Height;
		WindowProps(const std::wstring& title = L"Ailu Engine",
			u32 width = 1600,
			u32 height = 900)
			: Title(title), Width(width) ,Height(height)
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
		/// @brief 获取客户区的屏幕坐标，包含标题栏
		/// @return 
		virtual std::tuple<i32, i32> GetWindowPosition() const = 0;
		virtual void SetEventHandler(const EventHandler& handler) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual void* GetNativeWindowPtr() const = 0;
		virtual void SetTitle(const WString& title) = 0;
	};
}


#endif // !WINDOW_H__
