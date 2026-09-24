#pragma once
#ifndef INSPECTOR_COMPONENTEDITORS_SURFELGICOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_SURFELGICOMPONENTEDITOR_H

#include "Inspector/IComponentEditor.h"

namespace Ailu::Editor
{
    class SurfelGIComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
        void Refresh(ComponentEditorContext &context) override;
    };
}

#endif // INSPECTOR_COMPONENTEDITORS_SURFELGICOMPONENTEDITOR_H
