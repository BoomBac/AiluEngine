#include "pch.h"
#include "Framework/Common/Application.h"
#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
#define BIND_EVENT_HANDLER(f) std::bind(&Application::f,this,std::placeholders::_1)
	int Application::Initialize()
	{
		_p_window = new Ailu::WinWindow(Ailu::WindowProps());
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		_p_renderer = new Renderer();
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
			//_p_renderer->Tick();
		}
	}
	void Application::PushLayer(Layer* layer)
	{
		_layer_stack.PushLayer(layer);
	}
	void Application::PushOverLayer(Layer* layer)
	{
		_layer_stack.PushOverLayer(layer);
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
