#include "pch.h"
#include "CompanyEnv.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Events/InputLayer.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"

#include "Framework/Common/Input.h"
#include "Framework/Common/ThreadPool.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"

#include "Framework/ImGui/Widgets/OutputLog.h"

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
		return path;
#else
		AL_ASSERT(PLATFORM_WINDOWS != 1)
#endif // WINDOWS
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
		AL_ASSERT_MSG(sp_instance, "Application already init!");
		sp_instance = this;
		g_pLogMgr->Initialize();
		g_pLogMgr->AddAppender(new FileAppender());
		g_pLogMgr->AddAppender(new ImGuiLogAppender());
		_p_window = new Ailu::WinWindow(Ailu::WindowProps());
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		g_pTimeMgr->Initialize();
		_layer_stack = new LayerStack();
#ifdef DEAR_IMGUI
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
		g_pRenderer->Initialize(desc._gameview_width,desc._gameview_height);
		g_pLogMgr->LogFormat("Renderer init finish after {}ms", g_pTimeMgr->GetElapsedSinceLastMark());
		_p_input_layer = new InputLayer();
		PushLayer(_p_input_layer);
		_p_scene_layer = new SceneLayer();
		PushLayer(_p_scene_layer);
		SetThreadDescription(GetCurrentThread(), L"ALEngineMainThread");
		g_pLogMgr->Log("Application Init end");
		_state = EApplicationState::EApplicationState_Running;
		return 0;
	}

	void Application::Finalize()
	{
		DESTORY_PTR(_layer_stack);
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
		while (_state != EApplicationState::EApplicationState_Exit)
		{
			if (_state == EApplicationState::EApplicationState_Pause)
			{
				_p_window->OnUpdate();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			else
			{
				g_pTimeMgr->Tick(0.0f);
				auto last_mark = g_pTimeMgr->GetElapsedSinceLastMark();
				_render_lag += last_mark;
				_update_lag += last_mark;
				g_pTimeMgr->Mark();
				for (Layer* layer : *_layer_stack)
					layer->OnUpdate(ModuleTimeStatics::RenderDeltatime);
				_p_window->OnUpdate();
				g_pResourceMgr->Tick(delta_time);
				while (_render_lag > kMsPerRender)
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
					_render_lag -= kMsPerRender;
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
		return false;
	}
	bool Application::OnLostFoucus(WindowLostFocusEvent& e)
	{
		_p_input_layer->HandleInput(false);
		return true;
	}
	bool Application::OnGetFoucus(WindowFocusEvent& e)
	{
		_state = EApplicationState::EApplicationState_Running;
		_p_input_layer->HandleInput(true);
		return true;
	}
	bool Application::OnWindowMinimize(WindowMinimizeEvent& e)
	{
		_state = EApplicationState::EApplicationState_Pause;
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
	bool Application::OnDragFile(DragFileEvent& e)
	{
		g_pResourceMgr->ImportResourceAsync(e.GetDragedFilePath());
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
