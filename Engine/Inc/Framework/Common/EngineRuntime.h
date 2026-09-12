#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/SmartPtr.h"

namespace Ailu
{
    struct ApplicationDesc;
    struct ApplicationInitContext;
    class Window;
    class ResourceMgr;
    using ResourceSystem = ResourceMgr;
    class ScriptSystem;
    class JobSystem;

    namespace Core
    {
        class ThreadPool;
    }

    namespace Render
    {
        class GraphicsContext;
    }

    namespace SceneManagement
    {
        class SceneMgr;
    }

    class AILU_API EngineRuntime final
    {
    public:
        EngineRuntime() = default;
        EngineRuntime(const EngineRuntime &) = delete;
        EngineRuntime &operator=(const EngineRuntime &) = delete;
        ~EngineRuntime();

        bool Initialize(const ApplicationInitContext &context, Window &window, const ApplicationDesc &desc);
        void BeginShutdown();
        void Drain();
        void Shutdown();

    private:
        Scope<Core::ThreadPool> _thread_pool;
        Scope<JobSystem> _job_system;
        Scope<ResourceSystem> _resources;
        Scope<ScriptSystem> _scripts;
        Scope<SceneManagement::SceneMgr> _scenes;
        Render::GraphicsContext *_graphics = nullptr;
        Window *_window = nullptr;
        bool _audio_initialized = false;
        bool _gizmo_initialized = false;
        bool _ui_initialized = false;
        bool _is_initialized = false;
        bool _is_shutting_down = false;
    };
}
