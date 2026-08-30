#pragma once
#ifndef __ANIMATION_CONTROLLER_EDITOR_H__
#define __ANIMATION_CONTROLLER_EDITOR_H__

#include "Animation/AnimationControllerAsset.h"
#include "Editors/AssetEditor.h"
#include "Graph/GraphAsset.h"
#include "Graph/GraphDocument.h"

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
        class GraphCanvas;
    }

    namespace Editor
    {
        class AnimationControllerEditor final : public AssetEditor
        {
        public:
            AnimationControllerEditor();
            ~AnimationControllerEditor() override = default;

            void Update(f32 dt) override;

        private:
            void ReadFromAsset();
            void WriteToAsset();
            void MarkDirty();
            void RefreshAllUI();
            void RefreshParameters();
            void RefreshStates();
            void RefreshTransitions();
            void RefreshGraphFromController();
            void SyncControllerFromGraph(bool nodes_changed);
            void SyncGraphPositions();
            void RefreshGraphNodeTitles();
            void EnsureGraphNodeRegistry();
            void RebuildTransitionLinks();
            void AddParameter();
            void AddState();
            void AddTransition();
            void AddCondition();
            void RemoveParameter(u32 index);
            void RemoveState(u32 index);
            void RemoveTransition(u32 index);
            void ShowMotionPicker(u32 state_index, UI::UIElement *anchor);

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label);
            static UI::InputBlock *AddTextInput(UI::UIElement *parent, const String &label, const String &value,
                                                const std::function<void(String)> &on_changed);
            static UI::InputBlock *AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                 const std::function<void(f32)> &on_changed);

            bool OnOpen() override;
            void OnClose() override;
            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            AnimationControllerAsset *_controller = nullptr;
            Vector<AnimationParameterDesc> _editing_parameters;
            Vector<AnimationState> _editing_states;
            Vector<AnimationTransition> _editing_transitions;
            Vector<u16> _editing_any_state_transitions;
            u16 _editing_entry_state = kInvalidAnimationState;
            i32 _selected_state = -1;
            i32 _selected_transition = -1;
            HashMap<Guid, i32, GuidHasher> _transition_link_map;
            String _graph_link_signature;
            String _graph_node_signature;
            bool _graph_view_initialized = false;

            UI::VerticalBox *_parameters_root = nullptr;
            UI::VerticalBox *_states_root = nullptr;
            UI::VerticalBox *_details_root = nullptr;
            UI::VerticalBox *_transitions_root = nullptr;
            GraphCanvas *_graph_canvas = nullptr;
            Scope<GraphAsset> _graph_asset;
            Scope<GraphDocument> _graph_document;
            UI::Text *_txt_status = nullptr;
            String _last_edit_snapshot;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __ANIMATION_CONTROLLER_EDITOR_H__
