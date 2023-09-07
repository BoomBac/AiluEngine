#include "pch.h"
#include "Framework/Common/Application.h"
#include "Platform/WinWindow.h"
#include "Framework/Common/Log.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Framework/Common/KeyCode.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/InputLayer.h"

namespace Ailu
{

#define BIND_EVENT_HANDLER(f) std::bind(&Application::f,this,std::placeholders::_1)
	int Application::Initialize()
	{
		AL_ASSERT(sp_instance,"Application already init!")
		sp_instance = this;
		_p_window = new Ailu::WinWindow(Ailu::WindowProps());
		_p_window->SetEventHandler(BIND_EVENT_HANDLER(OnEvent));
		_p_imgui_layer = new ImGUILayer();
		PushLayer(_p_imgui_layer);

		_p_renderer = new Renderer();
		_p_renderer->Initialize();

		PushLayer(new InputLayer());
		_b_running = true;
		SetThreadDescription(GetCurrentThread(),L"ALEngineMainThread");
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
				layer->OnUpdate(16.66);
			_p_imgui_layer->Begin();
			for (Layer* layer : _layer_stack)
				layer->OnImguiRender();
			_p_imgui_layer->End();

			_p_window->OnUpdate();
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
		//auto pos = Input::GetMousePos();
		//LOG_INFO("pos: {},{}",pos.x,pos.y)
		if(Input::IsKeyPressed(AL_KEY_A))
			LOG_INFO(e.ToString())
		//LOG_INFO(e.ToString());
		for (auto it = _layer_stack.end(); it != _layer_stack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled()) break;
		}
	}
}
