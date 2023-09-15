#include "pch.h"
#include "Framework/Common/Application.h"
#include "Platform/WinWindow.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Framework/Events/InputLayer.h"
#include "Framework/Common/TimeMgr.h"
#include "CompanyEnv.h"

namespace Ailu
{
	TimeMgr* g_pTimeMgr = new TimeMgr();

#define BIND_EVENT_HANDLER(f) std::bind(&Application::f,this,std::placeholders::_1)
	int Application::Initialize()
	{
		AL_ASSERT(sp_instance,"Application already init!")
		sp_instance = this;
		_p_window = new Ailu::WinWindow(Ailu::WindowProps());
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		g_pTimeMgr->Initialize();
		_p_imgui_layer = new ImGUILayer();
		PushLayer(_p_imgui_layer);

		_p_renderer = new Renderer();
		_p_renderer->Initialize();

		PushLayer(new InputLayer());
		_b_running = true;
		SetThreadDescription(GetCurrentThread(),L"ALEngineMainThread");
		g_pTimeMgr->Mark();
		return 0;
	}
	void Application::Finalize()
	{
		DESTORY_PTR(_p_window)
		_p_renderer->Finalize();
		DESTORY_PTR(_p_renderer)
		g_pTimeMgr->Finalize();
		DESTORY_PTR(g_pTimeMgr)
	}
	double render_lag = 0.0;
	double update_lag = 0.0;
#ifdef __COMPANY_ENV_H__
	constexpr double kTargetFrameRate = 60;
#else
	constexpr double kTargetFrameRate = 170;
#endif // COMPLY


	constexpr double kMsPerUpdate = 16.66;
	constexpr double kMsPerRender = 1000.0 / kTargetFrameRate;
	void Application::Tick()
	{
		while (_b_running)
		{
			g_pTimeMgr->Tick();
			render_lag += g_pTimeMgr->GetElapsedSinceLastMark();
			update_lag += g_pTimeMgr->GetElapsedSinceLastMark();
			g_pTimeMgr->Mark();

			for (Layer* layer : _layer_stack)
				layer->OnUpdate(ModuleTimeStatics::RenderDeltatime);
			_p_window->OnUpdate();

			while (render_lag > kMsPerRender)
			{		
				_p_imgui_layer->Begin();
				for (Layer* layer : _layer_stack)
					layer->OnImguiRender();
				_p_imgui_layer->End();
				_p_renderer->Tick();
				render_lag -= kMsPerRender;
			}
		}
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
	void Application::OnEvent(Event& e)
	{
		EventDispather dispather(e);
		dispather.Dispatch<WindowCloseEvent>(BIND_EVENT_HANDLER(OnWindowClose));

		for (auto it = _layer_stack.end(); it != _layer_stack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled()) break;
		}
	}
}
