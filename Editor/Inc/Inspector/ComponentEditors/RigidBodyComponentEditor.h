#ifndef INSPECTOR_COMPONENTEDITORS_RIGIDBODYCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_RIGIDBODYCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class RigidBodyComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif
