#pragma once
#ifndef __EDITOR_LAYER_H__
#define __EDITOR_LAYER_H__
#include "Framework/Events/Layer.h"
#include "Framework/Events/Event.h"
#include "Render/EditorDebugFeature.h"
#include "Render/PickPass.h"
#include "Scene/Entity.h"
#include "Dock/DockManager.h"

struct ImFont;
namespace Ailu
{
	namespace UI
	{
		class Widget;
        class Text;
	}
	namespace Editor
	{
		class RenderView;
        class CommonView;
        class EditorProgressPanel;
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
			Render::EditorDebugFeature _editor_debug;
            Vector4f _scene_vp_rect;

		private:
            void ProcessTransformGizmo();
            void BuildEditorChrome();
            void UpdateEditorChrome(f32 dt);
            void SaveAllAssets();
            void StartDeferredLightingCapture();
            // 抓取任意 render graph pass 的输入/输出。空白 pass 名回退到 DeferredLightingPass，
            // 保证原有调用点行为不变。
            void StartRenderGraphCapture(const String &pass_name, const String &resource_name,
                                         u32 access_index, bool capture_output, u32 camera_ordinal,
                                         const String &file_prefix, u32 frame_count);
            void UpdateDeferredLightingCaptureStatus();
            // Unattended screenshot: arms a capture at a fixed frame and closes the editor once
            // the readback is written.  Inactive unless AILU_AUTOCAP_FRAME is set, so normal
            // editing is unaffected.
            //   AILU_AUTOCAP_FRAME     tick index to capture at
            //   AILU_AUTOCAP_PASS      render graph pass name (default DeferredLightingPass)
            //   AILU_AUTOCAP_RESOURCE  expected resource name (empty accepts any)
            //   AILU_AUTOCAP_PREFIX    output file prefix (default: pass name)
            //   AILU_AUTOCAP_FORMAT    "png" (default) or "dds"
            void UpdateAutoCapture();
		private:
			Scope<EditorProgressPanel> _progress_panel;
            Ref<UI::Widget> _main_widget = nullptr;
            Ref<UI::Widget> _toolbar_widget = nullptr;
            Ref<UI::Widget> _status_bar_widget = nullptr;
            UI::Border *_toolbar_border = nullptr;
            UI::Border *_window_drag_area = nullptr;
            UI::Button *_maximize_button = nullptr;
            UI::Border *_status_bar_border = nullptr;
            UI::Text *_status_left_text = nullptr;
            UI::Text *_status_right_text = nullptr;
            String _editor_status_message = "Ready";
            i32 _deferred_lighting_capture_frame_count = 1;
            u64 _deferred_lighting_capture_session_id = 0u;
            String _deferred_lighting_capture_status = "Image Capture: Idle";
            i64 _auto_capture_frame = -1;
            i64 _auto_capture_tick = 0;
            bool _auto_capture_done = false;
			//transform gizmo
            i16 _transform_gizmo_type = -1;
            bool _is_transform_gizmo_snap = false;
            bool _is_transform_gizmo_world = true;
            bool _is_begin_gizmo_transform = false;
            bool _is_end_gizmo_transform = true;
            Vector<Transform> _old_trans;
            ECS::Entity _selected_entity = ECS::kInvalidEntity;
		};
	}
}
#endif // !__EDITOR_LAYER_H__
