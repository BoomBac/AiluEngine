#pragma once
#ifndef __PLACE_ACTOR_H__
#define __PLACE_ACTOR_H__
#include "Dock/DockWindow.h"
#include "generated/PlaceActor.gen.h"

namespace Ailu
{
    class Asset;
    namespace Render
    {
        class Mesh;
    }
    namespace UI
    {
        class Button;
        class ScrollView;
        class SplitView;
        class VerticalBox;
    }
    namespace Editor
    {
        enum class EPlaceActorPresetType : u32
        {
            kMesh,
            kDirectionalLight,
            kPointLight,
            kSpotLight,
            kAreaLight,
            kLightProbe
        };

        struct PlaceActorDragPayload
        {
            EPlaceActorPresetType _type = EPlaceActorPresetType::kMesh;
            const char *_display_name = nullptr;
        };

        ACLASS()
        class PlaceActor : public DockWindow
        {
            GENERATED_BODY()
        public:
            PlaceActor();
            ~PlaceActor() override = default;
            void Update(f32 dt) override;
            String GetDockPersistenceId() const override { return "Ailu::Editor::PlaceActor"; }

        private:
            void BuildUI();
            void RefreshEntries();
            void ResolveEntries();
            void BeginActorDrag(i32 entry_index);

            UI::SplitView *_split_view = nullptr;
            UI::VerticalBox *_category_box = nullptr;
            UI::ScrollView *_entry_scroll = nullptr;
            UI::VerticalBox *_entry_content = nullptr;
            Vector<UI::Button *> _category_buttons;
            Vector<String> _entry_names;
            Vector<WString> _entry_paths;
            Vector<u32> _entry_categories;
            Vector<EPlaceActorPresetType> _entry_types;
            Vector<Asset *> _entry_assets;
            Vector<Ref<Render::Mesh>> _entry_meshes;
            Vector<UI::Button *> _entry_buttons;
            u32 _selected_category = 0u;
            PlaceActorDragPayload _drag_payload;
            i32 _drag_source_index = -1;
            Vector2f _drag_start_pos = Vector2f::kZero;
            bool _is_dragging = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__PLACE_ACTOR_H__
