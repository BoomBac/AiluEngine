#ifndef INSPECTOR_COMPONENTEDITORS_LIGHTPROBEGROUPCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_LIGHTPROBEGROUPCOMPONENTEDITOR_H

#include "Inspector/IComponentEditor.h"

namespace Ailu::UI
{
    class Text;
}

namespace Ailu::Editor
{
    class LightProbeGroupComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
        void Refresh(ComponentEditorContext &context) override;

    private:
        UI::Text *_bake_status = nullptr;
    };
}

#endif
