#ifndef INSPECTOR_COMPONENTEDITORS_ENVIRONMENTCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_ENVIRONMENTCOMPONENTEDITOR_H

#include "Inspector/IComponentEditor.h"
#include "Scene/Component.h"

namespace Ailu::Editor
{
    class EnvironmentComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
        bool NeedsRebuild(const ComponentEditorContext &context) const override;

    private:
        ECS::EEnvironmentSource _cached_source = ECS::EEnvironmentSource::kProceduralSky;
    };
}

#endif // INSPECTOR_COMPONENTEDITORS_ENVIRONMENTCOMPONENTEDITOR_H
