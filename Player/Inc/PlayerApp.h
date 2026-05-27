#pragma once

#include "Framework/Common/Application.h"

namespace Ailu
{
    class PlayerApp : public Application
    {
    public:
        int Initialize() final;
        void Finalize() final;

    private:
        bool OnWindowResize(WindowResizeEvent &e) final;
        bool LoadLaunchConfig();
        void BindActiveSceneCamera() const;

    private:
        WString _launch_config_path;
        WString _startup_scene_path;
        u32 _window_width = 1600u;
        u32 _window_height = 900u;
        bool _development_mode = false;
    };
}