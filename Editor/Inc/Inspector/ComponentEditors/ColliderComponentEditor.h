#ifndef INSPECTOR_COMPONENTEDITORS_COLLIDERCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_COLLIDERCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"
#include "Scene/Component.h"

namespace Ailu
{
    namespace Editor
    {
        class ColliderComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
            bool NeedsRebuild(const ComponentEditorContext &context) const override;

        private:
            ECS::EColliderType _cached_type = ECS::EColliderType::kBox;
        };
    }// namespace Editor
}// namespace Ailu
#endif
