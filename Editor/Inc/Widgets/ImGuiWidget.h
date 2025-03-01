#pragma once
#ifndef __IMGUIWIDGET_H__
#define __IMGUIWIDGET_H__
#include "Framework/Events/Event.h"
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Objects/Type.h"
#include <functional>
#include <mutex>

namespace Ailu
{
    namespace Editor
    {
        void DrawMemberProperty(PropertyInfo &prop_info, Object &obj);
        u32 DrawEnum(const Enum* enum_type,u32 enum_value);
#define TEXTURE_HANDLE_TO_IMGUI_TEXID(handle) reinterpret_cast<void *>(handle)
        class ImGuiWidget
        {
            DECLARE_PROTECTED_PROPERTY(is_focus, Focus, bool)
            using WidgetCloseEvent = std::function<void(void)>;

        public:
            inline static const String kDragFolderType = "DRAG_FOLDER_INNEAR";
            inline static const String kDragFolderTreeType = "DRAG_FOLDER_TREE_INNEAR";
            inline static const String kDragAssetType = "DRAG_ASSET_INNEAR";
            inline static const String kDragAssetMesh = "DRAG_ASSET_MESH";
            inline static const String kDragAssetMaterial = "DRAG_ASSET_MATERIAL";
            inline static const String kDragAssetTexture2D = "DRAG_ASSET_TEXTURE2D";
            inline static const String kDragAssetSkeletonMesh = "DRAG_ASSET_SKELETON";
            inline static const String kDragAssetAnimClip = "DRAG_ASSET_ANIM_CLIP";
            inline static const WString kNull = L"null";

            inline static bool s_global_modal_window_info[256]{false};
            static void MarkModalWindoShow(const int &handle);
            static String ShowModalDialog(const String &title, int handle = 0);
            static String ShowModalDialog(const String &title, std::function<void()> show_widget, int handle = 0);
            static u32 DisplayProgressBar(const String &title, f32 progress);
            static void RemoveProgressBar(u32 handle);
            static void ClearProgressBar();
            static void ShowProgressBar();
            static void SetFocus(const String &title);
            static void EndFrame() { s_global_widegt_handle = 0; }
            static u32 GetGlobalWidgetHandle() { return s_global_widegt_handle++; }

            static void OnObjectDropdown(const String &tag, std::function<void(Object *)> f);

            ImGuiWidget(const String &title);
            ~ImGuiWidget();
            virtual void Open(const i32 &handle);
            virtual void Close(i32 handle);

            i32 Handle() const { return _handle; }
            bool IsCaller(const i32 &handle) const { return _handle == handle; }
            void Show();
            bool Hover(const Vector2f &mouse_pos) const;
            //transform window pos to windget pos
            Vector2f GlobalToLocal(const Vector2f &screen_pos) const;
            Vector2f Size() const { return _size; }
            Vector2f Position() const { return _pos; }
            Vector2f ContentSize() const { return _content_size; }
            Vector2f ContentPosition() const { return _content_pos; }
            //start at screen left top
            Vector2f GlobalPostion() const { return _screen_pos; }
            virtual void OnEvent(Event &e);

        public:
            String _title;

        protected:
            virtual void ShowImpl();
            void OnWidgetClose(WidgetCloseEvent e);

        protected:
            //local pos,reletive to window
            Vector2f _pos, _size;
            Vector2f _content_pos, _content_size, _screen_pos;
            i32 _handle = -1;
            bool _b_show = false;
            bool _b_pre_show = false;
            bool _is_hide_common_widget_info = true;
            bool _allow_close = false;

        private:
            inline static u32 s_global_widegt_handle = 0u;
            inline static List<std::tuple<String, f32>> s_progress_infos{};
            inline static std::mutex s_progress_lock{};
            List<WidgetCloseEvent> _close_events;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !IMGUIWIDGET_H__
