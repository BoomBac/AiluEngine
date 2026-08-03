#include "Framework/Common/Application.h"
#include "Audio/Audio.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/EngineConfig.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Script/ScriptSystem.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"
#include "UI/UIRenderer.h"
#include "UI/UIFramework.h"
#include "UI/UILayer.h"
#include "pch.h"
#include <Render/Gizmo.h>
#ifdef AL_PLATFORM_WINDOWS
//WINDOWS marco CSIDL_PROFILE
#include <Shlobj.h>
#endif

#include "Framework/Common/Input.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/StackTrace.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Parser/TextParser.h"
#include "Objects/Type.h"
#include "Objects/JsonArchive.h"
#include "Render/Features/VolumetricClouds.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderPipeline.h"
#include "Project/ProjectManager.h"
#include <cmath>

#if defined(TRACY_ENABLE)
#include "tracy/Tracy.hpp"
#endif

//#define SEPARATE_LOGIC_THREAD 1

using namespace Ailu::Render;

namespace Ailu
{
    namespace
    {
        const char *ApplicationStateToString(EApplicationState state)
        {
            switch (state)
            {
                case EApplicationState::EApplicationState_None:
                    return "EApplicationState_None";
                case EApplicationState::EApplicationState_Running:
                    return "EApplicationState_Running";
                case EApplicationState::EApplicationState_Pause:
                    return "EApplicationState_Pause";
                case EApplicationState::EApplicationState_Exit:
                    return "EApplicationState_Exit";
                default:
                    return "EApplicationState_None";
            }
        }
    }

#define BIND_EVENT_HANDLER(f) std::bind(&Application::f, this, std::placeholders::_1)

    void Application::LoadEngineConfig()
    {
        JsonArchive ar;
        ar.Load(s_engine_config_path);
        const Type *type = EngineConfig::StaticType();
        for (auto &it: type->GetProperties())
            it.Deserialize(&g_engine_config, ar);
    }

    void Application::ReloadEngineConfig()
    {
        LoadEngineConfig();
        SetMultiThreadRendering(g_engine_config.isMultiThreadRender);
        LOG_INFO(L"Reloaded engine config: {}", s_engine_config_path);
    }

    void Application::SetMultiThreadRendering(bool enabled)
    {
        const bool old_enabled = _is_multi_thread_rendering.load();
        if (old_enabled == enabled)
            return;

        Render::RenderPipeline *pipeline = Render::RenderPipeline::Instance();
        if (!enabled && pipeline && pipeline->NeedWaitForRenderThread() && GetFrameCount() > 0u)
        {
            NotifyRender();
            WaitForRender();
            pipeline->SetRenderThreadFramePending(false);
        }

        if (g_pGfxContext == nullptr)
        {
            _is_multi_thread_rendering.store(enabled);
            return;
        }

        if (enabled)
            g_pGfxContext->SetMultiThreadRendering(true);

        _is_multi_thread_rendering.store(enabled);

        if (!enabled)
            g_pGfxContext->SetMultiThreadRendering(false);

        if (pipeline)
            pipeline->SetRenderThreadFramePending(false);
    }

    WString Application::GetWorkingPath()
    {
#ifdef AL_PLATFORM_WINDOWS
        TCHAR path[MAX_PATH];
        GetModuleFileName(NULL, path, MAX_PATH);
        return PathUtils::ExtarctDirectory(WString(path));
#else
        AL_ASSERT(AL_PLATFORM_WINDOWS == 1)
#endif// WINDOWS
    }
    WString Application::GetAppCachePath()
    {
        return GetWorkingPath() + L"cache/";
    }
    void Application::SetEngineConfigPath(const WString &engine_config_path)
    {
        s_engine_config_path = PathUtils::FormatFilePath(engine_config_path);
    }
    const WString& Application::GetProjectRootPath()
    {
        return ProjectManager::Get().CurrentProject().RootDirectory();
    }

    void Application::SetProjectRootPath(const WString &project_root_path)
    {
        s_project_root_path = PathUtils::NormalizeDirectoryPath(project_root_path);
    }

