#pragma once
#ifndef __EDITOR_APP__
#define __EDITOR_APP__

#include "Ailu.h"
#include "Framework/Common/Application.h"
#include "Widgets/EditorLayer.h"

namespace Ailu
{
    class Camera;
    namespace Editor
    {
        class InputLayer;
        class SceneLayer;
        class EditorApp : public Ailu::Application
        {
        public:
            int Initialize() final;
            void Finalize() final;
            void Tick(f32 delta_time) final;

        private:
            EditorLayer *_p_editor_layer;
            InputLayer *_p_input_layer;
            SceneLayer *_p_scene_layer;
            ApplicationDesc _desc;
            WString s_editor_config_path;
            WString s_editor_root_path;
            Camera *_p_scene_camera;
            WString _opened_scene_path;

        private:
            bool OnGetFocus(WindowFocusEvent &e) final;
            bool OnLostFocus(WindowLostFocusEvent &e) final;
            void LoadEditorConfig(ApplicationDesc &desc);
            void SaveEditorConfig();
            void LoadEditorResource();
            void WatchDirectory();
        };
    }// namespace Editor
}// namespace Ailu

#endif// !EDITOR_APP__
