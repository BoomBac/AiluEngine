#pragma warning(push)
#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告
#pragma once
#ifndef __IMGUI_LAYER_H__
#define __IMGUI_LAYER_H__
#include "Framework/Events/Layer.h"
#include "Widgets/TextureSelector.h"
#include "Widgets/RenderView.h"

struct ImFont;

namespace Ailu
{
	class AssetBrowser;
	class AssetTable;
	class ObjectDetail;
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

	class MeshBrowser : public ImguiWindow
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
		ImFont* _font = nullptr;
		TextureSelector _texture_selector;
		MeshBrowser _mesh_browser;
		RenderView* _render_view;
		AssetBrowser* _asset_browser;
		AssetTable* _asset_table;
		ObjectDetail* _object_detail;
		RTDebugWindow _rt_view;
		ImGuiWidget* _p_outputlog;
	};
}
#pragma warning(pop)
#endif // !IMGUI_LAYER_H__
