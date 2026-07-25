#pragma once
#ifndef __EDITOR_APP__
#define __EDITOR_APP__

#include "Framework/Common/Application.h"
#include "Framework/Common/FileWatcher.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Math/Vector.hpp"
#include "Widgets/EditorLayer.h"
#include "generated/EditorApp.gen.h"
#include <filesystem>

namespace Ailu
{
    using Math::Vector2f;
    using Math::Vector2UInt;
    using Math::Vector3f;
    using Math::Vector4f;

    namespace Render
    {
        class Camera;
    }
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
            APROPERTY()
            Vector2f _v2;
            APROPERTY()
            Vector3f _v3;
            APROPERTY()
            Vector4f _v4;
        };

        class InputLayer;
        class SceneLayer;
        class FirstPersonCameraController;
        class EditorApp : public Ailu::Application
        {
            DECLARE_DELEGATE(on_file_changed, const std::filesystem::path &);

        public:
            static EditorApp *GetEditor() { return static_cast<EditorApp *>(&Application::Get()); }
            //return  AiluEngine/Editor/EditorConfig.json
            static const WString &GetEditorConfigPath() { return s_editor_config_path; }
            //return  AiluEngine/Editor/
            static const WString &GetEditorRootPath() { return s_editor_root_path; }

        public:
            EditorApp();
            ~EditorApp();
            int Initialize() final;
            int Initialize(ApplicationInitContext init_ctx);
            void Finalize() final;
            void Tick(f32 delta_time) final;
            FirstPersonCameraController &GetSceneCameraController() { return *_camera_controller; }
            Camera *GetSceneCamera() { return _p_scene_camera; }
        private:
            bool OnGetFocus(WindowFocusEvent &e) final;
            bool OnLostFocus(WindowLostFocusEvent &e) final;
            void LoadEditorConfig(ApplicationDesc &desc);
            void SaveEditorConfig();
            void LoadEditorResource();
            void ConfigureResourceReloading();
            void WatchDirectory();

        private:
            inline static WString s_editor_config_path;
            inline static WString s_editor_root_path;
            EditorLayer *_p_editor_layer;
            InputLayer *_p_input_layer;
            SceneLayer *_p_scene_layer;
            ApplicationDesc _desc;
            Camera *_p_scene_camera;
            WString _opened_scene_path;
            EditorConfig _editor_config;
            Scope<FirstPersonCameraController> _camera_controller;
            FileWatchService _file_watch_service;
            FileChangeDispatcher _file_change_dispatcher;
            ResourceReloadService _resource_reload_service;
            bool _is_resource_reload_configured = false;
        };// namespace Editor
    }// namespace Editor
}// namespace Ailu
#endif// !EDITOR_APP__
