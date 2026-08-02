#ifndef INSPECTOR_ICOMPONENTEDITOR_H
#define INSPECTOR_ICOMPONENTEDITOR_H
#include "Framework/Common/NonCopyable.h"

namespace Ailu
{
    namespace UI
    {
        class VerticalBox;
    }
    namespace Editor
    {
        struct ComponentEditorContext;

        class IComponentEditor : public NonCopyable
        {
        public:
            virtual ~IComponentEditor() = default;
            virtual void Build(ComponentEditorContext &context) = 0;
            virtual void Refresh(ComponentEditorContext &context) {}
            virtual bool NeedsRebuild(const ComponentEditorContext &context) const { return false; }
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_ICOMPONENTEDITOR_H
