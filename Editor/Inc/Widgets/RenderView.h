#pragma once
#ifndef __RENDER_VIEW_H__
#define __RENDER_VIEW_H__
#include "ImGuiWidget.h"
#include "Render/Texture.h"
namespace Ailu
{
	namespace Editor
	{
		class RenderView : public ImGuiWidget
		{
		public:
			RenderView(const String& name = "RenderView");
			~RenderView();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
			void SetSource(RenderTexture* src) {_src = src;};
			Vector2f GetViewportSize() const
			{
				return Vector2f(_pre_tick_width, _pre_tick_height);
			}
            virtual void OnEvent(Event &e) override;
        protected:
			virtual void ShowImpl() override;
            Vector3f ScreenPosToWorldDir(Vector2f pos);
        protected:
			f32 _pre_tick_width, _pre_tick_height;
			//当gameview尺寸改变时的window大小
			f32 _pre_tick_window_width, _pre_tick_window_height;
			RenderTexture* _src = nullptr;
		};

		class SceneView : public RenderView
		{
		public:
			SceneView();
			~SceneView();
            void OnEvent(Event &e) final;
		private:
			void ShowImpl() final;
            void ProcessTransformGizmo();
            void OnActorPlaced(String obj_type,bool dropped = false);
        private:
            i16 _transform_gizmo_type = -1;
            bool _is_transform_gizmo_snap = false;
            bool _is_transform_gizmo_world = true;
            bool _is_begin_gizmo_transform = false;
            bool _is_end_gizmo_transform = true;
		};
	}
}
#endif // !RENDER_VIEW_H__
