#ifndef INSPECTOR_COMPONENTEDITORS_SCRIPTCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_SCRIPTCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class ScriptComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_SCRIPTCOMPONENTEDITOR_H
