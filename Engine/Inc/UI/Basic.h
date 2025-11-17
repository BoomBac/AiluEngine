#ifndef __UI_BASIC_H__
#define __UI_BASIC_H__
#include "UIElement.h"
#include "generated/Basic.gen.h"
namespace Ailu
{
    namespace Render
    {
        class Texture;
        struct Font;
    }

    namespace UI
    {
        class Text;
        class Image;
        ACLASS()
        class AILU_API Button : public UIElement
        {
            GENERATED_BODY()
        public:
            Button();
            explicit Button(const String &name);
            Vector2f MeasureDesiredSize() override;
            void SetText(const String &text);
            String GetText() const;
            void SetTexture(Render::Texture *tex);
            Render::Texture *GetTexture() const;
        private:
            void RenderImpl(UIRenderer &r) override;
            void PostArrange() override;
        private:
            Text *_text;
            Image *_icon;
        };

        ACLASS()
        class AILU_API Text : public UIElement
        {
            DECLARE_DELEGATE(on_text_change, String);
            GENERATED_BODY()
        public:
            Text();
            explicit Text(String text);
            void SetText(const String &text);
            const String &GetText() const { return _text; }
            Vector2f MeasureDesiredSize() override;
            f32 FontSize() const { return _font_size; }
            void FontSize(f32 size);
        private:
            void RenderImpl(UIRenderer &r) override;
            void PostDeserialize() override;
        public:
            APROPERTY()
            Color _color = Colors::kWhite;
            APROPERTY()
            EAlignment _horizontal_align = EAlignment::kLeft;
            APROPERTY()
            EAlignment _vertical_align = EAlignment::kCenter;
        private:
            APROPERTY()
            f32 _font_size = 14.0f;
            APROPERTY()
            String _text;
            Vector2f _text_size;
        };

        ACLASS()
        class AILU_API Slider : public UIElement
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_value_change, f32);
        public:
            Slider();
            explicit Slider(const String &name);
            Slider(f32 min, f32 max, f32 value);
            void Update(f32 dt) override;
            UIElement *HitTest(Vector2f pos) final;
            Vector2f MeasureDesiredSize() override;
            f32 GetValue() const { return _value; }
            void SetValue(f32 v);
        private:
            void RenderImpl(UIRenderer &r) override;
        public:
            APROPERTY()
            Vector2f _range = Vector2f(0.0f, 1.0f);

        private:
            Vector4f _bar_rect,_dot_rect;
            APROPERTY()
            f32 _value = 0.0f;
        };

        ACLASS()
        class AILU_API CheckBox : public UIElement
        {
            GENERATED_BODY()
        public:
            CheckBox();
            Vector2f MeasureDesiredSize() override;

        private:
            void RenderImpl(UIRenderer &r) override;
        public:
            APROPERTY()
            bool _is_checked = false;
        };
        /*
        具有背景的UI元素包装控件，一般具有一个元素
        */
        ACLASS()
        class AILU_API Border : public UIElement
        {
            GENERATED_BODY()
        public:
            Border();
            Vector2f MeasureDesiredSize() override;
            void Thickness(f32 thickness) { _thickness = Vector4f{thickness}; };
            void Thickness(Vector4f ltrb) { _thickness = ltrb; };
            Vector4f Thickness() const { return _thickness; }
        private:
            void RenderImpl(UIRenderer &r) final;
            void MeasureAndArrange(f32 dt) override;
            void PostDeserialize() override;
        public:
            APROPERTY()
            Color _bg_color = Colors::kGray;
            APROPERTY()
            Color _border_color = Colors::kWhite;
        protected:
            APROPERTY()
            Vector4f _thickness = Vector4f::kZero;
        };

        ACLASS()
        class AILU_API InputBlock : public UIElement
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_content_changed, String);
        public:
            InputBlock();
            InputBlock(const String &content);
            void Update(f32 dt) final;
            void SetContent(String content);
            Vector2f MeasureDesiredSize() override;
            bool IsEditing() const { return _is_editing; }
        private:
            void RenderImpl(UIRenderer &r) final;
            void FillCursorOffsetTable();
            u32 IndexFromMouseX(f32 x);
            void ClearSelection(){_select_start = _select_end = _cursor_pos;}
            void CommitEdit(bool is_finish_edit = true);
        private:
            inline static f32 kCursorXOffset = 4.0f;
            inline static f32 kCursorWidth = 2.0f;
            APROPERTY()
            String _content;
            u32 _cursor_pos = 0u;  // 光标位置
            bool _is_selecting;    // 是否正在选中
            u32 _select_start = 0u;// 选区开始
            u32 _select_end = 0u;  // 选区结束
            bool _cursor_visible = true;// 当前是否绘制光标
            bool _is_drag_adjusting = false;// 是否正在拖动调整纯数值文本
            f32 _drag_start_value = 0.0f;  // 纯数值文本开始拖动时的数值
            f32 _drag_start_x = 0.0f;
            bool _is_numeric = false;        // 当前文本是否为纯数值
            f32 _cursor_timer = 0.0f;   // 闪烁计时
            f32 _blink_interval = 0.5f; // 闪烁周期
            Vector<f32> _cursor_offsets;
            Vector2f _text_rect_size;
            bool _is_editing = false;
        };

        ACLASS()
        class AILU_API Image : public UIElement
        {
            GENERATED_BODY()
        public:
            Image();
            explicit Image(Render::Texture *texture);
            Vector2f MeasureDesiredSize() override;
            void SetTexture(Render::Texture* tex);
            Render::Texture *GetTexture() const { return _texture; }
            void PostDeserialize() override;
        private:
            void RenderImpl(UIRenderer &r) override;
        public:
            APROPERTY()
            Color _tint_color = Colors::kWhite;
            Render::Texture *_texture = nullptr;
            Vector2f _tex_size = Vector2f::kZero;
            APROPERTY()
            String _texture_guid;
        };
    }// namespace UI
}// namespace Ailu

#endif//__UI_BASIC_H__