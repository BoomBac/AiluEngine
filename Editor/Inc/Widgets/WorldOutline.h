#pragma once
#ifndef __WORLD_OUTLINE_H__
#define __WORLD_OUTLINE_H__
#include "Dock/DockWindow.h"
#include "Scene/Scene.h"
#include "generated/WorldOutline.gen.h"

namespace Ailu
{
    namespace UI
    {
        class TreeView;
        class Text;
        class Button;
    }
    namespace Editor
    {
        ACLASS()
        class WorldOutline : public DockWindow
        {
            GENERATED_BODY()
        public:
            WorldOutline();
            ~WorldOutline() override;
            void Update(f32 dt) override;

        private:
            void BuildUI();
            void BindTreeEvents();
            void SyncSelectionFromEngine();
            void ShowEntityContextMenu(ECS::Entity entity, Vector2f position);
            void FocusCameraOnEntity(ECS::Entity entity);
            void OnDropAction(UI::TreeView* source, u64 source_item, u64 target_item);

            UI::TreeView* _tree_view = nullptr;
            UI::Text* _scene_title = nullptr;
            UI::Button* _add_button = nullptr;

            class SceneTreeDataSource;
            Scope<SceneTreeDataSource> _data_source;

            SceneManagement::Scene* _observed_scene = nullptr;
            u64 _observed_structure_revision = 0;
            u64 _observed_selection_revision = 0;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__WORLD_OUTLINE_H__
