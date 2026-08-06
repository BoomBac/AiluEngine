#ifndef INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

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
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;

        private:
            UI::InputBlock *_position_blocks[3] = {};
            UI::InputBlock *_rotation_blocks[3] = {};
            UI::InputBlock *_scale_blocks[3] = {};
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
