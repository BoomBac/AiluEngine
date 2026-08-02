#ifndef INSPECTOR_COMPONENTEDITORS_SPRITERENDERERCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_SPRITERENDERERCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class SpriteRendererComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif
