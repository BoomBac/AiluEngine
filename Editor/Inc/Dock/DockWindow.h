#ifndef __DOCK_WINDOW_H__
#define __DOCK_WINDOW_H__
#include "UI/Widget.h"
namespace Ailu
{
    namespace UI
    {
        class Text;
        class Button;
        class Border;
    }
    namespace Editor
    {
        using UI::Widget;
        class DockWindow
        {
            friend class DockManager;
        public:
            inline static const f32 kTitleBarHeight = 20.0f;
            inline static const f32 kBorderThickness = 5.0f;// 边缘可拖拽区域
            Vector2f _position = Vector2f::kZero;
            u32 _resize_dir = 0;// 1:left, 2:right, 4:top, 8:bottom
        public:
            DockWindow(const String& title, Vector2f size);
            virtual ~DockWindow();
            void SetRect(Vector4f rect);
            void Update(f32 dt);

        private:
            inline static DockWindow* s_cur_resizing_window = nullptr;
        private:
            void UpdateResizeState(Vector2f mouse_pos, bool is_mouse_down);
        protected:
            Ref<Widget> _title_bar;// 可拖拽
            Ref<Widget> _content;  // Canvas / LinearBox
            Vector4f _rect;        // 当前位置和大小
            bool _is_focused;
            bool _is_dragging;
            bool _is_resizing;
            UI::Text *_title;
            UI::Button *_btn_close;
            UI::Border * _title_bar_root;
            UI::Border * _content_root;
            Vector2f _drag_start_pos;
            Vector2f _drag_start_mouse_pos;
            Vector2f _drag_start_offset;
            Vector2f _pre_mouse_pos;
        };
    }
}

#endif// !__DOCK_WINDOW_H__
