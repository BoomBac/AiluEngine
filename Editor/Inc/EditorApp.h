#pragma once
#ifndef __EDITOR_APP__
#define __EDITOR_APP__

#include "Framework/Common/Application.h"
#include "Widgets/EditorLayer.h"
#include "generated/EditorApp.gen.h"

namespace Ailu
{
    class Render::Camera;
    namespace Editor
    {
        ASTRUCT()
        struct EditorConfig
        {
            GENERATED_BODY()
            APROPERTY(Category = "Window")
            Vector2UInt _window_size;
            APROPERTY(Category = "Window")
            Vector2UInt _viewport_size;
            APROPERTY(Category = "Camera")
            Vector3f _position;
            APROPERTY(Category = "Camera")
            Vector4f _rotation;
            APROPERTY(Category = "Camera")
            f32 _fov;
            APROPERTY(Category = "Camera")
            f32 _aspect;
            APROPERTY(Category = "Camera")
            f32 _near;
            APROPERTY(Category = "Camera")
            f32 _far;
            APROPERTY(Category = "Camera")
            f32 _move_speed;
            APROPERTY(Category = "Camera")
            Vector2f _controller_rot;
            APROPERTY(Category = "Scene")
            String _scene_path;
        };

        ASTRUCT()
        struct TestObj
        {
            GENERATED_BODY()
            APROPERTY()
            String _name;
            APROPERTY()
            Object _p;
            APROPERTY()
            Vector<u32> _nums;
            APROPERTY()
            Vector<Object> _objs;
        };

        class InputLayer;
        class SceneLayer;
        class EditorApp : public Ailu::Application
        {
        public:
            int Initialize() final;
            void Finalize() final;
            void Tick(f32 delta_time) final;

            Delegate<const fs::path &> _on_file_changed_delegate;
        private:
            EditorLayer *_p_editor_layer;
            InputLayer *_p_input_layer;
            SceneLayer *_p_scene_layer;
            ApplicationDesc _desc;
            WString s_editor_config_path;
            WString s_editor_root_path;
            Camera *_p_scene_camera;
            WString _opened_scene_path;
            EditorConfig _editor_config;
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
