#pragma once
#ifndef __RENDER_VIEW_H__
#define __RENDER_VIEW_H__
#include "ImGuiWidget.h"
namespace Ailu
{
	namespace Editor
	{
		class RenderView : public ImGuiWidget
		{
		public:
			RenderView();
			~RenderView();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
			Vector2f GetViewportSize() const
			{
				return Vector2f(_pre_tick_width, _pre_tick_height);
			}
		private:
			void ShowImpl() final;
			f32 _pre_tick_width, _pre_tick_height;
			//当gameview尺寸改变时的window大小
			f32 _pre_tick_window_width, _pre_tick_window_height;
		};
	}
}
#endif // !RENDER_VIEW_H__
