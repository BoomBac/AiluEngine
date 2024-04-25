#pragma once
#ifndef __RENDER_VIEW_H__
#define __RENDER_VIEW_H__
#include "ImGuiWidget.h"
namespace Ailu
{
	class RenderView : public ImGuiWidget
	{
	public:
		RenderView();
		~RenderView();
		void Open(const i32& handle) final;
		void Close() final;
	private:
		void ShowImpl() final;
	};
}
#endif // !RENDER_VIEW_H__
