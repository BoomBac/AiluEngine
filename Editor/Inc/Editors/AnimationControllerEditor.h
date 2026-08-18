#pragma once
#ifndef __ANIMATION_CONTROLLER_EDITOR_H__
#define __ANIMATION_CONTROLLER_EDITOR_H__

#include "Animation/AnimationControllerAsset.h"
#include "Dock/DockWindow.h"
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
        class AnimationControllerEditor final : public DockWindow
        {
        public:
            AnimationControllerEditor();
            ~AnimationControllerEditor() override = default;

            void Update(f32 dt) override;
            void Open(AnimationControllerAsset *controller);
            void Close();

        private:
            void ReadFromAsset();
            void WriteToAsset();
            void Apply();
            void Revert();
            void MarkDirty();
            void RefreshAllUI();
            void RefreshParameters();
            void RefreshStates();
            void RefreshTransitions();
            void RefreshGraphFromController();
            void SyncControllerFromGraph();
            void SyncGraphPositions();
            void EnsureGraphNodeRegistry();
            void RebuildTransitionLinks();
            void AddParameter();
            void AddState();
            void AddTransition();
            void AddCondition();
            void RemoveParameter(u32 index);
            void RemoveState(u32 index);
            void RemoveTransition(u32 index);
            void ShowClipPicker(u32 state_index, UI::UIElement *anchor);

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label);
            static UI::InputBlock *AddTextInput(UI::UIElement *parent, const String &label, const String &value,
                                                const std::function<void(String)> &on_changed);
            static UI::InputBlock *AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                 const std::function<void(f32)> &on_changed);

        private:
            AnimationControllerAsset *_controller = nullptr;
            Vector<AnimationParameterDesc> _editing_parameters;
            Vector<AnimationState> _editing_states;
            Vector<AnimationTransition> _editing_transitions;
            Vector<u16> _editing_any_state_transitions;
            u16 _editing_entry_state = kInvalidAnimationState;
            Vector<AnimationParameterDesc> _original_parameters;
            Vector<AnimationState> _original_states;
            Vector<AnimationTransition> _original_transitions;
            Vector<u16> _original_any_state_transitions;
            u16 _original_entry_state = kInvalidAnimationState;
            bool _is_dirty = false;
            i32 _selected_state = -1;
            i32 _selected_transition = -1;
            String _graph_link_signature;
            String _graph_node_signature;

            UI::VerticalBox *_parameters_root = nullptr;
            UI::VerticalBox *_states_root = nullptr;
            UI::VerticalBox *_details_root = nullptr;
            UI::VerticalBox *_transitions_root = nullptr;
            GraphCanvas *_graph_canvas = nullptr;
            Scope<GraphAsset> _graph_asset;
            Scope<GraphDocument> _graph_document;
            UI::Text *_txt_status = nullptr;
            UI::Button *_btn_apply = nullptr;
            UI::Button *_btn_revert = nullptr;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __ANIMATION_CONTROLLER_EDITOR_H__
