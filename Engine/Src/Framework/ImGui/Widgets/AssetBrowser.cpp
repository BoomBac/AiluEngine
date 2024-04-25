#include "pch.h"
#include "Framework/ImGui/Widgets/AssetBrowser.h"
#include "Framework/Common/Path.h"
#include "Ext/imgui/imgui.h"
#include "Render/Shader.h"
#include "Render/Material.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/FileManager.h"
#include "Render/Texture.h"

namespace Ailu
{
	static std::tuple<ImVec2, ImVec2> GetCurImguiWindowRect()
	{
		ImVec2 vMin = ImGui::GetWindowContentRegionMin();
		ImVec2 vMax = ImGui::GetWindowContentRegionMax();
		vMin.x += ImGui::GetWindowPos().x;
		vMin.y += ImGui::GetWindowPos().y;
		vMax.x += ImGui::GetWindowPos().x;
		vMax.y += ImGui::GetWindowPos().y;
		return std::make_tuple(vMin, vMax);
	}

	static ImVec2 GetCurImguiWindowSize()
	{
		auto [vmin, vmax] = GetCurImguiWindowRect();
		return vmax - vmin;
	}

	AssetBrowser::AssetBrowser() : ImGuiWidget("AssetBrowser")
	{

	}
	AssetBrowser::~AssetBrowser()
	{
	}
	void AssetBrowser::Open(const i32& handle)
	{
		ImGuiWidget::Open(handle);
		_folder_icon = g_pTexturePool->Get(L"Editor/folder");
		_file_icon =   g_pTexturePool->Get(L"Editor/file_us");
		_mesh_icon =   g_pTexturePool->Get(L"Editor/3d");
		_shader_icon = g_pTexturePool->Get(L"Editor/shader");
		_image_icon =  g_pTexturePool->Get(L"Editor/image");
		_scene_icon =  g_pTexturePool->Get(L"Editor/scene");
	}
	void AssetBrowser::Close()
	{
	}
	void AssetBrowser::ShowImpl()
	{
		if (ImGui::Button("<-"))
		{
			FileManager::BackToParent();
		}
		auto window_size = GetCurImguiWindowSize();
		const fs::path& cur_path = FileManager::GetCurPath();
		auto asset_content_size = window_size;
		asset_content_size.x *= 0.9f;
		asset_content_size.y *= 0.8f;
		static float preview_tex_size = 64;
		float paddinged_preview_tex_size_padding = preview_tex_size * 1.15f;
		ImGui::SliderFloat(" ", &preview_tex_size, 16, 256, "%.0f");
		u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
		u32 icon_num_per_row = window_width / (u32)paddinged_preview_tex_size_padding;
		icon_num_per_row += icon_num_per_row == 0 ? 1 : 0;
		static ImVec2 uv0{ 0,0 }, uv1{ 1,1 };
		static fs::directory_entry s_cur_entry(cur_path);
		fs::directory_iterator curdir_it{ cur_path };
		static int cur_selected_file_index = -1;
		ImGui::Text("Current dir: %s", ToChar(FileManager::GetCurDirStr()).data());
		int file_count = 0;
		for (auto& dir_it : curdir_it)
		{
			bool is_dir = dir_it.is_directory();
			auto file_name = dir_it.path().filename();
			auto file_name_str = file_name.string();
			auto asset = g_pResourceMgr->GetAsset(PathUtils::ExtractAssetPath(PathUtils::FormatFilePath(dir_it.path().wstring())));
			bool is_cur_asset_imported = asset && asset->_p_inst_asset;
			is_cur_asset_imported &= !is_dir;
			ImGui::BeginGroup();
			//ImGuiContext* context = ImGui::GetCurrentContext();
			//auto drawList = context->CurrentWindow->DrawList;
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
			ImTextureID tex_id = is_dir ? TEXTURE_HANDLE_TO_IMGUI_TEXID(_folder_icon->GetNativeTextureHandle()) : TEXTURE_HANDLE_TO_IMGUI_TEXID(_file_icon->GetNativeTextureHandle());
			static ImVec4 color_unimported = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
			static ImVec4 color_bg = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
			static ImVec4 color_tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			String ext_str = file_name.extension().string();
			if (ext_str == ".fbx" || ext_str.c_str() == ".FBX")
			{
				tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_mesh_icon->GetNativeTextureHandle());
			}
			else if (ext_str == ".hlsl")
			{
				tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_shader_icon->GetNativeTextureHandle());
			}
			else if (ResourceMgr::kLDRImageExt.contains(ext_str) || ResourceMgr::kHDRImageExt.contains(ext_str))
			{
				tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_image_icon->GetNativeTextureHandle());
			}
			else if (ext_str == ".almap")
			{
				tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_scene_icon->GetNativeTextureHandle());
			}
			if (ImGui::ImageButton(tex_id, ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0, color_bg, is_dir || is_cur_asset_imported? 
				color_tint : color_unimported))
			{
				cur_selected_file_index = file_count;
				LOG_INFO("selected file {}", file_name_str.c_str());
			}
			ImGui::PopStyleColor();
			std::string truncatedText = file_name_str.substr(0, static_cast<size_t>(preview_tex_size / ImGui::CalcTextSize("A").x));
			ImGui::Text("%s", truncatedText.c_str());
			ImGui::EndGroup();
			//点击了文件
			if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1))
			{
				s_cur_entry = dir_it;
				cur_selected_file_index = file_count;
			}
			//进入新的文件夹
			if (cur_selected_file_index == file_count && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && is_dir)
			{
				FileManager::SetCurPath(dir_it.path());
			}
			if ((file_count + 1) % icon_num_per_row != 0)
			{
				ImGui::SameLine();
			}
			if (ImGui::BeginPopupContextItem(file_name_str.c_str())) // <-- use last item id as popup id
			{
				if (!is_cur_asset_imported)
				{
					if (ImGui::MenuItem("Import"))
					{
						g_pResourceMgr->ImportResourceAsync(s_cur_entry.path().wstring(),ImportSetting("",false));
					}
				}
				if (ImGui::MenuItem("Rename"))
				{
				}
				if (ImGui::MenuItem("Delete"))
				{
					FileManager::DeleteDirectory(s_cur_entry.path().wstring());
				}
				ImGui::EndPopup();
			}
			++file_count;
		}
		if (ImGui::BeginPopupContextWindow("AssetBrowserContext", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup))
		{
			if (ImGui::BeginMenu("New"))
			{
				if (ImGui::MenuItem("Folder"))
				{
					FileManager::CreateDirectory(FileManager::GetCurDirStr() + L"/NewFolder");
				}
				if (ImGui::MenuItem("Material"))
				{
					MarkModalWindoShow(49);
				}
				if (ImGui::MenuItem("Shader"))
				{
					//ShaderLibrary::Add(_cur_dir + "test_new_shader.shader", "test_shader");
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
		ShowModalDialog("New Material", std::bind(&AssetBrowser::ShowNewMaterialWidget, this), 49);
	}
	void AssetBrowser::ShowNewMaterialWidget()
	{
		//static char buf[256];
		//ImGui::InputText("##MaterialName", buf, IM_ARRAYSIZE(buf));
		//static Ref<Shader> selected_shader = nullptr;
		//int count = 0;
		//static int selected_index = -1;
		//if (ImGui::BeginCombo("Select Shader: ", selected_shader ? selected_shader->Name().c_str() : "select shader"))
		//{
		//	for (auto it = ShaderLibrary::Begin(); it != ShaderLibrary::End(); it++)
		//	{
		//		if (ImGui::Selectable((*it)->Name().c_str(), selected_index == count))
		//			selected_index = count;
		//		if (selected_index == count)
		//			selected_shader = *it;
		//		++count;
		//	}
		//	ImGui::EndCombo();
		//}
		//if (ImGui::Button("OK", ImVec2(120, 0)))
		//{
		//	LOG_INFO("{},{}", String{ buf }, selected_shader ? selected_shader->Name() : "null shader");
		//	String name{ buf };

		//	if (!name.empty() && selected_shader)
		//	{
		//		String sys_path = _cur_dir;
		//		if (!sys_path.ends_with("\\") && !sys_path.ends_with("/"))
		//			sys_path.append("/");
		//		sys_path.append(name).append(".almat");
		//		auto asset_path = PathUtils::ExtractAssetPath(sys_path);
		//		auto new_mat = MaterialLibrary::CreateMaterial(selected_shader, name, asset_path);
		//		new_mat->IsInternal(selected_shader->Name() == "shaders");
		//		g_pResourceMgr->SaveAsset(sys_path, new_mat.get());
		//	}
		//	ImGui::CloseCurrentPopup();
		//}
		//ImGui::SameLine();
		//if (ImGui::Button("Cancel", ImVec2(120, 0)))
		//{
		//	ImGui::CloseCurrentPopup();
		//}
	}
}
