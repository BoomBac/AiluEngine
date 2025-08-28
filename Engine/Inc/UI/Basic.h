#ifndef __UI_BASIC_H__
#define __UI_BASIC_H__
#include "UIElement.h"
#include "generated/Basic.gen.h"
namespace Ailu
{
    namespace UI
    {
        ACLASS()
        class AILU_API Button : public UIElement
        {
            GENERATED_BODY()
        public:
            Button();
            explicit Button(const String &name);
            void Update(f32 dt) override;
            void Render(UIRenderer &r) override;
        };

        ACLASS()
        class AILU_API Text : public UIElement
        {
            GENERATED_BODY()
        public:
            Text();
            explicit Text(const String &name);
            void Update(f32 dt) override;
            void Render(UIRenderer &r) override;

        public:
            APROPERTY()
            String _text;
            APROPERTY()
            u16 _font_size = 14u;
            APROPERTY()
            Color _color = Colors::kWhite;
        };

        ACLASS()
        class AILU_API Slider : public UIElement
        {
            GENERATED_BODY()
        public:
            Slider();
            explicit Slider(const String &name);
            void Update(f32 dt) override;
            void Render(UIRenderer &r) override;
            UIElement *HitTest(Vector2f pos) final;
        public:
            APROPERTY()
            Vector2f _range = Vector2f(0.0f, 1.0f);
            APROPERTY()
            f32 _value = 0.0f;
            APROPERTY()
            u32 _font_size = 12u;
            Vector4f _bar_rect,_dot_rect;
        };

        ACLASS()
        class AILU_API CheckBox : public UIElement
        {
            GENERATED_BODY()
        public:
            CheckBox();
            void Render(UIRenderer &r) override;
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
            void Update(f32 dt) final;
            void Render(UIRenderer &r) final;
        public:
            APROPERTY()
            Color _bg_color = Colors::kGray;
            APROPERTY()
            Color _border_color = Colors::kWhite;
            APROPERTY()
            f32 _thickness = 1.0f;
        };
    }// namespace UI
}// namespace Ailu

#endif//__UI_BASIC_H__