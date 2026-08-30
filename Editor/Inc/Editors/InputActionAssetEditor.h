#pragma once
#ifndef __INPUT_ACTION_ASSET_EDITOR_H__
#define __INPUT_ACTION_ASSET_EDITOR_H__

#include "Editors/AssetEditor.h"
#include "Input/InputActionAsset.h"
#include "UI/UIElement.h"
#include <functional>

namespace Ailu
{
    namespace UI
    {
        class Button;
        class CheckBox;
        class Dropdown;
        class HorizontalBox;
        class InputBlock;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        enum class EInputActionEditorSelection : u8
        {
            kNone,
            kActionMap,
            kAction,
            kBinding,
            kContext
        };

        class InputActionAssetEditor : public AssetEditor
        {
        public:
            InputActionAssetEditor();
            ~InputActionAssetEditor() override;

            void Update(f32 dt) override;


        private:
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildLeftPanel(UI::VerticalBox *left);
            void BuildCenterPanel(UI::VerticalBox *center);
            void BuildRightPanel(UI::VerticalBox *right);
            void BuildStatusBar(UI::HorizontalBox *status_bar);

            void MarkDirty();
            void ValidateSelection();
            void RefreshAllUI();
            void RefreshAssetInfo();
            void RefreshTree();
            void RefreshDetails();
            void RefreshSummary();
            void RefreshStatusBar();

            void SelectActionMap(i32 map_index);
            void SelectAction(i32 map_index, i32 action_index);
            void SelectBinding(i32 map_index, i32 action_index, i32 binding_index);
            void SelectContext(i32 context_index);

            void AddActionMap();
            void RemoveSelectedActionMap();
            void AddAction();
            void RemoveSelectedAction();
            void AddBinding();
            void RemoveSelectedBinding();
            void AddContext();
            void RemoveSelectedContext();
            void AddContextMapRef(const String &map_name);
            void RemoveContextMapRef(i32 ref_index);

            InputActionMap *SelectedMap();
            InputAction *SelectedAction();
            InputBinding *SelectedBinding();
            InputContext *SelectedContext();
            const InputActionMap *SelectedMap() const;
            const InputAction *SelectedAction() const;
            const InputBinding *SelectedBinding() const;
            const InputContext *SelectedContext() const;

            Vector<InputActionMap> &ActionMaps() { return _input_asset->GetActionMaps(); }
            const Vector<InputActionMap> &ActionMaps() const { return _input_asset->GetActionMaps(); }
            Vector<InputContext> &Contexts() { return _input_asset->GetContexts(); }
            const Vector<InputContext> &Contexts() const { return _input_asset->GetContexts(); }

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label,
                                                     f32 label_width = 92.0f);
            static UI::InputBlock *AddTextInput(UI::UIElement *parent, const String &label, const String &value,
                                                const std::function<void(String)> &on_changed, f32 label_width = 92.0f);
            static UI::InputBlock *AddU32Input(UI::UIElement *parent, const String &label, u32 value,
                                               const std::function<void(u32)> &on_changed, f32 label_width = 92.0f);
            static UI::InputBlock *AddI32Input(UI::UIElement *parent, const String &label, i32 value,
                                               const std::function<void(i32)> &on_changed, f32 label_width = 92.0f);
            static UI::Dropdown *AddDropdown(UI::UIElement *parent, const String &label, const Vector<String> &items,
                                             i32 selected_index, const std::function<void(i32)> &on_changed,
                                             f32 label_width = 92.0f);
            static UI::CheckBox *AddCheckBox(UI::UIElement *parent, const String &label, bool value,
                                             const std::function<void(bool)> &on_changed, f32 label_width = 92.0f);

            bool OnOpen() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            InputActionAsset *_input_asset = nullptr;

            EInputActionEditorSelection _selection_type = EInputActionEditorSelection::kNone;
            i32 _selected_map = -1;
            i32 _selected_action = -1;
            i32 _selected_binding = -1;
            i32 _selected_context = -1;

            UI::Button *_btn_add_map = nullptr;
            UI::Button *_btn_add_action = nullptr;
            UI::Button *_btn_add_binding = nullptr;
            UI::Button *_btn_add_context = nullptr;
            UI::Button *_btn_delete = nullptr;

            UI::Text *_txt_asset_name = nullptr;
            UI::Text *_txt_asset_path = nullptr;
            UI::Text *_txt_asset_guid = nullptr;
            UI::Text *_txt_map_count = nullptr;
            UI::Text *_txt_action_count = nullptr;
            UI::Text *_txt_binding_count = nullptr;
            UI::Text *_txt_context_count = nullptr;
            UI::Text *_txt_status = nullptr;

            UI::VerticalBox *_tree_root = nullptr;
            UI::VerticalBox *_detail_root = nullptr;
            UI::VerticalBox *_summary_root = nullptr;
            String _last_edit_snapshot;
        };
    } // namespace Editor
} // namespace Ailu

#endif // __INPUT_ACTION_ASSET_EDITOR_H__