    WString Application::GetUserHomePath()
    {
        wchar_t userProfile[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, userProfile)))
        {
            return PathUtils::FormatFilePath(WString(userProfile)) + L"/";
        }
        else
        {
            LOG_ERROR("Application::GetUseHomePath: Get User Profile Path Failed");
            return L"";
        }
    }

    WString Application::GetAiluRoot()
    {
        static WString root;
        if (root.empty())
        {
            root = PathUtils::NormalizeDirectoryPath(GetUserHomePath() + L"OneDrive/AiluEngine");
        }
        return root;
    }

    int Application::Initialize()
    {
        ApplicationDesc desc;
        desc._window_width = 1600;
        desc._window_height = 900;
        desc._gameview_width = 1600;
        desc._gameview_height = 900;
        return Initialize(desc,ApplicationInitContext{});
    }

    int Application::Initialize(ApplicationDesc desc,const ApplicationInitContext& init_ctx)
    {
        AL_ASSERT_MSG(sp_instance == nullptr, "Application already init!");
        ObjectRegister::Initialize();
        Enum::InitTypeInfo();
        Allocator::Init();
        _raw_event_queue = MakeScope<Core::RawEventQueue>();
        TimeMgr::Init();
        TimeMgr::Get().Initialize();
        TimeMgr::Get().Mark();
        sp_instance = this;
        s_main_thread_id = std::this_thread::get_id();
        LogMgr::Get().AddAppender(new FileAppender());
        //Load ini
        {
            ProjectManager::Init();
            auto& proj_mgr = ProjectManager::Get();
            if (!proj_mgr.OpenProject(init_ctx._project_file_path))
            {
                LOG_ERROR(L"Open project {} failed!",init_ctx._project_file_path)
                return -1;
            }
            Project& project = proj_mgr.CurrentProject();
            s_project_root_path = GetProjectRootPath();
            LOG_INFO(L"ProjectRoot: {}", s_project_root_path);
            ResourceMgr::ConfigProject(&project);
            ResourceMgr::ConfigEngineResRoot(GetAiluRoot() + L"Engine/Res/");
            ResourceMgr::ConfigEditorResRoot(GetAiluRoot() + L"Editor/Res/");
            LoadEngineConfig();
            _is_multi_thread_rendering.store(g_engine_config.isMultiThreadRender);
        }
        //LogMgr::Get().AddAppender(new ConsoleAppender());
        _p_window = std::move(WindowFactory::Create(g_engine_config.isMultiThreadRender ? L"AiluEngine -mt" : L"AiluEngine", desc._window_width, desc._window_height));
        _p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
        s_focus_window = _p_window.get();
        _input_system = MakeScope<InputSystem>();
        _win_input_backend.Initialize(*_input_system, _p_window.get());
        _layer_stack = new LayerStack();
        //PushLayer(new UI::UILayer());
#ifdef DEAR_IMGUI
        //初始化imgui gfx时要求imgui window已经初始化
        _p_imgui_layer = new ImGUILayer();
#endif// DEAR_IMGUI
    Core::ThreadPool::Init(6u, "GlobalThreadPool");
    JobSystem::Init(6u);
        GraphicsContext::InitGlobalContext();
        GraphicsContext::Get().RegisterWindow(_p_window.get());
        RenderTexture::s_backbuffer = RenderTexture::WindowBackBuffer(&Application::Get().GetWindow());
        g_pGfxContext->ResizeSwapChain(_p_window->GetNativeWindowPtr(), desc._window_width, desc._window_height);
        ResourceMgr::Init();
        ResourceMgr::Get().Initialize();
        ScriptSystem::Get().Initialize();
        AudioDeviceConfig audio_config;
        audio_config._enabled = init_ctx._enable_audio;
        audio_config._device_name = init_ctx._audio_device_name;
        audio_config._sample_rate = init_ctx._audio_sample_rate;
        audio_config._max_voices = init_ctx._audio_max_voices;
        audio_config._max_streaming_voices = init_ctx._audio_max_streaming_voices;
        Audio::Initialize(audio_config);
        Gizmo::Initialize();
        UI::UIManager::Init();
        SceneManagement::SceneMgr::Init();
#ifdef DEAR_IMGUI
        PushLayer(_p_imgui_layer);
#endif// DEAR_IMGUI
        SetThreadName("MainThread");
    #if defined(TRACY_ENABLE)
        tracy::SetThreadName("MainThread");
    #endif
        _state.store(EApplicationState::EApplicationState_Running);
        _render_lag = s_target_lag;
        _update_lag = s_target_lag;
        _is_handling_event.store(true);
        LOG_INFO("Application Initialize Success with {} s", 0.001f * TimeMgr::Get().GetElapsedSinceLastMark());
        _before_update += []()
        {
            Profiler::Get().BeginFrame();
        };
        _after_update += [this]()
        {
            Profiler::Get().EndFrame();
            Core::ThreadPool::Get().ClearRecords();
            _frame_count++;
        };
        return 0;
    }

    void Application::Finalize()
    {
        JsonArchive ar;
        const Type *type = EngineConfig::StaticType();
        for (auto &it: type->GetProperties())
            it.Serialize(&g_engine_config,ar);
        ar.Save(s_engine_config_path);
        _win_input_backend.Shutdown(*_input_system);
        _input_system.reset();
        delete _layer_stack; _layer_stack = nullptr;
        UI::UIManager::Shutdown();
        Gizmo::Shutdown();
        SceneManagement::SceneMgr::Shutdown();
        Audio::Shutdown();
        ScriptSystem::Get().Finalize();
        ResourceMgr::Get().Finalize();
        ResourceMgr::Shutdown();
        GraphicsContext::Get().UnRegisterWindow(_p_window.get());
        GraphicsContext::FinalizeGlobalContext();
        Core::ThreadPool::Shutdown();
        TimeMgr::Get().Finalize();
        TimeMgr::Shutdown();
        JobSystem::Shutdown();
        ProjectManager::Shutdown();
        ObjectRegister::Shutdown();
        Allocator::Get().PrintLeaks();
        Allocator::Shutdown();
    }

    void Application::Tick(f32 delta_time)
    {
        TimeMgr::Get().Reset();
#if defined(SEPARATE_LOGIC_THREAD)
        std::thread logic_thread = std::thread([&]()
                                               {
            SetThreadName("LogicThread");
#if defined(TRACY_ENABLE)
            tracy::SetThreadName("LogicThread");
#endif
            while (State() == EApplicationState::EApplicationState_Running || State() == EApplicationState::EApplicationState_Pause)
            {
                LogicLoop();
            }
            LOG_INFO("Exit Logic Thread"); });
        while (State() != EApplicationState::EApplicationState_Exit)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            {
                PROFILE_BLOCK_CPU("Application::WindowUpdate")
                _p_window->OnUpdate();
            }
            {
                PROFILE_BLOCK_CPU("Application::PumpTasks")
                _dispatcher.PumpTasks();
            }
            if (State() == EApplicationState::EApplicationState_Pause)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (logic_thread.joinable())
            logic_thread.join();
#else
        while (State() != EApplicationState::EApplicationState_Exit)
        {
            {
                PROFILE_BLOCK_CPU("Application::WindowUpdate")
                _p_window->OnUpdate();
            }
            {
                PROFILE_BLOCK_CPU("Application::LogicLoop")
                LogicLoop();
            }
            {
                PROFILE_BLOCK_CPU("Application::PumpTasks")
                _dispatcher.PumpTasks();
            }
            if (State() == EApplicationState::EApplicationState_Pause)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
#endif
        LOG_INFO("Exit");

    }
    void Application::PushLayer(Layer *layer)
    {
        _layer_stack->PushLayer(layer);
    }
    void Application::PushOverLayer(Layer *layer)
    {
        _layer_stack->PushOverLayer(layer);
    }
    void Application::WaitForRender()
    {
        PROFILE_BLOCK_CPU("Application::WaitForRender")
        std::unique_lock<std::mutex> lock(_mutex);
        _main_wait.wait(lock, [this]
                        { return _render_finished; });
        _render_finished = false;
    }

    void Application::WaitForMain()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _render_wait.wait(lock, [this]
                          { return _main_finished || State() == EApplicationState::EApplicationState_Exit; });
        _main_finished = false;
    }

    void Application::NotifyMain()
    {
        //LOG_INFO("Application::NotifyMain");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _render_finished = true;
        }
        _main_wait.notify_one();
    }

    void Application::NotifyRender()
    {
        //LOG_INFO("Application::NotifyRender");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _main_finished = true;
        }
        _render_wait.notify_one();
    }

    void Application::SetCursor(ECursorType type, ECursorPriority priority)
    {
        if ((u8) priority < (u8) _cursor_priority)
            return;
        _cursor_type = type;
        _cursor_priority = priority;
        SetCursorInternal(_cursor_type);
    }

    Application &Application::Get()
    {
        return *sp_instance;
    }
    bool Application::IsMainThread()
    {
        return std::this_thread::get_id() == s_main_thread_id;
    }
    bool Application::OnWindowClose(WindowCloseEvent &e)
    {
        if (e._window == s_focus_window)
            s_focus_window = nullptr;
        if (e._window == _p_window.get())
        {
            _state.store(EApplicationState::EApplicationState_Exit);
            _main_finished = true;
            _is_handling_event.store(false);
            NotifyRender();
        }
        return false;
    }
    const ObjectLayer &Application::NameToLayer(const String &name)
    {
        auto it = std::find_if(_object_layers.begin(), _object_layers.end(), [&](const ObjectLayer &cur_layer) -> bool
                               { return name == cur_layer._name; });
        return it == _object_layers.end() ? _object_layers[1] : *it;
    }
    bool Application::OnLostFocus(WindowLostFocusEvent &e)
    {
        if (e._window == s_focus_window)
            s_focus_window = nullptr;
        //s_target_lag = 1000.0f / 5.0f;
        return true;
    }
    bool Application::OnGetFocus(WindowFocusEvent &e)
    {
        s_focus_window = e._window;
        s_target_lag = kMsPerRender;
        _state.store(EApplicationState::EApplicationState_Running);
        return true;
    }
    bool Application::OnWindowMinimize(WindowMinimizeEvent &e)
    {
        if (e._window != _p_window.get())
            return false;
        _state.store(EApplicationState::EApplicationState_Pause);
        TimeMgr::Get().Reset();
        LOG_WARNING("Application state: {}", ApplicationStateToString(State()))
        return false;
    }
    bool Application::OnWindowResize(WindowResizeEvent &e)
    {
        //if (_state == EApplicationState::EApplicationState_Pause)
        //	_state = EApplicationState::EApplicationState_Running;
        //LogMgr::Get().LogWarningFormat("Application state: {}", EApplicationState::ToString(_state));
        GraphicsContext::Get().ResizeSwapChain(e._handle,(u32)e.GetWidth(), (u32)e.GetHeight());
        LOG_INFO("Window resize: {}x{}", e.GetWidth(), e.GetHeight());
        _on_window_resize_delegate.Invoke(e._window,Vector2f{e.GetWidth(), e.GetHeight()});
        return false;
    }
    bool Application::OnWindowMove(WindowMovedEvent &e)
    {
        if (e._window != _p_window.get())
            return true;
        if (e.IsBegin())
        {
            TimeMgr::Get().Pause();
        }
        else
        {
            TimeMgr::Get().Resume();
        }
        return true;
    }
    bool Application::OnDragFile(DragFileEvent &e)
    {
        if (e.GetDragedFilesPath().empty())
            return false;
        std::lock_guard lock(_drop_files_mtx);
        _drop_files = e.GetDragedFilesPath();
        _drop_mouse_pos = e.HasPosition() ? Vector2f(e.GetX(), e.GetY()) : Input::GetMousePos(e._window);
        _has_drop_mouse_pos = true;
        _has_drop_files = true;
        return false;
    }

    void Application::UpdatePlatformEventState(Event &e)
    {
        switch (e.GetEventType())
        {
        case EEventType::kWindowClose:
            Input::NotifyWindowClosed(e._window);
            break;
        case EEventType::kWindowLostFocus:
            Input::NotifyFocusLost();
            break;
        case EEventType::kKeyPressed:
            Input::NotifyKeyPressed(static_cast<EKey>(static_cast<KeyPressedEvent &>(e).GetKeyCode()));
            break;
        case EEventType::kKeyReleased:
            Input::NotifyKeyReleased(static_cast<EKey>(static_cast<KeyReleasedEvent &>(e).GetKeyCode()));
            break;
        case EEventType::kMouseButtonPressed:
            Input::NotifyKeyPressed(static_cast<EKey>(static_cast<MouseButtonPressedEvent &>(e).GetButton()));
            break;
        case EEventType::kMouseButtonReleased:
            Input::NotifyKeyReleased(static_cast<EKey>(static_cast<MouseButtonReleasedEvent &>(e).GetButton()));
            break;
        case EEventType::kMouseMoved:
        {
            auto &mouse_event = static_cast<MouseMovedEvent &>(e);
            Input::NotifyMouseMove(e._window, Vector2f(mouse_event.GetX(), mouse_event.GetY()));
            break;
        }
        default:
            break;
        }

        EventDispather dispather(e);
        dispather.Dispatch<WindowCloseEvent>(BIND_EVENT_HANDLER(OnWindowClose));
        dispather.Dispatch<WindowFocusEvent>(BIND_EVENT_HANDLER(OnGetFocus));
        dispather.Dispatch<WindowLostFocusEvent>(BIND_EVENT_HANDLER(OnLostFocus));
        dispather.Dispatch<DragFileEvent>(BIND_EVENT_HANDLER(OnDragFile));
        dispather.Dispatch<WindowMinimizeEvent>(BIND_EVENT_HANDLER(OnWindowMinimize));
        dispather.Dispatch<WindowResizeEvent>(BIND_EVENT_HANDLER(OnWindowResize));
        dispather.Dispatch<WindowMovedEvent>(BIND_EVENT_HANDLER(OnWindowMove));
        dispather.Dispatch<MouseSetCursorEvent>(BIND_EVENT_HANDLER(OnSetCursor));
    }

    void Application::OnEvent(Event &e)
    {
        UpdatePlatformEventState(e);
#if defined(SEPARATE_LOGIC_THREAD)
        EventDispather dispather(e);
        dispather.Dispatch<MouseButtonReleasedEvent>(BIND_EVENT_HANDLER(OnMouseUp));
        dispather.Dispatch<MouseButtonPressedEvent>(BIND_EVENT_HANDLER(OnMouseDown));
        dispather.Dispatch<MouseMovedEvent>(BIND_EVENT_HANDLER(OnMouseMove));
        dispather.Dispatch<MouseScrollEvent>(BIND_EVENT_HANDLER(OnMouseScroll));
        dispather.Dispatch<KeyPressedEvent>(BIND_EVENT_HANDLER(OnKeyDown));
        dispather.Dispatch<KeyReleasedEvent>(BIND_EVENT_HANDLER(OnKeyUp));
#else
        for (auto it = _layer_stack->end(); it != _layer_stack->begin();)
        {
            (*--it)->OnEvent(e);
            if (e.Handled()) break;
        }
#endif
    }

    bool Application::OnMouseDown(MouseButtonPressedEvent &e)
    {
        _raw_event_queue->Push<MouseButtonPressedEvent>(e);
        return true;
    }

    bool Application::OnMouseUp(MouseButtonReleasedEvent &e)
    {
        _raw_event_queue->Push<MouseButtonReleasedEvent>(e);
        return true;
    }

    bool Application::OnMouseMove(MouseMovedEvent &e)
    {
        _raw_event_queue->Push<MouseMovedEvent>(e);
        return true;
    }

    bool Application::OnMouseScroll(MouseScrollEvent &e)
    {
        _raw_event_queue->Push<MouseScrollEvent>(e);
        return true;
    }

    bool Application::OnKeyDown(KeyPressedEvent &e)
    {
        _raw_event_queue->Push<KeyPressedEvent>(e);
        return true;
    }

    bool Application::OnKeyUp(KeyReleasedEvent &e)
    {
        _raw_event_queue->Push<KeyReleasedEvent>(e);
        return true;
    }

    bool Application::OnSetCursor(MouseSetCursorEvent &e)
    {
        _is_client_cursor_context = e.IsClientArea();
        if (_is_client_cursor_context)
        {
            SetCursorInternal(_cursor_type);
            return true;
        }
        if (e.GetCursorType() != MouseSetCursorEvent::kUseCurrentCursor)
            SetCursorInternal(static_cast<ECursorType>(e.GetCursorType()));
        return true;
    }

    void Application::LogicLoop()
    {
#if defined(TRACY_ENABLE)
        ZoneScopedN("Application::LogicLoop");
#endif
        const f32 delta_time = TimeMgr::s_delta_time;
        if (State() == EApplicationState::EApplicationState_Pause)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return;
        }
        BeginFrame();
        UpdateInputAndEvents(delta_time);
        // 处理窗口信息之后，才会进入暂停状态，也就是说暂停状态后的第一帧还是会执行，
        // 这样会导致计时器会留下最后一个时间戳，再次回到渲染时，会有一个非常大的 lag 使得 update 错误。
        if (State() == EApplicationState::EApplicationState_Pause || State() == EApplicationState::EApplicationState_Exit)
            return;

        UpdateResources(delta_time);
        UpdateLayers(delta_time);
        UpdateScenes(delta_time);
        PrepareRender();
        RenderFrame();
        RenderEditor();
        PresentFrame();
        EndFrame();
    }

    void Application::BeginFrame()
    {
        PROFILE_BLOCK_CPU("Application::BeginFrame")
        _before_update_delegate.Invoke();
        const auto last_mark = TimeMgr::Get().GetElapsedSinceLastMark();
        TimeMgr::Get().Tick(last_mark);
        _render_lag += last_mark;
        _update_lag += last_mark;
        Input::BeginFrame();
        BeginCursorFrame();
        TimeMgr::Get().Mark();
    }

    void Application::UpdateInputAndEvents(f32 delta_time)
    {
        PROFILE_BLOCK_CPU("Application::Input")
#if defined(TRACY_ENABLE)
        ZoneScopedN("UI::Update + Events");
#endif
        _input_system->Update(delta_time);
        UI::UIManager::Get()->Update(delta_time);
#if defined(SEPARATE_LOGIC_THREAD)
        PROFILE_BLOCK_CPU(Application_OnEvent)
        static u8 event_mem[Core::RawEventQueue::MAX_EVENT_SIZE];
        while (auto e = _raw_event_queue->Pop(event_mem))
        {
            if (e == nullptr)
                continue;
            for (auto it = _layer_stack->end(); it != _layer_stack->begin();)
            {
                (*--it)->OnEvent(*e);
                if (e->Handled())
                    break;
            }
        }
        if (_has_drop_files)
        {
            std::lock_guard lock(_drop_files_mtx);
            DragFileEvent e = _has_drop_mouse_pos ? DragFileEvent(_drop_files, _drop_mouse_pos.x, _drop_mouse_pos.y) :
                                                    DragFileEvent(_drop_files);
            e._window = _p_window.get();
            for (auto it = _layer_stack->end(); it != _layer_stack->begin();)
            {
                (*--it)->OnEvent(e);
                if (e.Handled())
                    break;
            }
            _drop_files.clear();
            _has_drop_mouse_pos = false;
            _has_drop_files = false;
        }
#else
        (void) delta_time;
#endif
    }

    void Application::UpdateResources(f32 delta_time)
    {
        PROFILE_BLOCK_CPU("Application::Resources")
        ResourceMgr::Get().Tick(delta_time);
    }

    void Application::UpdateLayers(f32 delta_time)
    {
        PROFILE_BLOCK_CPU("Application::Layers")
#if defined(TRACY_ENABLE)
        ZoneScopedN("LayerUpdate");
#endif
        for (Layer *layer: *_layer_stack)
        {
            layer->OnUpdate(delta_time);
        }
    }

    void Application::UpdateScenes(f32 delta_time)
    {
        PROFILE_BLOCK_CPU("Application::Scenes")
#if defined(TRACY_ENABLE)
        ZoneScopedN("SceneTick");
#endif
        ScriptSystem::Get().Tick(delta_time);
        _fixed_accumulator += delta_time;

        u32 fixed_step_count = 0u;
        while (_fixed_accumulator >= kFixedDeltaTime && fixed_step_count < kMaxFixedStepsPerFrame)
        {
            SceneManagement::SceneMgr::Get().FixedUpdate(kFixedDeltaTime);
            _fixed_accumulator -= kFixedDeltaTime;
            ++fixed_step_count;
        }

        if (fixed_step_count == kMaxFixedStepsPerFrame && _fixed_accumulator >= kFixedDeltaTime)
            _fixed_accumulator = std::fmod(_fixed_accumulator, kFixedDeltaTime);

        SceneManagement::SceneMgr::Get().Update(delta_time);
        const f32 render_alpha = _fixed_accumulator / kFixedDeltaTime;
        SceneManagement::SceneMgr::Get().LateUpdate(delta_time, render_alpha);
    }

    void Application::PrepareRender()
    {
        PROFILE_BLOCK_CPU("Application::PreRender")
    }

    void Application::RenderFrame()
    {
        PROFILE_BLOCK_CPU("Application::Render")
#if defined(TRACY_ENABLE)
        ZoneScopedN("RenderScene");
#endif
        Render::RenderPipeline::Get().Render();
    }

    void Application::RenderEditor()
    {
#ifdef DEAR_IMGUI
        PROFILE_BLOCK_CPU("Application::EditorRender")
#if defined(TRACY_ENABLE)
        ZoneScopedN("RenderImGui");
#endif
        _p_imgui_layer->Begin();
        for (Layer *layer: *_layer_stack)
            layer->OnImguiRender();
        _p_imgui_layer->End();
#endif// DEAR_IMGUI
    }

    void Application::PresentFrame()
    {
        PROFILE_BLOCK_CPU("Application::Present")
        g_pGfxContext->Present();
        if (_is_multi_thread_rendering.load())
            Render::RenderPipeline::Get().SetRenderThreadFramePending(true);
        _render_lag -= s_target_lag;
    }

    void Application::EndFrame()
    {
        PROFILE_BLOCK_CPU("Application::EndFrame")
        _after_update_delegate.Invoke();

        if (_is_multi_thread_rendering.load())
            NotifyRender();

    #if defined(TRACY_ENABLE)
        FrameMark;
    #endif
    }

    void Application::BeginCursorFrame()
    {
        _cursor_type = ECursorType::kArrow;
        _cursor_priority = ECursorPriority::kFallback;
    }
    
    void Application::SetCursorInternal(ECursorType type)
    {
#if AL_PLATFORM_WINDOWS
        static bool s_init = false;
        static HashMap<ECursorType,HCURSOR> s_cursor_map{};
        if (!s_init)
        {
            s_init = true;
            s_cursor_map[ECursorType::kArrow] = LoadCursor(NULL, IDC_ARROW);
            s_cursor_map[ECursorType::kSizeNS] = LoadCursor(NULL, IDC_SIZENS);
            s_cursor_map[ECursorType::kSizeEW] = LoadCursor(NULL, IDC_SIZEWE);
            s_cursor_map[ECursorType::kSizeNWSE] = LoadCursor(NULL, IDC_SIZENWSE);
            s_cursor_map[ECursorType::kSizeNESW] = LoadCursor(NULL, IDC_SIZENESW);
            s_cursor_map[ECursorType::kHand] = LoadCursor(NULL, IDC_HAND);
        }
        ::SetCursor(s_cursor_map[type]);
#endif// AL_PLATFORM_WINDOWS
    }
}// namespace Ailu
