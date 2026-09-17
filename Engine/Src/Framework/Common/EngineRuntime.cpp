#include "Framework/Common/EngineRuntime.h"

#include "Audio/Audio.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Script/ScriptSystem.h"
#include "Platform/WinWindow.h"
#include "Render/GraphicsContext.h"
#include "Render/Gizmo.h"
#include "Render/ImageCapture/ImageCaptureService.h"
#include "Render/RenderPipeline.h"
#include "Render/Texture.h"
#include "Scene/Scene.h"
#include "UI/UIFramework.h"

namespace Ailu
{
    EngineRuntime::~EngineRuntime() = default;

    bool EngineRuntime::Initialize(const ApplicationInitContext &context, Window &window, const ApplicationDesc &desc)
    {
        if (_is_initialized)
            return true;

        _window = &window;
        _is_initialized = true;
        _thread_pool = MakeScope<Core::ThreadPool>(6u, "GlobalThreadPool");
        _job_system = MakeScope<JobSystem>(6u);

        Render::GraphicsContext::InitGlobalContext();
        _graphics = Render::g_pGfxContext;
        _graphics->RegisterWindow(_window);
        Render::RenderTexture::s_backbuffer = Render::RenderTexture::WindowBackBuffer(_window);
        _graphics->ResizeSwapChain(_window->GetNativeWindowPtr(), desc._window_width, desc._window_height);

        _resources = MakeScope<ResourceSystem>();
        if (_resources->Initialize() != 0)
        {
            Shutdown();
            return false;
        }

        _scripts = MakeScope<ScriptSystem>();
        if (_scripts->Initialize() != 0)
        {
            Shutdown();
            return false;
        }

        AudioDeviceConfig audio_config;
        audio_config._enabled = context._enable_audio;
        audio_config._device_name = context._audio_device_name;
        audio_config._sample_rate = context._audio_sample_rate;
        audio_config._max_voices = context._audio_max_voices;
        audio_config._max_streaming_voices = context._audio_max_streaming_voices;
        Audio::Initialize(audio_config);
        _audio_initialized = true;

        Render::Gizmo::Initialize();
        _gizmo_initialized = true;
        UI::UIManager::Init();
        _ui_initialized = true;
        _scenes = MakeScope<SceneManagement::SceneMgr>();
        return true;
    }

    void EngineRuntime::BeginShutdown()
    {
        if (!_is_initialized || _is_shutting_down)
            return;

        _is_shutting_down = true;
        if (_resources)
            _resources->BeginShutdown();
    }

    void EngineRuntime::Drain()
    {
        if (!_is_initialized)
            return;

        if (_resources)
            _resources->Drain();
        if (_job_system)
            _job_system->Wait();
        if (_graphics)
            _graphics->WaitForGpu();
        // GPU post-submit callbacks may enqueue jobs after the first JobSystem wait.
        if (_job_system)
            _job_system->Wait();
        if (_resources)
            _resources->Finalize();
    }

    void EngineRuntime::Shutdown()
    {
        if (!_is_initialized)
            return;

        BeginShutdown();
        Drain();
        Render::ImageCaptureService::Shutdown();

        if (_scenes)
        {
            SceneManagement::SceneShutdownContext context;
            context._scripts = _scripts.get();
            _scenes->ShutdownScenes(context);
        }
        _scenes.reset();

        if (_scripts)
        {
            _scripts->Finalize();
            _scripts.reset();
        }

        if (_resources)
        {
            _resources->ReleaseRuntimeResources();
            _resources.reset();
        }

        if (_audio_initialized)
        {
            Audio::Shutdown();
            _audio_initialized = false;
        }
        if (_gizmo_initialized)
        {
            Render::Gizmo::Shutdown();
            _gizmo_initialized = false;
        }
        if (_ui_initialized)
        {
            UI::UIManager::Shutdown();
            _ui_initialized = false;
        }

        if (_graphics)
        {
            if (_window)
                _graphics->UnRegisterWindow(_window);
            Render::GraphicsContext::FinalizeGlobalContext();
            _graphics = nullptr;
        }

        _job_system.reset();
        _thread_pool.reset();
        _window = nullptr;
        _is_initialized = false;
    }
}
