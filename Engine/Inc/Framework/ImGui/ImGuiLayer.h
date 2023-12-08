#pragma warning(push)
#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告
#pragma once
#ifndef __IMGUI_LAYER_H__
#define __IMGUI_LAYER_H__
#include "Framework/Events/Layer.h"

namespace Ailu
{
	class AILU_API TextureSelector
	{
	public:
		inline static String kNull = "null";
		void Open(const int& handle);
		void Show();
		const String& GetSelectedTexture(const int& handle) const;
		bool IsCaller(const int& handle) const { return _handle == handle; }
	private:
		int _handle = -1;
		bool _b_show = false;
		int _selected_img_index = -1;
		String _cur_selected_texture_path;
	};

	class MeshBrowser
	{
	public:
		inline static String kNull = "null";
		void Open(const int& handle);
		void Show();
		bool IsCaller(const int& handle) const { return _handle == handle; }
	private:
		int _handle = -1;
		bool _b_show = false;
		String _cur_selected_texture_path;
	};

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
		void ShowWorldOutline();
		void ShowObjectDetail();
	private:
		TextureSelector _texture_selector;
	};
}
#pragma warning(pop)
#endif // !IMGUI_LAYER_H__
