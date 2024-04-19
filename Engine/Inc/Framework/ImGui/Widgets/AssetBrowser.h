#pragma once
#ifndef __ASSET_BROWSER_H__
#define __ASSET_BROWSER_H__
#include "ImGuiWidget.h"


namespace Ailu
{
	class Texture;
    class AssetBrowser : public ImGuiWidget
    {
	public:
		AssetBrowser();
		~AssetBrowser();
		void Open(const i32& handle) final;
		void Close() final;
	private:
		void ShowImpl() final;
		void ShowNewMaterialWidget();
	private:
		Ref<Texture> _folder_icon;
		Ref<Texture> _file_icon;
		//Ref<Texture> _material_icon;
		Ref<Texture> _mesh_icon;
		Ref<Texture> _shader_icon;
		Ref<Texture> _image_icon;
		Ref<Texture> _scene_icon;
    };
}

#endif // !ASSET_BROWSER_H__

