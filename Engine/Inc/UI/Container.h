#ifndef __UI_CONTAINER_H__
#define __UI_CONTAINER_H__
#include "UIElement.h"
#include "generated/Container.gen.h"

namespace Ailu
{
    namespace UI
    {
        ACLASS()
        class AILU_API Canvas : public UIElement
        {
            GENERATED_BODY()
        public:
            Canvas();
            ~Canvas();
            Vector2f MeasureDesiredSize() override;

        private:
            void MeasureAndArrange(f32 dt) override;
            void RenderImpl(UIRenderer &r) final;
        };

        ACLASS()
        class AILU_API LinearBox : public UIElement
        {
            GENERATED_BODY()
        public:
            enum class EOrientation
            {
                kHorizontal,
                kVertical
            };
            LinearBox(EOrientation orientation = EOrientation::kHorizontal);
            Vector2f MeasureDesiredSize() override;
        private:
            void MeasureAndArrange(f32 dt) override;
            void RenderImpl(UIRenderer &r) override;
        private:
            EOrientation _orientation;
        };


        ACLASS()
        class AILU_API VerticalBox : public LinearBox
        {
            GENERATED_BODY()
        public:
            VerticalBox() :LinearBox(EOrientation::kVertical) {};
            ~VerticalBox() = default;
        };

        ACLASS()
        class AILU_API HorizontalBox : public LinearBox
        {
            GENERATED_BODY()
        public:
            HorizontalBox() : LinearBox(EOrientation::kHorizontal) {};
            ~HorizontalBox() = default;
        };

        ACLASS()
        class AILU_API ScrollView : public UIElement
        {
            GENERATED_BODY()
        public:
            ScrollView();
            virtual ~ScrollView() = default;
            void PreUpdate(f32 dt) override;
            void SetViewportHeight(f32 height) { _slot._size.y = height; }
            void SetViewportWidth(f32 w) { _slot._size.x = w; }
            Vector2f MeasureDesiredSize() override;
        protected:
            void RenderImpl(UIRenderer &r) override;
            void PostDeserialize() override;
            void MeasureAndArrange(f32 dt) override;
            void PostArrange() override;
        protected:
            inline static const f32 kScrollBarWidth = 6.0f;
            Vector2f _current_offset = Vector2f::kZero;
            Vector2f _target_offset = Vector2f::kZero, _max_offset = Vector2f::kZero;
            Vector2f _content_size;
            Vector4f _vbar_rect,_hbar_rect;
            bool _is_hover_vbar = false, _is_hover_hbar;
            bool _is_dragging_bar = false;
            Vector2f _drag_start_mouse;     // 鼠标按下时位置(全局)
            f32 _drag_start_offset = 0.0f;// 按下时的_scroll_offset
            f32 _scroll_speed = 10.0f;
            bool _is_vertical = true;
        };

        ACLASS()
        class AILU_API ListView : public ScrollView
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_item_clicked, UIElement *, i32);
        public:
            ListView();
            ~ListView() = default;

            void AddItem(Ref<UIElement> item);
            void ClearItems();
            void SizeToContent(bool enable);
            bool IsSizeToContent() const { return _is_size_to_content; }
            Vector2f MeasureDesiredSize() override;
        protected:
            APROPERTY()
            bool _is_size_to_content = false;// If true, the list will resize to fit its content
            void RenderImpl(UIRenderer &r) final override;
        private:
            VerticalBox *_content_box = nullptr;// 用于布局子项
            UIElement *_hovered_item = nullptr;
            UIElement *_selected_item = nullptr;
        };

        class Text;
        class Button;
        ACLASS()
        class Dropdown : public UIElement
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_selected_changed, i32);

        public:
            Dropdown();
            void SetSelectedIndex(i32 index);
            int GetSelectedIndex() const { return _selected_index; }
            String GetSelectedText() const;

        private:
            void RenderImpl(UIRenderer &r) final;
            void PostDeserialize() final;
            UIElement *HitTest(Vector2f pos) final;

        private:
            APROPERTY()
            Vector<String> _items;
            i32 _selected_index = -1;
            Vector4f _button_rect;
            Ref<HorizontalBox> _root;
            Text *_text;
            Button *_button;
            bool _is_dropdown_open = false;
        };

        ACLASS()
        class AILU_API CollapsibleView : public UIElement
        {
            GENERATED_BODY()
        public:
            inline static f32 s_header_height = 24.0f;
        public:
            //仅供反序列化调用，实际使用请调用带title的构造函数
            CollapsibleView();
            explicit CollapsibleView(String title);
            void Update(f32 dt) override;
            void SetCollapsed(bool collapsed, bool animated = false);
            bool IsCollapsed() const { return _is_collapsed; }
            Vector2f MeasureDesiredSize() override;
            // 子容器，用于放置用户内容
            UIElement* GetContent() { return _content; }
            void SetTitle(const String &title);
            String GetTitle() const;
        private:
            void RenderImpl(UIRenderer &r) override;
            void PostDeserialize() override;
            void MeasureAndArrange(f32 dt) override;
        private:
            UIElement* _header; // 标题栏（内部有 Text + Icon/Button）
            UIElement* _content;// 折叠区的内容
            Text *_title;
            APROPERTY()
            String _title_text;
            APROPERTY()
            bool _is_collapsed = false;
            bool _is_animated = true;
            f32 _anim_progress = 1.0f;// 0.0=折叠 1.0=展开
        };

        ACLASS()
        class AILU_API SplitView : public UIElement
        {
            GENERATED_BODY()
        public:
            inline static const f32 kSplitBarThickness = 2.0f;
            SplitView();
            void Update(f32 dt) override;
        private:
            void RenderImpl(UIRenderer &r) override;
            void PostDeserialize() override;
            void MeasureAndArrange(f32 dt) override;
        private:
            APROPERTY()
            f32 _ratio = 0.5f;
            APROPERTY()
            bool _is_horizontal = true;
            bool _is_dragging_bar = false;
            bool _is_hover_bar = false;
        };
    }
}// namespace Ailu
#endif// !__UI_CONTAINER_H__