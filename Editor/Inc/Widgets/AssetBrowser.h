#pragma once
#ifndef __ASSET_BROWSER_H__
#define __ASSET_BROWSER_H__
#include "ImGuiWidget.h"
#include "Ext/imgui/imgui.h"


namespace Ailu
{
	class Texture;
	class Asset;
	namespace Editor
	{
		class TextureDetailView;
		class AssetBrowser : public ImGuiWidget
		{
		public:
			inline static Asset* s_draging_asset = nullptr;
			AssetBrowser(ImGuiWidget* asset_detail_widget);
			~AssetBrowser();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
		private:
			void OnUpdateAssetList(const ISearchFilter& filter);
			void ShowImpl() final;
			void ShowNewMaterialWidget();
			void DrawFolder(std::filesystem::path dir_path, u32 cur_file_index);
			void DrawAsset(Asset* asset, u32 cur_file_index);
			void MarkDirty() { _dirty = true; };
			void DrawTreePannelHelper(const WString& cur_path, String& indent);
			void DrawTreePannel();
			void DrawMainPannel();
			//两个格式化之后的系统路径
			bool OnFileDropDown(const WString& from, const WString& to);
		private:
			inline static ImVec4 kColorUnimported = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
			inline static ImVec4 kColorBg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
			inline static ImVec4 kColorTint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			inline static ImVec4 kColorBgTransparent = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
			ImVec2 _uv0{ 0,0 }, _uv1{ 1,1 };
			ImVec2 _window_size, _asset_content_size;
			f32 _preview_tex_size = 64, _paddinged_preview_tex_size_padding = _preview_tex_size * 1.15f;
			u32 _icon_num_per_row;
			f32 _left_pannel_width, _right_pannel_width;
			u32 _new_material_modal_window_handle = 0;
			WString _draging_file;
            Asset* _hovered_asset = nullptr;
			TextureDetailView* _p_tex_detail_widget;
			Map<WString, Vector<Asset*>> _dir_asset_map;
			//当前目录下选中的文件索引
			u32 _selected_file_index = 0;
			u32 _selected_file_rename_index = -1;
			Texture* _folder_icon;
			Texture* _file_icon;
			Texture* _mesh_icon;
			Texture* _shader_icon;
			Texture* _image_icon;
			Texture* _scene_icon;
			Texture* _material_icon;
            bool _is_search_active = false;
            char _search_buf[256];
            WString _search_str;
			Vector<Asset*> _cur_dir_assets;
			WString _selected_file_sys_path;
			bool _dirty;
			char _rename_buffer[256];
		};
	}
}

#endif // !ASSET_BROWSER_H__

