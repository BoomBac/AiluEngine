#pragma once
#ifndef __ASSET_BROWSER_H__
#define __ASSET_BROWSER_H__
#include "ImGuiWidget.h"
#include "Ext/imgui/imgui.h"

namespace Ailu
{
	class Texture;
	class Asset;
    class AssetBrowser : public ImGuiWidget
    {
	public:
		AssetBrowser();
		~AssetBrowser();
		void Open(const i32& handle) final;
		void Close(i32 handle) final;
	private:
		void OnUpdateAssetList();
		void ShowImpl() final;
		void ShowNewMaterialWidget();
		void DrawFolder(fs::path dir_path,u32 cur_file_index);
		void DrawFile(fs::path dir_path,u32 cur_file_index);
		void DrawAsset(Asset* asset);
	private:
		inline static ImVec4 kColorUnimported = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
		inline static ImVec4 kColorBg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		inline static ImVec4 kColorTint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		inline static ImVec4 kColorBgTransparent = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
		ImVec2 _uv0{ 0,0 }, _uv1{ 1,1 };
		ImVec2 _window_size,_asset_content_size;
		f32 _preview_tex_size = 64, _paddinged_preview_tex_size_padding = _preview_tex_size * 1.15f;
		u32 _icon_num_per_row;
		//当前目录下选中的文件索引
		u32 _selected_file_index = 0;
		Texture* _folder_icon;
		Texture* _file_icon;
		Texture* _mesh_icon;
		Texture* _shader_icon;
		Texture* _image_icon;
		Texture* _scene_icon;
		Vector<Asset*> _cur_dir_assets;
    };
}

#endif // !ASSET_BROWSER_H__

