#ifndef INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"
#include "Inspector/TransformPropertyCommand.h"

namespace Ailu
{
    namespace UI
    {
        class InputBlock;
    }
    namespace Editor
    {
        class TransformComponentEditor final : public IComponentEditor
        {
        public:
            ~TransformComponentEditor() override;
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;
            bool SupportsMultiEdit() const override { return true; }

        private:
            void FinishEdit();
            CommandManager *_command_manager = nullptr;
            u32 _history_subscription = 0;
            Scope<TransformPropertyCommand> _pending_command;
            UI::InputBlock *_editing_block = nullptr;
            UI::InputBlock *_position_blocks[3] = {};
            UI::InputBlock *_rotation_blocks[3] = {};
            UI::InputBlock *_scale_blocks[3] = {};
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
