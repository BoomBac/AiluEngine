#ifndef INSPECTOR_COMPONENTEDITORS_LIGHTCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_LIGHTCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"
#include "Scene/Component.h"

namespace Ailu
{
    namespace Editor
    {
        class LightComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;
            bool NeedsRebuild(const ComponentEditorContext &context) const override;

        private:
            ECS::ELightType _cached_light_type = static_cast<ECS::ELightType>(-1);
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_LIGHTCOMPONENTEDITOR_H
