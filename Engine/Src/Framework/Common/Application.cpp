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

namespace Ailu
{
	TimeMgr* g_pTimeMgr = new TimeMgr();
	SceneMgr* g_pSceneMgr = new SceneMgr();
	ResourceMgr* g_pResourceMgr = new ResourceMgr();
	LogMgr* g_pLogMgr = new LogMgr();
	Scope<ThreadPool> g_thread_pool = MakeScope<ThreadPool>(8, "GlobalThreadPool");

#define BIND_EVENT_HANDLER(f) std::bind(&Application::f,this,std::placeholders::_1)
	int Application::Initialize()
	{
		AL_ASSERT(sp_instance,"Application already init!")
		sp_instance = this;
		g_pLogMgr->Initialize();
		g_pLogMgr->AddAppender(new FileAppender());
		_p_window = new Ailu::WinWindow(Ailu::WindowProps());
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		g_pTimeMgr->Initialize();
#ifdef DEAR_IMGUI
		_p_imgui_layer = new ImGUILayer();
		PushLayer(_p_imgui_layer);
#endif // DEAR_IMGUI
		GraphicsContext::InitGlobalContext();
		g_pResourceMgr->Initialize();
		g_pSceneMgr->Initialize();
		_p_renderer = new Renderer();
		_p_renderer->Initialize();
		_p_input_layer = new InputLayer();
		PushLayer(_p_input_layer);
		_b_running = true;
		SetThreadDescription(GetCurrentThread(),L"ALEngineMainThread");
		g_pTimeMgr->Mark();
		g_pLogMgr->Log("Init end");
		return 0;
	}
	void Application::Finalize()
	{
		DESTORY_PTR(_p_window);
		g_pResourceMgr->Finalize();
		g_pSceneMgr->Finalize();
		DESTORY_PTR(g_pSceneMgr);
		DESTORY_PTR(g_pResourceMgr);
		_p_renderer->Finalize();
		DESTORY_PTR(_p_renderer)
		g_pTimeMgr->Finalize();
		DESTORY_PTR(g_pTimeMgr);
		g_pLogMgr->Finalize();
		DESTORY_PTR(g_pLogMgr);
	}

	double render_lag = 0.0;
	double update_lag = 0.0;
#ifdef COMPANY_ENV
	constexpr double kTargetFrameRate = 60;
#else
	constexpr double kTargetFrameRate = 170;
#endif // COMPLY


	constexpr double kMsPerUpdate = 16.66;
	constexpr double kMsPerRender = 1000.0 / kTargetFrameRate;
	void Application::Tick(const float& delta_time)
	{
		while (_b_running)
		{
			g_pTimeMgr->Tick(0.0f);
			auto last_mark = g_pTimeMgr->GetElapsedSinceLastMark();
			render_lag += last_mark;
			update_lag += last_mark;
			g_pTimeMgr->Mark();
			for (Layer* layer : _layer_stack)
				layer->OnUpdate(ModuleTimeStatics::RenderDeltatime);
			_p_window->OnUpdate();
			g_pResourceMgr->Tick(delta_time);
			while (render_lag > kMsPerRender)
			{
				g_pSceneMgr->Tick(delta_time);
#ifdef DEAR_IMGUI
				_p_imgui_layer->Begin();
				for (Layer* layer : _layer_stack)
					layer->OnImguiRender();
				_p_imgui_layer->End();
#endif // DEAR_IMGUI
				_p_renderer->Tick(delta_time);
				render_lag -= kMsPerRender;
			}
		}
		g_pLogMgr->Log("Exit");
	}
	void Application::PushLayer(Layer* layer)
	{
		_layer_stack.PushLayer(layer);
		layer->OnAttach();
	}
	void Application::PushOverLayer(Layer* layer)
	{
		_layer_stack.PushOverLayer(layer);
		layer->OnAttach();
	}
	Application* Application::GetInstance()
	{
		return sp_instance;
	}
	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		_b_running = false;
		return _b_running;
	}
	bool Application::OnLostFoucus(WindowLostFocusEvent& e)
	{
		_layer_stack.PopLayer(_p_input_layer);
		return true;
	}
	bool Application::OnDragFile(DragFileEvent& e)
	{
		g_pResourceMgr->ImportAssetAsync(e.GetDragedFilePath());
		return false;
	}
	bool Application::OnGetFoucus(WindowFocusEvent& e)
	{
		_layer_stack.PushLayer(_p_input_layer);
		return true;
	}
	void Application::OnEvent(Event& e)
	{
		EventDispather dispather(e);
		dispather.Dispatch<WindowCloseEvent>(BIND_EVENT_HANDLER(OnWindowClose));
		dispather.Dispatch<WindowFocusEvent>(BIND_EVENT_HANDLER(OnGetFoucus));
		dispather.Dispatch<WindowLostFocusEvent>(BIND_EVENT_HANDLER(OnLostFoucus));
		dispather.Dispatch<DragFileEvent>(BIND_EVENT_HANDLER(OnDragFile));
		//static bool b_handle_input = true;
		//if (e.GetEventType() == EEventType::kWindowLostFocus) b_handle_input = false;
		//else if (e.GetEventType() == EEventType::kWindowFocus) b_handle_input = true;
		//if (!b_handle_input) return;

		for (auto it = _layer_stack.end(); it != _layer_stack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled()) break;
		}
	}
}
