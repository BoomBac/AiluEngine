#pragma once
#ifndef __PLACE_ACTOR_H__
#define __PLACE_ACTOR_H__
#include "Dock/DockWindow.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/String.h"
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
        struct PlaceActorDragPayload
        {
            String _id;
        };

        struct PlaceActorUIEntry
        {
            String _id;
            String _display_name;
            String _category;
            i32 _order = 0;
            WString _asset_path;
            Asset *_asset = nullptr;
            Ref<Render::Mesh> _mesh = nullptr;
            bool _is_mesh = false;
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
            void BuildEntries();
            void BuildCategories();
            void RefreshEntries();
            void ResolveEntries();
            void BeginActorDrag(i32 entry_index);

            UI::SplitView *_split_view = nullptr;
            UI::VerticalBox *_category_box = nullptr;
            UI::ScrollView *_entry_scroll = nullptr;
            UI::VerticalBox *_entry_content = nullptr;
            Vector<UI::Button *> _category_buttons;
            Vector<String> _categories;
            Vector<PlaceActorUIEntry> _entries;
            u32 _selected_category = 0u;
            PlaceActorDragPayload _drag_payload;
            i32 _drag_source_index = -1;
            Vector2f _drag_start_pos = Vector2f::kZero;
            bool _is_dragging = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__PLACE_ACTOR_H__
