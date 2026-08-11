#pragma once
#ifndef __COLLIDER_2D_COMPONENT_EDITOR_H__
#define __COLLIDER_2D_COMPONENT_EDITOR_H__

#include "Inspector/IComponentEditor.h"

namespace Ailu::Editor
{
    class Collider2DComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
    };
}

#endif
