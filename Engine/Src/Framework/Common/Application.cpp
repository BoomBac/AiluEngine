#include "Framework/Common/Application.h"
#include "CompanyEnv.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/EngineConfig.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"
#include "UI/UIRenderer.h"
#include "pch.h"
#include <Render/Gizmo.h>
#include <Render/TextRenderer.h>
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
#include "Render/Features/VolumetricClouds.h"
#include "Render/GraphicsContext.h"
#include "Render/RenderPipeline.h"

#include "Objects/ReflectData.h"

using namespace Ailu::Render;

namespace Ailu
{
#define BIND_EVENT_HANDLER(f) std::bind(&Application::f, this, std::placeholders::_1)
    TimeMgr *g_pTimeMgr = new TimeMgr();
    SceneManagement::SceneMgr *g_pSceneMgr = new SceneManagement::SceneMgr();
    ResourceMgr *g_pResourceMgr = new ResourceMgr();
    LogMgr *g_pLogMgr = new LogMgr();
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
        g_pTimeMgr->Initialize();
        g_pTimeMgr->Mark();
        sp_instance = this;
        //Load ini
        {
            auto work_path = GetWorkingPath();
            work_path = Application::GetUseHomePath();
            LOG_INFO(L"WorkPath: {}", work_path);
            //AlluEngine/
            WString prex_w = work_path + L"OneDrive/AiluEngine/";
            ResourceMgr::ConfigRootPath(prex_w);
            _engin_config_path = prex_w + L"Editor/EngineConfig.ini";
            INIParser ini_parser;
            if (ini_parser.Load(_engin_config_path))
            {
                Type *type = EngineConfig::StaticType();
                for (auto &it: ini_parser.GetValues("Layer"))
                {
                    auto &[k, v] = it;
                    {
                        ObjectLayer cur_layer;
                        cur_layer._name = k;
                        cur_layer._value = std::stoul(v);
                        cur_layer._bit_pos = (u32) sqrt((f32) cur_layer._value);
                        type->FindPropertyByName(cur_layer._name)->SetValue<u32>(s_engine_config, cur_layer._value);
                    }
                }
                s_engine_config.isMultiThreadRender = (bool) std::stoul(ini_parser.GetValue("Render", "isMultiThreadRender", "0"));
            }
        }
        _is_multi_thread_rendering = s_engine_config.isMultiThreadRender;
        g_pLogMgr->Initialize();
        g_pLogMgr->AddAppender(new FileAppender());
        //g_pLogMgr->AddAppender(new ConsoleAppender());
        auto window_props = WindowProps();
        window_props.Width = desc._window_width;
        window_props.Height = desc._window_height;
        _p_window = new WinWindow(window_props);
        _p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
        _p_window->SetTitle(s_engine_config.isMultiThreadRender ? L"AiluEngine -mt" : L"AiluEngine");
        _layer_stack = new LayerStack();
#ifdef DEAR_IMGUI
        //初始化imgui gfx时要求imgui window已经初始化
        _p_imgui_layer = new ImGUILayer();
#endif// DEAR_IMGUI
        JobSystem::Init(6u);
        GraphicsContext::InitGlobalContext();
        g_pGfxContext->ResizeSwapChain(desc._window_width, desc._window_height);
        g_pResourceMgr->Initialize();
        Gizmo::Initialize();
        TextRenderer::Init();
        UI::UIRenderer::Init();
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
        // {
        //     TIMER_BLOCK("TestAlloc");
        //     for (u16 i = 0; i < 2000; i++)
        //     {
        //         u32 *a = AL_NEW(u32);
        //         *a = 20;
        //         AL_FREE(a);
        //     }
        // }
        // {
        //     TIMER_BLOCK("TestSysAlloc");
        //     for (u16 i = 0; i < 2000; i++)
        //     {
        //         u32 *a = new u32();
        //         *a = 20;
        //         delete a;
        //     }
        // }
        // Core::Allocator::Get().PrintLeaks();
        return 0;
    }

    void Application::Finalize()
    {
        INIParser ini_parser;
        Type *type = EngineConfig::StaticType();
        for (const auto &it: type->GetProperties())
        {
            if (it.DataType() == EDataType::kUInt32)
            {
                ini_parser.SetValue(it.MetaInfo()._category, it.Name(), std::format("{}", it.GetValue<u32>(s_engine_config)));
            }
            else if (it.DataType() == EDataType::kBool)
            {
                ini_parser.SetValue(it.MetaInfo()._category, it.Name(), std::format("{}", (u32) it.GetValue<bool>(s_engine_config)));
            }
        }
        if (ini_parser.Save(_engin_config_path))
        {
            LOG_INFO("Application::Finalize: write engine config...");
        }
        DESTORY_PTR(_layer_stack);
        //DESTORY_PTR(_p_event_handle_thread);
        DESTORY_PTR(_p_window);
        TextRenderer::Shutdown();
        UI::UIRenderer::Shutdown();
        Gizmo::Shutdown();
        g_pSceneMgr->Finalize();
        g_pResourceMgr->Finalize();
        DESTORY_PTR(g_pSceneMgr);
        DESTORY_PTR(g_pResourceMgr);
        GraphicsContext::FinalizeGlobalContext();
        g_pTimeMgr->Finalize();
        DESTORY_PTR(g_pTimeMgr);
        JobSystem::Shutdown();
        ObjectRegister::Shutdown();
        Core::Allocator::Get().PrintLeaks();
        Core::Allocator::Shutdown();
        g_pLogMgr->Finalize();
        DESTORY_PTR(g_pLogMgr);
    }

    void Application::Tick(f32 delta_time)
    {
        g_pTimeMgr->Reset();
        std::thread logic_thread = std::thread([&]()
                                               {
            SetThreadName("LogicThread");
            while (_state == EApplicationState::EApplicationState_Running || _state == EApplicationState::EApplicationState_Pause)
            {
                {
                    if (_state == EApplicationState::EApplicationState_Pause)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        continue;
                    }
                    _before_update.Invoke();
                    auto last_mark = g_pTimeMgr->GetElapsedSinceLastMark();
                    g_pTimeMgr->Tick(last_mark);
                    _render_lag += last_mark;
                    _update_lag += last_mark;
                    g_pTimeMgr->Mark();
                    {
                        CPUProfileBlock main_b("Application::Tick");
                        //处理窗口信息之后，才会进入暂停状态，也就是说暂停状态后的第一帧还是会执行，
                        //这样会导致计时器会留下最后一个时间戳，再次回到渲染时，会有一个非常大的lag使得update错误
                        if (_state == EApplicationState::EApplicationState_Pause)
                            continue;
                        else if (_state == EApplicationState::EApplicationState_Exit)
                            break;
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
                                g_pGfxContext->GetPipeline()->Render();
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
                            g_pGfxContext->GetPipeline()->FrameCleanUp();
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
                    _after_update.Invoke();
                }
            }
            LOG_INFO("Exit Logic Thread"); });
        while (_state != EApplicationState::EApplicationState_Exit)
        {
            if (_state == EApplicationState::EApplicationState_Pause)
            {
                _p_window->OnUpdate();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            else
            {
                _p_window->OnUpdate();
                //std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        if (logic_thread.joinable())
            logic_thread.join();
        LOG_INFO("Exit");
    }
    void Application::PushLayer(Layer *layer)
    {
        _layer_stack->PushLayer(layer);
        layer->OnAttach();
    }
    void Application::PushOverLayer(Layer *layer)
    {
        _layer_stack->PushOverLayer(layer);
        layer->OnAttach();
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
    Application &Application::Get()
    {
        return *sp_instance;
    }
    bool Application::OnWindowClose(WindowCloseEvent &e)
    {
        _state = EApplicationState::EApplicationState_Exit;
        _main_finished = true;
        _is_handling_event.store(false);
        NotifyRender();
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
        s_target_lag = 1000.0f / 5.0f;
        return true;
    }
    bool Application::OnGetFocus(WindowFocusEvent &e)
    {
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
        //g_pLogMgr->LogWarningFormat("Application state: {}", EApplicationState::ToString(_state));
        LOG_INFO("Window resize: {}x{}", e.GetWidth(), e.GetHeight());
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

        for (auto it = _layer_stack->end(); it != _layer_stack->begin();)
        {
            (*--it)->OnEvent(e);
            if (e.Handled()) break;
        }
    }
}// namespace Ailu
