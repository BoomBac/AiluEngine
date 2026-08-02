#ifndef INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class TransformComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_TRANSFORMCOMPONENTEDITOR_H
