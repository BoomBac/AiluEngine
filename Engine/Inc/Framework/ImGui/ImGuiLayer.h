#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告
#pragma warning(disable: 4275) //dll interface warning
#pragma once
#ifndef __IMGUI_LAYER_H__
#define __IMGUI_LAYER_H__
#include "Framework/Events/Layer.h"

namespace Ailu
{
	class AILU_API ImGUILayer : public Layer
	{
	public:
		ImGUILayer();
		ImGUILayer(const String& name);
		~ImGUILayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event& e) override;
		void OnImguiRender() override;

		void Begin();
		void End();
	private:
	};
}

#endif // !IMGUI_LAYER_H__
