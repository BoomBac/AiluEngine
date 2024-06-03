#pragma once
#ifndef __IMGUIWIDGET_H__
#define __IMGUIWIDGET_H__
#include <functional>
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
#define TEXTURE_HANDLE_TO_IMGUI_TEXID(handle) reinterpret_cast<void*>(handle)
	class ImGuiWidget
	{
		DECLARE_PROTECTED_PROPERTY(is_focus,Focus,bool)
		using WidgetCloseEvent = std::function<void(void)>;
	public:
		inline static const String kDragFolderType = "DRAG_FOLDER_INNEAR";
		inline static const String kDragFolderTreeType = "DRAG_FOLDER_TREE_INNEAR";
		inline static const String kDragAssetType = "DRAG_ASSET_INNEAR";
		inline static const String kDragAssetMesh = "DRAG_ASSET_MESH";

		inline static WString kNull = L"null";
		inline static bool s_global_modal_window_info[256]{ false };
		static void MarkModalWindoShow(const int& handle);
		static String ShowModalDialog(const String& title, int handle = 0);
		static String ShowModalDialog(const String& title, std::function<void()> show_widget, int handle = 0);
		static void SetFocus(const String& title);
		static void EndFrame() { s_global_widegt_handle = 0; }

		ImGuiWidget(const String& title);
		~ImGuiWidget();
		virtual void Open(const i32& handle);
		virtual void Close(i32 handle);
		i32 Handle() const { return _handle; }
		bool IsCaller(const i32& handle) const { return _handle == handle; }
		void Show();
		Vector2f _pos, _size;
		static u32 GetGlobalWidgetHandle() { return s_global_widegt_handle++; }
	public:
		String _title;
	protected:
		virtual void ShowImpl();
		void OnWidgetClose(WidgetCloseEvent e);
	protected:
		i32 _handle = -1;
		bool _b_show = false;
		bool _b_pre_show = false;
		bool _is_hide_common_widget_info = false;
		bool _allow_close = true;
	private:
		inline static u32 s_global_widegt_handle = 0u;
		List<WidgetCloseEvent> _close_events;
	};
}

#endif // !IMGUIWIDGET_H__

