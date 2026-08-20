#include "PlayerApp.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Objects/JsonArchive.h"
#include "Render/Camera.h"
#include "Render/RenderPipeline.h"
#include "Scene/Scene.h"

#include <document.h>

namespace Ailu
{
    int PlayerApp::Initialize()
    {
        LogMgr::Init();
        _launch_config_path = GetWorkingPath() + L"player.json";
        if (!LoadLaunchConfig())
            return 1;

        ApplicationDesc desc{};
        desc._window_width = _window_width;
        desc._window_height = _window_height;
        desc._gameview_width = _window_width;
        desc._gameview_height = _window_height;

        auto ret = Application::Initialize(desc);
        if (ret != 0)
            return ret;

        _pipeline = AL_NEW_TAG(EMemoryTag::kRenderer, Render::RenderPipeline);
        Render::RenderPipeline::Register(_pipeline);
        SceneManagement::SceneMgr::Get().OpenScene(_startup_scene_path);
        SceneManagement::SceneMgr::Get().Tick(0.0f);
        BindActiveSceneCamera();
        return 0;
    }

    void PlayerApp::Finalize()
    {
        AL_DELETE(_pipeline);
        LogMgr::Shutdown();
        Application::Finalize();
    }

    bool PlayerApp::OnWindowResize(WindowResizeEvent &e)
    {
        Application::OnWindowResize(e);
        if (Render::Camera::sCurrent)
        {
            Render::Camera::sCurrent->OutputSize(static_cast<u16>(e.GetWidth()), static_cast<u16>(e.GetHeight()));
            Render::Camera::sCurrent->RecalculateMatrix(true);
        }
        return false;
    }

    bool PlayerApp::LoadLaunchConfig()
    {
        String config_text;
        if (!FileManager::ReadFile(_launch_config_path, config_text))
        {
            LOG_ERROR(L"Player launch config not found: {}", _launch_config_path);
            return false;
        }

        rapidjson::Document doc;
        doc.Parse(config_text.c_str());
        if (doc.HasParseError() || !doc.IsObject())
        {
            LOG_ERROR(L"Failed to parse player config: {}", _launch_config_path);
            return false;
        }

        const fs::path config_dir = fs::path(_launch_config_path).parent_path();
        if (!doc.HasMember("resourceRoot") || !doc["resourceRoot"].IsString())
        {
            LOG_ERROR("player.json is missing required string field 'resourceRoot'");
            return false;
        }

        const auto resource_root = fs::weakly_canonical(config_dir / fs::path(ToWChar(doc["resourceRoot"].GetString())));
        ResourceMgr::ConfigEngineResRoot(resource_root.wstring());
        Application::SetProjectRootPath(PathUtils::NormalizeDirectoryPath(config_dir.wstring()));
        Application::SetEngineConfigPath(PathUtils::FormatFilePath((config_dir / "EngineConfig.json").wstring()));

        if (doc.HasMember("startupScene") && doc["startupScene"].IsString())
            _startup_scene_path = ToWChar(doc["startupScene"].GetString());
        else
            _startup_scene_path = L"WaterTest/empty_scene.almap";

        if (doc.HasMember("windowWidth") && doc["windowWidth"].IsUint())
            _window_width = doc["windowWidth"].GetUint();
        if (doc.HasMember("windowHeight") && doc["windowHeight"].IsUint())
            _window_height = doc["windowHeight"].GetUint();
        if (doc.HasMember("developmentMode") && doc["developmentMode"].IsBool())
            _development_mode = doc["developmentMode"].GetBool();

        LOG_INFO(L"Player resource root: {}", ResourceMgr::EngineResRootPath());
        LOG_INFO(L"Player startup scene: {}", _startup_scene_path);
        return true;
    }

    void PlayerApp::BindActiveSceneCamera() const
    {
        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        if (scene)
        {
            for (auto &camera_component : scene->GetRegister().View<ECS::CCamera>())
            {
                Render::Camera::sCurrent = &camera_component._camera;
                Render::Camera::sCurrent->OutputSize(static_cast<u16>(_window_width), static_cast<u16>(_window_height));
                Render::Camera::sCurrent->RecalculateMatrix(true);
                return;
            }
        }

        auto *fallback_camera = Render::Camera::GetDefaultCamera();
        fallback_camera->OutputSize(static_cast<u16>(_window_width), static_cast<u16>(_window_height));
        fallback_camera->RecalculateMatrix(true);
    }
}
