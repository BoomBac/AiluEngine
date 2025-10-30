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
			Render::PickFeature _pick;
            Vector4f _scene_vp_rect;

        private:
            void ProcessTransformGizmo();
		private:
			Ref<UI::Widget> _main_widget = nullptr;
			//transform gizmo
            i16 _transform_gizmo_type = -1;
            bool _is_transform_gizmo_snap = false;
            bool _is_transform_gizmo_world = true;
            bool _is_begin_gizmo_transform = false;
            bool _is_end_gizmo_transform = true;
            Vector<Transform> _old_trans;
		};
	}
}
#endif // !__EDITOR_LAYER_H__
