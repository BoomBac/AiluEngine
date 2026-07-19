#ifndef __DOCK_WINDOW_H__
#define __DOCK_WINDOW_H__
#include "UI/Widget.h"
#include "Objects/Object.h"
#include "UI/InteractionZone.h"
#include "generated/DockWindow.gen.h"
namespace Ailu
{
    class JsonArchive;
    class Window;
    namespace UI
    {
        class Text;
        class Button;
        class Border;
        class HorizontalBox;
    }
    namespace Editor
    {
        class DockManager;
        enum EHoverEdgeDir
        {
            kLeft = 1,
            kRight = 4,
            kTop = 2,
            kBottom = 8
        };
        enum EDockWindowFlag
        {
            kNoTitleBar = 1 << 0,
            kNoResize = 1 << 1,
            kNoMove = 1 << 2,
            kNoCollapse = 1 << 3,
            kFullSize = 1 << 4//窗口内容区撑满整个窗口
        };

        class DockWindow;
        class IDockTabItem
        {
        public:
            virtual ~IDockTabItem() = default;
            virtual String GetTitle() const = 0;
            virtual Vector2f Position() const = 0;
            virtual Vector2f Size() const = 0;
            virtual void SetRect(Vector4f rect) = 0;
            virtual void Update(f32 dt) = 0;
            virtual bool IsHover(Vector2f pos) const = 0;
            virtual void SetFocus(bool is_focus) = 0;
            virtual void SetTabActive(bool is_active) = 0;
            virtual void RestoreStandaloneFromTab() = 0;
            virtual bool ContainsWindow(DockWindow *w) const = 0;
            virtual DockWindow *PrimaryWindow() const = 0;
            virtual void AttachToWindow(Window *w) = 0;
        };

        using UI::Widget;
        ACLASS()
        class DockWindow : public Object, public IDockTabItem
        {
            GENERATED_BODY()
            friend class DockManager;
            DECLARE_DELEGATE(on_size_change, Vector2f);
            DECLARE_DELEGATE(on_get_focus, DockWindow*);
            DECLARE_DELEGATE(on_lost_focus, DockWindow*);
        public:
            inline static const f32 kTitleBarHeight = 20.0f;
            inline static const f32 kBorderThickness = 5.0f;// 边缘可拖拽区域
            inline static const Vector2f kMinSize = {100.0f, 100.0f};// 最小窗口大小
            u32 _resize_dir = 0;// 1:left, 2:right, 4:top, 8:bottom
        public:
            static u32 HoverEdge(Vector2f pos, Vector2f size, Vector2f mpos, f32 thickness = kBorderThickness);
            DockWindow();
            DockWindow(const String &title, Vector2f size = {640.0f,360.0f});
            virtual ~DockWindow();
            void SetRect(Vector4f rect);
            virtual void Update(f32 dt);
            bool IsHover(Vector2f pos) const;
            void SetTitleBarVisibility(bool is_visibility,bool is_expand_content = false);
            void SetContentVisibility(bool is_visibility);
            String GetTitle() const;
            void SetTitle(String title);
            void SetFocus(bool is_focus);
            bool IsFocus() const { return _is_focused; }
            /// <summary>
            /// 获取鼠标悬浮边缘情况
            /// </summary>
            /// <param name="pos"></param>
            /// <returns>combine of EHoverEdgeDir</returns>
            u32 HoverEdge(Vector2f pos) const;
            Vector4f DragArea() const;
            u32 _flags = 0u;
            Widget *ContentWidget() { return _content_widget.get(); };
            Widget *TitleWidget() { return _title_widget.get(); };
            Ref<Widget> ContentWidgetRef() const { return _content_widget; };
            Ref<Widget> TitleWidgetRef() const { return _title_widget; };
            Vector2f Position() const override { return _position; };
            Vector2f Size() const override { return _size; };
            void SetSize(Vector2f size);
            void SetPosition(Vector2f position);
            void SetTabActive(bool is_active) override;
            void RestoreStandaloneFromTab() override;
            bool ContainsWindow(DockWindow *w) const override { return this == w; }
            DockWindow *PrimaryWindow() const override { return const_cast<DockWindow *>(this); }
            void AttachToWindow(Window *w) override;
            virtual void SaveDockLayoutState(JsonArchive &ar) {}
            virtual void LoadDockLayoutState(JsonArchive &ar) {}
            virtual void OnDockLayoutLoaded() {}
        private:
            inline static DockWindow* s_cur_resizing_window = nullptr;
        private:
            void UpdateResizeState(Vector2f mouse_pos, bool is_mouse_down);
        protected:
            Vector2f _position, _size;
            Ref<Widget> _title_widget;// 可拖拽
            Ref<Widget> _content_widget;  // Canvas / LinearBox
            bool _is_focused;
            bool _is_dragging;
            bool _is_resizing;
            UI::Text *_title;
            UI::Button *_btn_close;
            UI::Border * _title_bar_root;
            UI::Border *_title_drag_area;
            UI::Border * _content_root;
            Vector2f _drag_start_pos;
            Vector2f _drag_start_mouse_pos;
            Vector2f _drag_start_offset;
            Vector2f _pre_mouse_pos;
            bool _is_dirty = true;
            bool _is_title_bar_visible = true;
            bool _is_expand_content_when_title_hidden = false;
            Vector<Ailu::UI::ZoneHandle> _resize_zone_handles;
        };

        class DockTab
        {
        public:
            DockTab();
            ~DockTab();
            bool AddTab(const Ref<DockWindow> &w);
            bool AddTabItem(const Ref<IDockTabItem> &item);
            /// <summary>
            /// 移除一个标签，返回移除后是否为空
            /// </summary>
            /// <param name="w"></param>
            /// <returns></returns>
            bool RemoveTab(DockWindow *w);
            Ref<IDockTabItem> RemoveActiveItem();
            void Update(f32 dt);
            void SetVisible(bool is_visible);
            /// <summary>
            /// 获取鼠标悬浮边缘情况
            /// </summary>
            /// <param name="pos"></param>
            /// <returns>combine of EHoverEdgeDir</returns>
            u32 HoverEdge(Vector2f pos) const;
            bool HoverDragArea(Vector2f pos) const;
            bool IsHover(Vector2f pos) const;
            Ref<IDockTabItem> ActiveItem() const;
            DockWindow *ActivePrimaryWindow() const;
            i32 ActiveIndex() const { return _active_index; }
            i32 TabCount() const { return static_cast<i32>(_tabs.size()); }
            void SetActiveIndex(i32 new_index);
            bool Contains(DockWindow *w) const;
            void SetFocus(bool is_focus);
            bool IsFocus() const { return _is_focused; }
            auto begin() { return _tabs.begin(); }
            auto end() { return _tabs.end(); }
            Widget *TitleWidget() { return _tab_bar.get(); }
        public:
            Vector2f _position, _size;
            u32 _flags = 0u;

        private:
            bool IsActiveTab(UI::UIElement *e) const;
            void OnActiveTabChanged(i32 new_index);
        private:
            Vector<Ref<IDockTabItem>> _tabs;
            int _active_index = -1;
            Ref<Widget> _tab_bar;
            UI::Border *_tab_root;
            UI::HorizontalBox *_tab_hb;
            UI::HorizontalBox *_tab_titles;
            UI::Button *_btn_close;
            UI::Border *_drag_area;
            bool _is_focused;
            bool _is_visible = true;
            i32 _content_focus_handle = -1;//保存当前活跃window content foucs 代理句柄，用于点击窗口内容区使标签栏也获得焦点
        };
    }
}

#endif// !__DOCK_WINDOW_H__
