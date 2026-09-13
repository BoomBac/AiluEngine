#ifndef INSPECTOR_COMPONENTEDITORS_POSTPROCESSVOLUMECOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_POSTPROCESSVOLUMECOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        class PostprocessVolumeComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif
