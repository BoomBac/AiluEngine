#pragma once
#ifndef __ANIMATOR_COMPONENT_EDITOR_H__
#define __ANIMATOR_COMPONENT_EDITOR_H__

#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class AnimatorComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __ANIMATOR_COMPONENT_EDITOR_H__
