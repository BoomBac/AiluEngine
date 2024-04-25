#pragma once
#ifndef __IMGUIWIDGET_H__
#define __IMGUIWIDGET_H__
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
namespace Ailu
{
#define TEXTURE_HANDLE_TO_IMGUI_TEXID(handle) reinterpret_cast<void*>(handle)
	class ImGuiWidget
	{
		DECLARE_PROTECTED_PROPERTY(is_focus,Focus,bool)
	public:
		inline static WString kNull = L"null";
		inline static bool s_global_modal_window_info[256]{ false };
		static void MarkModalWindoShow(const int& handle);
		static String ShowModalDialog(const String& title, int handle = 0);
		static String ShowModalDialog(const String& title, std::function<void()> show_widget, int handle = 0);

		ImGuiWidget(const String& title);
		~ImGuiWidget();
		virtual void Open(const i32& handle);
		virtual void Close();
		bool IsCaller(const i32& handle) const { return _handle == handle; }
		void Show();
		Vector2f _left_top, _right_bottom;
	public:
		String _title;
	protected:
		i32 _handle = -1;
		bool _b_show = false;
		virtual void ShowImpl();
	};
}

#endif // !IMGUIWIDGET_H__

