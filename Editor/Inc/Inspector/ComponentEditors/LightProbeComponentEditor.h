#ifndef INSPECTOR_COMPONENTEDITORS_LIGHTPROBECOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_LIGHTPROBECOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class LightProbeComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif
