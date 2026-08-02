#ifndef INSPECTOR_COMPONENTEDITORS_CAMERACOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_CAMERACOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class CameraComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif
