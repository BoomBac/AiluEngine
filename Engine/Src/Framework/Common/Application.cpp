#include "Framework/Common/Application.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/EngineConfig.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"
#include "UI/UIRenderer.h"
#include "UI/UIFramework.h"
#include "UI/UILayer.h"
#include "pch.h"
#include <Render/Gizmo.h>
#ifdef PLATFORM_WINDOWS
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


using namespace Ailu::Render;

namespace Ailu
{
#define BIND_EVENT_HANDLER(f) std::bind(&Application::f, this, std::placeholders::_1)
    TimeMgr *g_pTimeMgr = new TimeMgr();
    SceneManagement::SceneMgr *g_pSceneMgr = new SceneManagement::SceneMgr();
    ResourceMgr *g_pResourceMgr = new ResourceMgr();
    Scope<Core::ThreadPool> g_pThreadTool = MakeScope<Core::ThreadPool>(6u, "GlobalThreadPool");

    WString Application::GetWorkingPath()
    {
#ifdef PLATFORM_WINDOWS
        TCHAR path[MAX_PATH];
        GetModuleFileName(NULL, path, MAX_PATH);
        return PathUtils::ExtarctDirectory(WString(path));
#else
        AL_ASSERT(PLATFORM_WINDOWS == 1)
#endif// WINDOWS
    }
    WString Application::GetAppCachePath()
    {
        return GetWorkingPath() + L"cache/";
    }
    WString Application::GetUseHomePath()
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

    int Application::Initialize()
    {
        ApplicationDesc desc;
        desc._window_width = 1600;
        desc._window_height = 900;
        desc._gameview_width = 1600;
        desc._gameview_height = 900;
        return Initialize(desc);
    }

    int Application::Initialize(ApplicationDesc desc)
    {
        AL_ASSERT_MSG(sp_instance == nullptr, "Application already init!");
        ObjectRegister::Initialize();
        Enum::InitTypeInfo();
        Core::Allocator::Init();
        _raw_event_queue = MakeScope<Core::RawEventQueue>();
        g_pTimeMgr->Initialize();
        g_pTimeMgr->Mark();
        sp_instance = this;
        LogMgr::Get().AddAppender(new FileAppender());
        //Load ini
        {
            auto work_path = GetWorkingPath();
            work_path = Application::GetUseHomePath();
            LOG_INFO(L"WorkPath: {}", work_path);
            //AlluEngine/
            WString prex_w = work_path + L"OneDrive/AiluEngine/";
            ResourceMgr::ConfigRootPath(prex_w);
            _engin_config_path = prex_w + L"Editor/EngineConfig.json";
            JsonArchive ar;
            ar.Load(_engin_config_path);
            Type *type = EngineConfig::StaticType();
            for (auto &it: type->GetProperties())
                it.Deserialize(&s_engine_config, ar);
        }
        _is_multi_thread_rendering = s_engine_config.isMultiThreadRender;
        //LogMgr::Get().AddAppender(new ConsoleAppender());
        _p_window = std::move(WindowFactory::Create(s_engine_config.isMultiThreadRender ? L"AiluEngine -mt" : L"AiluEngine", desc._window_width, desc._window_height));
        _p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
        _layer_stack = new LayerStack();
        //PushLayer(new UI::UILayer());
#ifdef DEAR_IMGUI
        //初始化imgui gfx时要求imgui window已经初始化
        _p_imgui_layer = new ImGUILayer();
#endif// DEAR_IMGUI
        JobSystem::Init(6u);
        GraphicsContext::InitGlobalContext();
        GraphicsContext::Get().RegisterWindow(_p_window.get());
        RenderTexture::s_backbuffer = RenderTexture::WindowBackBuffer(&Application::Get().GetWindow());
        g_pGfxContext->ResizeSwapChain(_p_window->GetNativeWindowPtr(), desc._window_width, desc._window_height);
        g_pResourceMgr->Initialize();
        Gizmo::Initialize();
        UI::UIManager::Init();
#ifdef DEAR_IMGUI
        PushLayer(_p_imgui_layer);
#endif// DEAR_IMGUI
        g_pSceneMgr->Initialize();
        SetThreadName("MainThread");
        _state = EApplicationState::EApplicationState_Running;
        _render_lag = s_target_lag;
        _update_lag = s_target_lag;
        _is_handling_event.store(true);
        LOG_INFO("Application Initialize Success with {} s", 0.001f * g_pTimeMgr->GetElapsedSinceLastMark());
        _before_update += []()
        {
            Profiler::Get().BeginFrame();
        };
        _after_update += [this]()
        {
            Profiler::Get().EndFrame();
            g_pThreadTool->ClearRecords();
            _frame_count++;
        };
        return 0;
    }

    void Application::Finalize()
    {
        JsonArchive ar;
        Type *type = EngineConfig::StaticType();
        for (auto &it: type->GetProperties())
            it.Serialize(&s_engine_config,ar);
        ar.Save(_engin_config_path);
        DESTORY_PTR(_layer_stack);
        UI::UIManager::Shutdown();
        Gizmo::Shutdown();
        g_pSceneMgr->Finalize();
        g_pResourceMgr->Finalize();
        DESTORY_PTR(g_pSceneMgr);
        DESTORY_PTR(g_pResourceMgr);
        GraphicsContext::Get().UnRegisterWindow(_p_window.get());
        GraphicsContext::FinalizeGlobalContext();
        g_pTimeMgr->Finalize();
        DESTORY_PTR(g_pTimeMgr);
        JobSystem::Shutdown();
        ObjectRegister::Shutdown();
        Core::Allocator::Get().PrintLeaks();
        Core::Allocator::Shutdown();
    }

    void Application::Tick(f32 delta_time)
    {
        g_pTimeMgr->Reset();
        std::thread logic_thread = std::thread([&]()
                                               {
            SetThreadName("LogicThread");
            while (_state == EApplicationState::EApplicationState_Running || _state == EApplicationState::EApplicationState_Pause)
            {
                LogicLoop();
            }
            LOG_INFO("Exit Logic Thread"); });
        while (_state != EApplicationState::EApplicationState_Exit)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            _p_window->OnUpdate();
            _dispatcher.PumpTasks();
            if (_state == EApplicationState::EApplicationState_Pause)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (logic_thread.joinable())
            logic_thread.join();
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
        //LOG_INFO("Application::WaitForRender...");
        CPUProfileBlock b("Application::WaitForRender");
        std::unique_lock<std::mutex> lock(_mutex);
        _main_wait.wait(lock, [this]
                        { return _render_finished; });
        _render_finished = false;
    }

    void Application::WaitForMain()
    {
        //LOG_INFO("Application::WaitForMain...");
        CPUProfileBlock b("Application::WaitForMain");
        std::unique_lock<std::mutex> lock(_mutex);
        _render_wait.wait(lock, [this]
                          { return _main_finished || _state == EApplicationState::EApplicationState_Exit; });
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

    void Application::SetCursor(ECursorType type)
    {
        _cursor_type = type;
    }

    Application &Application::Get()
    {
        return *sp_instance;
    }
    bool Application::OnWindowClose(WindowCloseEvent &e)
    {
        if (e._window == s_focus_window)
            s_focus_window = nullptr;
        if (e._window == _p_window.get())
        {
            _state = EApplicationState::EApplicationState_Exit;
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
        _state = EApplicationState::EApplicationState_Running;
        return true;
    }
    bool Application::OnWindowMinimize(WindowMinimizeEvent &e)
    {
        _state = EApplicationState::EApplicationState_Pause;
        g_pTimeMgr->Reset();
        LOG_WARNING("Application state: {}", EApplicationState::ToString(_state))
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
        if (e.IsBegin())
        {
            g_pTimeMgr->Pause();
        }
        else
        {
            g_pTimeMgr->Resume();
        }
        return true;
    }
    bool Application::OnDragFile(DragFileEvent &e)
    {
        return false;
    }

    void Application::OnEvent(Event &e)
    {
        EventDispather dispather(e);
        dispather.Dispatch<WindowCloseEvent>(BIND_EVENT_HANDLER(OnWindowClose));
        dispather.Dispatch<WindowFocusEvent>(BIND_EVENT_HANDLER(OnGetFocus));
        dispather.Dispatch<WindowLostFocusEvent>(BIND_EVENT_HANDLER(OnLostFocus));
        dispather.Dispatch<DragFileEvent>(BIND_EVENT_HANDLER(OnDragFile));
        dispather.Dispatch<WindowMinimizeEvent>(BIND_EVENT_HANDLER(OnWindowMinimize));
        dispather.Dispatch<WindowResizeEvent>(BIND_EVENT_HANDLER(OnWindowResize));
        dispather.Dispatch<WindowMovedEvent>(BIND_EVENT_HANDLER(OnWindowMove));
        dispather.Dispatch<MouseButtonReleasedEvent>(BIND_EVENT_HANDLER(OnMouseUp));
        dispather.Dispatch<MouseButtonPressedEvent>(BIND_EVENT_HANDLER(OnMouseDown));
        dispather.Dispatch<MouseMovedEvent>(BIND_EVENT_HANDLER(OnMouseMove));
        dispather.Dispatch<MouseScrollEvent>(BIND_EVENT_HANDLER(OnMouseScroll));
        dispather.Dispatch<KeyPressedEvent>(BIND_EVENT_HANDLER(OnKeyDown));
        dispather.Dispatch<KeyReleasedEvent>(BIND_EVENT_HANDLER(OnKeyUp));
        dispather.Dispatch<MouseSetCursorEvent>(BIND_EVENT_HANDLER(OnSetCursor));
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
        //_cursor_type = static_cast<ECursorType>(e.GetCursorType());
        SetCursorInternal();
        return true;
    }

    void Application::LogicLoop()
    {
        f32 delta_time = TimeMgr::s_delta_time;
        if (_state == EApplicationState::EApplicationState_Pause)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return;
        }
        _before_update_delegate.Invoke();
        auto last_mark = g_pTimeMgr->GetElapsedSinceLastMark();
        g_pTimeMgr->Tick(last_mark);
        _render_lag += last_mark;
        _update_lag += last_mark;
        Input::BeginFrame();
        g_pTimeMgr->Mark();
        {
            CPUProfileBlock main_b("Application::Tick");
            {
                CPUProfileBlock main_b("Application::OnEvent");
                static u8 event_mem[Core::RawEventQueue::MAX_EVENT_SIZE];
                while (auto e = _raw_event_queue->Pop(event_mem))
                {
                    if (e != nullptr)
                    {
                        for (auto it = _layer_stack->end(); it != _layer_stack->begin();)
                        {
                            (*--it)->OnEvent(*e);
                            if (e->Handled()) break;
                        }
                    }
                }
            }
            //处理窗口信息之后，才会进入暂停状态，也就是说暂停状态后的第一帧还是会执行，
            //这样会导致计时器会留下最后一个时间戳，再次回到渲染时，会有一个非常大的lag使得update错误
            if (_state == EApplicationState::EApplicationState_Pause)
                return;
            else if (_state == EApplicationState::EApplicationState_Exit)
                return;
            else {};
            //此时更新已经传入delta_time了，所以不需要多次更新，也就是while(_update_lag >= s_target_lag)
            //这也是固定频率更新，所以实际上delta_time始终为1
            //if (_update_lag >= s_target_lag)
            {
                CPUProfileBlock b("LayerUpdate");
                //if (_update_lag >= s_target_lag)
                {
                    g_pResourceMgr->Tick(delta_time);
                    for (Layer *layer: *_layer_stack)
                    {
                        //layer->OnUpdate((f32)(_update_lag / s_target_lag));
                        layer->OnUpdate(1.0f);
                    }
                    //_update_lag -= s_target_lag;
                    //_update_lag = std::max<f32>(_update_lag,0.0f);
                    //_update_lag = last_mark;
                }
            }
            //if (_render_lag >= s_target_lag)
            {
                {
                    CPUProfileBlock b("SceneTick");
                    g_pSceneMgr->Tick(delta_time);
                }
                {
                    CPUProfileBlock b("RenderScene");
                    Render::RenderPipeline::Get().Render();
                }
#ifdef DEAR_IMGUI
                {
                    CPUProfileBlock b("RenderImGui");
                    _p_imgui_layer->Begin();
                    for (Layer *layer: *_layer_stack)
                        layer->OnImguiRender();
                    _p_imgui_layer->End();
                }
#endif// DEAR_IMGUI
                g_pGfxContext->Present();
                Render::RenderPipeline::Get().FrameCleanUp();
                _render_lag -= s_target_lag;
            }
            //锁帧处理
            {
                f64 remaining = s_target_lag - _update_lag;
                if (remaining > 0.0)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds((i64) (remaining * 1000.0)));
                }
                _update_lag = 0.0;
            }
        }
        _after_update_delegate.Invoke();
    }
    
    void Application::SetCursorInternal()
    {
#if PLATFORM_WINDOWS
        HCURSOR cursor = nullptr;
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
        ::SetCursor(s_cursor_map[_cursor_type]);
#endif// PLATFORM_WINDOWS
    }
}// namespace Ailu
