#pragma once
#include "Objects/Serialize.h"
#include "UIStyles.h"
#include "UI/Table/UITableStyle.h"
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

            const UIListViewStyle *FindListViewStyle(const UIStyleId &style_id) const;
            void SetListViewStyle(const UIStyleId &style_id, const UIListViewStyle &style);

            const UIElementVisualStyle *FindElementVisualStyle(const UIStyleId &style_id) const;
            void SetElementVisualStyle(const UIStyleId &style_id, const UIElementVisualStyle &style);

            const UIBorderStyle *FindBorderStyle(const UIStyleId &style_id) const;
            void SetBorderStyle(const UIStyleId &style_id, const UIBorderStyle &style);

            const UICollapsibleViewStyle *FindCollapsibleViewStyle(const UIStyleId &style_id) const;
            void SetCollapsibleViewStyle(const UIStyleId &style_id, const UICollapsibleViewStyle &style);

            const UISplitViewStyle *FindSplitViewStyle(const UIStyleId &style_id) const;
            void SetSplitViewStyle(const UIStyleId &style_id, const UISplitViewStyle &style);

            const UIColorPickerStyle *FindColorPickerStyle(const UIStyleId &style_id) const;
            void SetColorPickerStyle(const UIStyleId &style_id, const UIColorPickerStyle &style);

            const UITreeViewStyle *FindTreeViewStyle(const UIStyleId &style_id) const;
            void SetTreeViewStyle(const UIStyleId &style_id, const UITreeViewStyle &style);

            const UITableStyle *FindTableStyle(const UIStyleId &style_id) const;
            void SetTableStyle(const UIStyleId &style_id, const UITableStyle &style);

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
            UIListViewStyle _list_view_style;

            APROPERTY()
            UIElementVisualStyle _element_visual_style;

            APROPERTY()
            UIBorderStyle _border_style;

            APROPERTY()
            UICollapsibleViewStyle _collapsible_view_style;

            APROPERTY()
            UISplitViewStyle _split_view_style;

            APROPERTY()
            UIColorPickerStyle _color_picker_style;

            APROPERTY()
            UIScrollViewStyle _scroll_view_style;

            APROPERTY()
            UITreeViewStyle _tree_view_style;

            APROPERTY()
            UITableStyle _table_style;

        private:
            explicit UITheme(bool init_default);
            void InitializeDefaultDark();

        private:
            APROPERTY()
            HashMap<UIStyleId, UIButtonStyle> _button_styles;

            APROPERTY()
            HashMap<UIStyleId, UISliderStyle> _slider_styles;

            APROPERTY()
            HashMap<UIStyleId, UICheckBoxStyle> _check_box_styles;

            APROPERTY()
            HashMap<UIStyleId, UIInputStyle> _input_styles;

            APROPERTY()
            HashMap<UIStyleId, UIListViewStyle> _list_view_styles;

            APROPERTY()
            HashMap<UIStyleId, UIElementVisualStyle> _element_visual_styles;

            APROPERTY()
            HashMap<UIStyleId, UIBorderStyle> _border_styles;

            APROPERTY()
            HashMap<UIStyleId, UICollapsibleViewStyle> _collapsible_view_styles;

            APROPERTY()
            HashMap<UIStyleId, UISplitViewStyle> _split_view_styles;

            APROPERTY()
            HashMap<UIStyleId, UIColorPickerStyle> _color_picker_styles;

            APROPERTY()
            HashMap<UIStyleId, UITreeViewStyle> _tree_view_styles;

            APROPERTY()
            HashMap<UIStyleId, UITableStyle> _table_styles;

            u64 _revision = 1u;
        };
    }// namespace UI
}// namespace Ailu
