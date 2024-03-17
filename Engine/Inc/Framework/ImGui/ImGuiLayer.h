#pragma warning(push)
#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告
#pragma once
#ifndef __IMGUI_LAYER_H__
#define __IMGUI_LAYER_H__
#include "Framework/Events/Layer.h"

namespace Ailu
{
	class ImguiWindow
	{
	public:
		inline static String kNull = "null";
		~ImguiWindow() = default;
		bool IsCaller(const int& handle) const { return _handle == handle; }
		virtual void Open(const int& handle)
		{
			_handle = handle;
			_b_show = true;
		};
		virtual void Close()
		{
			_b_show = false;
		};
		virtual void Show()
		{
			if (!_b_show)
			{
				_handle = -1;
				return;
			}
		};
	protected:
		int _handle = -1;
		bool _b_show = false;
	};
	class TextureSelector : public ImguiWindow
	{
	public:
		void Open(const int& handle) final;
		void Show() final;
		const String& GetSelectedTexture(const int& handle) const;
	private:
		int _selected_img_index = -1;
		String _cur_selected_texture_path = kNull;
	};

	class MeshBrowser : public ImguiWindow
	{
	public:
		void Open(const int& handle) final;
		void Show() final;
	};

	class AssetBrowser : public ImguiWindow
	{
	public:
		void Open(const int& handle) final;
		void Show() final;
	private:
		String _cur_dir;
	};

	class AssetTable : public ImguiWindow
	{
	public:
		void Open(const int& handle) final;
		void Show() final;
	};

	class RTDebugWindow : public ImguiWindow
	{
	public:
		void Open(const int& handle) final;
		void Show() final;
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
		MeshBrowser _mesh_browser;
		AssetBrowser _asset_browser;
		AssetTable _asset_table;
		RTDebugWindow _rt_view;
	};
}
#pragma warning(pop)
#endif // !IMGUI_LAYER_H__
