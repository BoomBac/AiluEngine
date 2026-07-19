#pragma once
#include "Objects/Serialize.h"
#include "UIStyles.h"
#include "generated/UITheme.gen.h"

namespace Ailu
{
    namespace UI
    {
        using UIStyleId = String;

        ACLASS()
        class AILU_API UITheme : public SerializeObject
        {
            GENERATED_BODY()

        public:
            UITheme();
            static UITheme DefaultDark();
            static UITheme DefaultLight();

            u64 Revision() const { return _revision; }
            void BumpRevision() { ++_revision; }
            void PostDeserialize() override;

        public:
            const UIButtonStyle *FindButtonStyle(const UIStyleId &style_id) const;
            void SetButtonStyle(const UIStyleId &style_id, const UIButtonStyle &style);

            const UISliderStyle *FindSliderStyle(const UIStyleId &style_id) const;
            void SetSliderStyle(const UIStyleId &style_id, const UISliderStyle &style);

            const UICheckBoxStyle *FindCheckBoxStyle(const UIStyleId &style_id) const;
            void SetCheckBoxStyle(const UIStyleId &style_id, const UICheckBoxStyle &style);

            const UIInputStyle *FindInputStyle(const UIStyleId &style_id) const;
            void SetInputStyle(const UIStyleId &style_id, const UIInputStyle &style);

        public:
            APROPERTY()
            UIColorTokens _colors;

            APROPERTY()
            UISpacingTokens _spacing;

            APROPERTY()
            UITypographyTokens _typography;

            APROPERTY()
            UIButtonStyle _button_style;

            APROPERTY()
            UISliderStyle _slider_style;

            APROPERTY()
            UICheckBoxStyle _check_box_style;

            APROPERTY()
            UIInputStyle _input_style;

            APROPERTY()
            UIScrollViewStyle _scroll_view_style;

        private:
            explicit UITheme(bool init_default);

        private:
            APROPERTY()
            HashMap<UIStyleId, UIButtonStyle> _button_styles;

            APROPERTY()
            HashMap<UIStyleId, UISliderStyle> _slider_styles;

            APROPERTY()
            HashMap<UIStyleId, UICheckBoxStyle> _check_box_styles;

            APROPERTY()
            HashMap<UIStyleId, UIInputStyle> _input_styles;

            u64 _revision = 1u;
        };
    }// namespace UI
}// namespace Ailu
