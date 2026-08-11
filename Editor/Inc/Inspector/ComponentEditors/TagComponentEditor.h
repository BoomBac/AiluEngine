#pragma once
#ifndef TAG_COMPONENT_EDITOR_H
#define TAG_COMPONENT_EDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu::Editor
{
    class TagComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
    };
}
#endif
