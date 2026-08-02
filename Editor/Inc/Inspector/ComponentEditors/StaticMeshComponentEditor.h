#ifndef INSPECTOR_COMPONENTEDITORS_STATICMESHCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_STATICMESHCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class StaticMeshComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_STATICMESHCOMPONENTEDITOR_H
