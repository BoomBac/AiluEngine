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
		~ImGUILayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event& e) override;
		void OnImguiRender() override;

		void Begin();
		void End();

	private:
		void ShowWorldOutline();
		void ShowObjectDetail();
	};
}

#endif // !IMGUI_LAYER_H__
