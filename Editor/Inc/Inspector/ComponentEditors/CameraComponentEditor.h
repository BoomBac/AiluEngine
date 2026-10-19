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
            bool NeedsRebuild(const ComponentEditorContext &context) const override;
        private:
            int _cached_type = -1;
            int _cached_lens_mode = -1;
            int _cached_exposure_mode = -1;
        };
    }// namespace Editor
}// namespace Ailu
#endif
