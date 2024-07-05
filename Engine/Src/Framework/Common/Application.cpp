#include "pch.h"
#include "CompanyEnv.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"

#include "Framework/Common/Input.h"
#include "Framework/Common/ThreadPool.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"

namespace Ailu
{
#define BIND_EVENT_HANDLER(f) std::bind(&Application::f,this,std::placeholders::_1)

	TimeMgr* g_pTimeMgr = new TimeMgr();
	SceneMgr* g_pSceneMgr = new SceneMgr();
	ResourceMgr* g_pResourceMgr = new ResourceMgr();
	LogMgr* g_pLogMgr = new LogMgr();
	Renderer* g_pRenderer = new Renderer();
	Scope<ThreadPool> g_pThreadTool = MakeScope<ThreadPool>(18, "GlobalThreadPool");

	WString Application::GetWorkingPath()
	{
#ifdef PLATFORM_WINDOWS
		TCHAR path[MAX_PATH];
		GetModuleFileName(NULL, path, MAX_PATH);
		return PathUtils::FormatFilePath(WString(path));
#else
		AL_ASSERT(PLATFORM_WINDOWS != 1)
#endif // WINDOWS
	}

        double Application::s_target_framecount = Application::kTargetFrameRate;

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
		AL_ASSERT_MSG(sp_instance, "Application already init!");
		sp_instance = this;
		g_pLogMgr->Initialize();
		g_pLogMgr->AddAppender(new FileAppender());
		auto window_props = Ailu::WindowProps();
		window_props.Width = desc._window_width;
		window_props.Height = desc._window_height;
		_p_window = new Ailu::WinWindow(window_props);
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		g_pTimeMgr->Initialize();
		_layer_stack = new LayerStack();
#ifdef DEAR_IMGUI
		//初始化imgui gfx时要求imgui window已经初始化
		_p_imgui_layer = new ImGUILayer();
#endif // DEAR_IMGUI
		GraphicsContext::InitGlobalContext();
		g_pGfxContext->ResizeSwapChain(desc._window_width, desc._window_height);
		g_pLogMgr->Log("Begin init resource mgr...");
		g_pTimeMgr->Mark();
		g_pResourceMgr->Initialize();
#ifdef DEAR_IMGUI
		PushLayer(_p_imgui_layer);
		
#endif // DEAR_IMGUI
		g_pLogMgr->LogFormat("Resource mgr init finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		g_pLogMgr->Log("Begin init scene mgr...");
		g_pTimeMgr->Mark();
		g_pSceneMgr->Initialize();
		g_pLogMgr->LogFormat("Scene mgr init finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		g_pLogMgr->Log("Begin init renderer...");
		g_pTimeMgr->Mark();
		g_pLogMgr->LogFormat("Renderer init finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		SetThreadDescription(GetCurrentThread(), L"ALEngineMainThread");
		g_pLogMgr->Log("Application Init end");
		_state = EApplicationState::EApplicationState_Running;
		_render_lag = s_target_lag;
		_update_lag = s_target_lag;
		_is_handling_event.store(true);
		//_p_event_handle_thread = new std::thread([this]() {
		//	while (_is_handling_event)
		//	{
		//		_p_window->OnUpdate();
		//	}
		//});
		//_p_event_handle_thread->detach();
		return 0;
	}

	void Application::Finalize()
	{
		DESTORY_PTR(_layer_stack);
		//DESTORY_PTR(_p_event_handle_thread);
		DESTORY_PTR(_p_window);
		g_pSceneMgr->Finalize();
		g_pResourceMgr->Finalize();
		DESTORY_PTR(g_pSceneMgr);
		DESTORY_PTR(g_pResourceMgr);
		g_pRenderer->Finalize();
		DESTORY_PTR(g_pRenderer);
		GraphicsContext::FinalizeGlobalContext();
		g_pTimeMgr->Finalize();
		DESTORY_PTR(g_pTimeMgr);
		g_pLogMgr->Finalize();
		DESTORY_PTR(g_pLogMgr);
	}

	void Application::Tick(const float& delta_time)
	{
		g_pTimeMgr->Mark();
		while (_state != EApplicationState::EApplicationState_Exit)
		{
			if (_state == EApplicationState::EApplicationState_Pause)
			{
				_p_window->OnUpdate();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			else
			{
				_p_window->OnUpdate();
				//处理窗口信息之后，才会进入暂停状态，也就是说暂停状态后的第一帧还是会执行，
				//这样会导致计时器会留下最后一个时间戳，再次回到渲染时，会有一个非常大的lag使得update错误
				if (_state == EApplicationState::EApplicationState_Pause)
					continue;
				else if (_state == EApplicationState::EApplicationState_Exit)
					break;
				else {};
				g_pTimeMgr->Tick(0.0f);
				auto last_mark = g_pTimeMgr->GetElapsedSinceLastMark();
				_render_lag += last_mark;
				_update_lag += last_mark;
				//AL_ASSERT(_update_lag > 1000);
				_update_lag = std::min<float>(_update_lag,1000);
				_update_lag = std::max<float>(_update_lag,0.1);
				g_pTimeMgr->Mark();
				if (_update_lag >= s_target_lag)
				{
					g_pResourceMgr->Tick(delta_time);
					for (Layer* layer : *_layer_stack)
					{
						layer->OnUpdate(_update_lag * 0.001f);
					}
					_update_lag -= s_target_lag;
				}
				if (_render_lag >= s_target_lag)
				{
					s_frame_count++;
					g_pSceneMgr->Tick(delta_time);
#ifdef DEAR_IMGUI
					_p_imgui_layer->Begin();
					for (Layer* layer : *_layer_stack)
						layer->OnImguiRender();
					_p_imgui_layer->End();
#endif // DEAR_IMGUI
					g_pRenderer->Tick(delta_time);
					_render_lag -= s_target_lag;
				}
			}
		}
		g_pLogMgr->Log("Exit");
	}
	void Application::PushLayer(Layer* layer)
	{
		_layer_stack->PushLayer(layer);
		layer->OnAttach();
	}
	void Application::PushOverLayer(Layer* layer)
	{
		_layer_stack->PushOverLayer(layer);
		layer->OnAttach();
	}
	Application* Application::GetInstance()
	{
		return sp_instance;
	}
	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		_state = EApplicationState::EApplicationState_Exit;
		_is_handling_event.store(false);
		return false;
	}
	bool Application::OnLostFoucus(WindowLostFocusEvent& e)
	{
		g_pLogMgr->Log("OnLostFoucus");
		s_target_lag = 1000 / 30.0;
		return true;
	}
	bool Application::OnGetFoucus(WindowFocusEvent& e)
	{
		g_pLogMgr->Log("OnGetFoucus");
		s_target_lag = kMsPerRender;
		_state = EApplicationState::EApplicationState_Running;
		return true;
	}
	bool Application::OnWindowMinimize(WindowMinimizeEvent& e)
	{
		_state = EApplicationState::EApplicationState_Pause;
		g_pTimeMgr->Reset();
		LOG_WARNING("Application state: {}", EApplicationState::ToString(_state))
			return false;
	}
	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		//if (_state == EApplicationState::EApplicationState_Pause)
		//	_state = EApplicationState::EApplicationState_Running;
		//g_pLogMgr->LogWarningFormat("Application state: {}", EApplicationState::ToString(_state));
		g_pLogMgr->LogFormat("Window resize: {}x{}", e.GetWidth(), e.GetHeight());
		return false;
	}
	bool Application::OnWindowMove(WindowMovedEvent& e)
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
	bool Application::OnDragFile(DragFileEvent& e)
	{
		auto& draged_files = e.GetDragedFilesPath();
		for (auto& it : draged_files)
		{
			g_pResourceMgr->ImportResourceAsync(it);
		}
		return false;
	}

	void Application::OnEvent(Event& e)
	{
		EventDispather dispather(e);
		dispather.Dispatch<WindowCloseEvent>(BIND_EVENT_HANDLER(OnWindowClose));
		dispather.Dispatch<WindowFocusEvent>(BIND_EVENT_HANDLER(OnGetFoucus));
		dispather.Dispatch<WindowLostFocusEvent>(BIND_EVENT_HANDLER(OnLostFoucus));
		dispather.Dispatch<DragFileEvent>(BIND_EVENT_HANDLER(OnDragFile));
		dispather.Dispatch<WindowMinimizeEvent>(BIND_EVENT_HANDLER(OnWindowMinimize));
		dispather.Dispatch<WindowResizeEvent>(BIND_EVENT_HANDLER(OnWindowResize));
		dispather.Dispatch<WindowMovedEvent>(BIND_EVENT_HANDLER(OnWindowMove));
		//		dispather.Dispatch<MouseButtonPressedEvent>(BIND_EVENT_HANDLER(OnMouseBtnClicked));
				//static bool b_handle_input = true;
				//if (e.GetEventType() == EEventType::kWindowLostFocus) b_handle_input = false;
				//else if (e.GetEventType() == EEventType::kWindowFocus) b_handle_input = true;
				//if (!b_handle_input) return;

		for (auto it = _layer_stack->end(); it != _layer_stack->begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled()) break;
		}
	}
}
