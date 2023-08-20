#include "pch.h"
#include "Framework/Common/Application.h"
#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"

#include "Framework/ImGui/ImGuiLayer.h"

#define AL_ASSERT(x,msg) if(x) throw(std::runtime_error(msg));

namespace Ailu
{
#define BIND_EVENT_HANDLER(f) std::bind(&Application::f,this,std::placeholders::_1)
	int Application::Initialize()
	{
		AL_ASSERT(_sp_instance,"Application already init!")
		_sp_instance = this;
		_p_window = new Ailu::WinWindow(Ailu::WindowProps());
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		PushLayer(new ImGUILayer());
		_p_renderer = new Renderer();
		_p_renderer->Initialize();
		_b_running = true;
		return 0;
	}
	void Application::Finalize()
	{
		DESTORY_PTR(_p_window)
		_p_renderer->Finalize();
		DESTORY_PTR(_p_renderer)
	}
	void Application::Tick()
	{
		while (_b_running)
		{
			for (Layer* layer : _layer_stack)
				layer->OnUpdate();
			_p_window->OnUpdate();
			std::cout << "hello" << std::endl;
			_p_renderer->Tick();
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
		return _sp_instance;
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
		LOG_INFO(e.ToString());
		for (auto it = _layer_stack.end(); it != _layer_stack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled()) break;
		}
	}
}
