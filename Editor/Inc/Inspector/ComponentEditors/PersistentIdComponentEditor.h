#ifndef INSPECTOR_COMPONENTEDITORS_PERSISTENTIDCOMPONENTEDITOR_H
#define INSPECTOR_COMPONENTEDITORS_PERSISTENTIDCOMPONENTEDITOR_H
#include "Inspector/IComponentEditor.h"

namespace Ailu
{
    namespace Editor
    {
        // Persistent ID 只读展示：允许查看与复制 GUID，不允许编辑或重新生成。
        class PersistentIdComponentEditor final : public IComponentEditor
        {
        public:
            void Build(ComponentEditorContext &context) override;
            void Refresh(ComponentEditorContext &context) override;
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_COMPONENTEDITORS_PERSISTENTIDCOMPONENTEDITOR_H
