#pragma once
#ifndef __EDITOR_LAYER_H__
#define __EDITOR_LAYER_H__
#include "Framework/Events/Layer.h"
#include "Render/PickPass.h"
#include "Dock/DockManager.h"

struct ImFont;
namespace Ailu
{
	namespace UI
	{
		class Widget;
	}
	namespace Editor
	{
		class RenderView;
        class CommonView;
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
		private:
			Render::PickFeature _pick;
			Ref<UI::Widget> _main_widget = nullptr;
		};
	}
}
#endif // !__EDITOR_LAYER_H__
