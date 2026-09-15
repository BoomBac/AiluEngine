#pragma once
#ifndef INSPECTOR_COMPONENTEDITORS_DDGIVOLUMECOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_DDGIVOLUMECOMPONENTEDITOR_H

#include "Inspector/IComponentEditor.h"

namespace Ailu::Editor
{
    class DDGIVolumeComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
        void Refresh(ComponentEditorContext &context) override;
    };
}

#endif // INSPECTOR_COMPONENTEDITORS_DDGIVOLUMECOMPONENTEDITOR_H
