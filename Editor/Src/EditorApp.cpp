#include "EditorApp.h"
#include "Render/Renderer.h"

using namespace Ailu;

namespace Editor
{
	//Renderer* g_pRenderer = new Renderer();
	int EditorApp::Initialize()
	{
		ApplicationDesc desc;
		desc._window_width = 1600;
		desc._window_height = 900;
		desc._gameview_width = 1600;
		desc._gameview_height = 900;

		auto ret = Application::Initialize();
		//_p_input_layer = new InputLayer();
		//PushLayer(_p_input_layer);
		_p_scene_layer = new SceneLayer();
		PushLayer(_p_scene_layer);
		//g_pRenderer->Initialize(desc._gameview_width, desc._gameview_height);

		return ret;
	}
	void EditorApp::Finalize()
	{
		Application::Finalize();
	}
	void EditorApp::Tick(const float& delta_time)
	{
		Application::Tick(delta_time);
	}
}
