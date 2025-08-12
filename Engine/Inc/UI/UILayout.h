#ifndef __UI_LAYOUT_H__
#include "Framework/Math/ALMath.hpp"
#include "UIStyle.h"
#include "generated/UILayout.gen.h"

namespace Ailu
{
    namespace UI
    {
        class Panel;
        class UILayout
        {
        public:
            virtual ~UILayout() = default;
            virtual void UpdateLayout(Panel *panel) = 0;
        };

        AENUM()
        enum class EAlignment
        {
            kLeft,
            kCenter,
            kRight,
            kTop,
            kBottom
        };

        enum class ELayoutFillMode
        {
            kKeepSelf,
            kFitParent,
            kPercentParent
        };

        class VerticalLayout : public UILayout
        {
        public:
            void UpdateLayout(Panel *panel) final;

        public:
            EAlignment _align = EAlignment::kCenter;
            f32 _spacing = UIStyleSheet::_vertical_spacing;
            Vector4f _padding = UIStyleSheet::_layout_padding;//LTRB
            f32 _percent = 0.0f;
            ELayoutFillMode _fill_mode = ELayoutFillMode::kPercentParent;
        };

        class HorizontalLayout : public UILayout
        {
        public:
            void UpdateLayout(Panel *panel) final;

        public:
            EAlignment _align = EAlignment::kCenter;
            f32 _spacing = UIStyleSheet::_horizontal_spacing;
            Vector4f _padding = UIStyleSheet::_layout_padding;//LTRB
            f32 _percent = 0.0f;
            ELayoutFillMode _fill_mode = ELayoutFillMode::kPercentParent;
        };

        class AnchorLayout : public UILayout
        {
        public:
            void UpdateLayout(Panel *panel) final;
        };
    }// namespace UI
}// namespace Ailu
#endif// !__UI_LAYOUT_H__