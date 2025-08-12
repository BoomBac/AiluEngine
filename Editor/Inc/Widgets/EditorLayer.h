#pragma once
#ifndef __EDITOR_LAYER_H__
#define __EDITOR_LAYER_H__
#include "Framework/Events/Layer.h"
#include "ImGuiWidget.h"
#include "Render/PickPass.h"

struct ImFont;
namespace Ailu
{
    namespace UI
    {
        class Canvas;
    }
	namespace Editor
	{
		class SceneView;
		class EditorLayer : public Layer
		{
		public:
			EditorLayer();
			EditorLayer(const String& name);
			~EditorLayer();


			void OnAttach() override;
			void OnDetach() override;
			void OnEvent(Event& e) override;
            void OnUpdate(f32 dt) override;
			void OnImguiRender() override;

			void Begin();
			void End();
			Vector2f     _viewport_size;
			ImGuiWidget* _p_scene_view;
			ImGuiWidget* _p_preview_cam_view;
			ImGuiWidget* _p_profiler_window;
            ImGuiWidget *_p_rdg_debugger;
		private:
			ImFont* _font = nullptr;
			Vector<Scope<ImGuiWidget>> _widgets;
			ImGuiWidget* _p_env_setting;
            Render::PickFeature _pick;
            UI::Canvas *_canvas = nullptr;
		};
	}
}
#endif // !__EDITOR_LAYER_H__
