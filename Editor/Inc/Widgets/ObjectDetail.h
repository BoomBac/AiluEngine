#ifndef __COMMON_VIEW__
#define __COMMON_VIEW__
#include "Dock/DockWindow.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ReflectedPropertyPanel.h"
#include "Inspector/IComponentEditor.h"
#include "generated/ObjectDetail.gen.h"

namespace Ailu
{
    namespace UI
    {
        class Image;
        class VerticalBox;
        class ScrollView;
        class InputBlock;
        class CollapsibleView;
        class Button;
        class CheckBox;
        class Text;
    }// namespace UI
    namespace Editor
    {
        struct ObjectDetailComponentEntry
        {
            const ComponentEditorInfo *_component_info = nullptr;
            UI::CollapsibleView *_block = nullptr;
            Scope<IComponentEditor> _custom_editor;
            Scope<ReflectedPropertyPanel> _reflected_panel;
        };

        ACLASS()
        class ObjectDetail : public DockWindow
        {
            GENERATED_BODY()
        public:
            ObjectDetail();
            ~ObjectDetail() override;
            void Update(f32 dt) final;

        private:
            void Rebuild(ECS::Entity entity);
            void ClearComponentEntries();
            void BuildComponentEntry(const ComponentEditorInfo &info, ECS::Entity entity);
            UI::CollapsibleView *CreateComponentBlock(const ComponentEditorInfo &info, ECS::Entity entity, bool allow_remove);
            ComponentEditorContext BuildContext(const ComponentEditorInfo &info, UI::CollapsibleView *block);
            void ShowAddComponentPopup(UI::UIElement *anchor);

            UI::ScrollView *_root = nullptr;
            UI::VerticalBox *_vb = nullptr;
            UI::Text *_name_text = nullptr;
            UI::CheckBox *_entity_enabled_checkbox = nullptr;
            UI::Button *_add_component_button = nullptr;
            ECS::Entity _selected_entity = ECS::kInvalidEntity;
            bool _needs_rebuild = true;
            Vector<ObjectDetailComponentEntry> _component_entries;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__COMMON_VIEW__
