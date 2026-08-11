#pragma once
#ifndef __RIGID_BODY_2D_COMPONENT_EDITOR_H__
#define __RIGID_BODY_2D_COMPONENT_EDITOR_H__

#include "Inspector/IComponentEditor.h"

namespace Ailu::Editor
{
    class RigidBody2DComponentEditor final : public IComponentEditor
    {
    public:
        void Build(ComponentEditorContext &context) override;
    };
}

#endif
