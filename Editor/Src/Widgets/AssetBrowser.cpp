#include <functional>
#include "Ext/imgui/imgui.h"
#include "Engine/Inc/Framework/Common/Path.h"
#include "Engine/Inc/Render/Shader.h"
#include "Engine/Inc/Render/Material.h"
#include "Engine/Inc/Framework/Common/ResourceMgr.h"
#include "Engine/Inc/Framework/Common/FileManager.h"
#include "Engine/Inc/Render/Texture.h"
#include "Inc/Widgets/AssetBrowser.h"
#include "Inc/Widgets/CommonTextureWidget.h"

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
		return ImVec2(vmax.x - vmin.x, vmax.y - vmin.y);
	}

	static void DrawBorderFrame()
	{
		ImVec2 cur_img_pos = ImGui::GetCursorPos();
		ImVec2 imgMin = ImGui::GetItemRectMin();
		ImVec2 imgMax = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddRect(imgMin, imgMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 1.0f);
	};
	AssetBrowser::AssetBrowser(ImGuiWidget* asset_detail_widget) : ImGuiWidget("AssetBrowser")
	{
		_is_hide_common_widget_info = true;
		g_pResourceMgr->AddAssetChangedListener(std::bind(&AssetBrowser::OnAssetsChanged, this));
		_p_tex_detail_widget = dynamic_cast<TextureDetailView*>(asset_detail_widget);
	}
	AssetBrowser::~AssetBrowser()
	{
	}
	void AssetBrowser::Open(const i32& handle)
	{
		ImGuiWidget::Open(handle);
		_folder_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
		_file_icon =   g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"file.alasset");
		_mesh_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"3d.alasset");
		_shader_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"shader.alasset");
		_image_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"image.alasset");
		_scene_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"scene.alasset" );
		OnAssetsChanged();
	}
	void AssetBrowser::Close(i32 handle)
	{
		ImGuiWidget::Close(handle);
	}
	void AssetBrowser::OnAssetsChanged()
	{
		_cur_dir_assets.clear();
		SearchFilterByDirectory filter({ PathUtils::ExtractAssetPath(FileManager::GetCurDirStr())});
		_cur_dir_assets = std::move(g_pResourceMgr->GetAssets(filter));
		_selected_file_index = (u32)(-1);
		_selected_file_sys_path = FileManager::GetCurDirStr();
		_dirty = false;
	}
	void AssetBrowser::ShowImpl()
	{
		if (ImGui::Button("<-"))
		{
			FileManager::BackToParent();
			OnAssetsChanged();
		}
		const fs::path& cur_path = FileManager::GetCurPath();
		_window_size = GetCurImguiWindowSize();
		_asset_content_size = _window_size;
		_asset_content_size.x *= 0.9f;
		_asset_content_size.y *= 0.8f;
		_paddinged_preview_tex_size_padding = _preview_tex_size * 1.15f;
		ImGui::SliderFloat(" ", &_preview_tex_size, 16, 256, "%.0f");
		u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
		_icon_num_per_row = window_width / (u32)_paddinged_preview_tex_size_padding;
		_icon_num_per_row += _icon_num_per_row == 0 ? 1 : 0;
		
		u32 new_material_modal_window_handle = 0;
		fs::directory_iterator curdir_it{ cur_path };

		ImGui::Text("Current dir: %s,Selected id %d", ToChar(_selected_file_sys_path).data(),_selected_file_index);
		int file_index = 0;
		for (auto& dir_it : curdir_it)
		{
			bool is_dir = dir_it.is_directory();
			if (is_dir)
			{
				DrawFolder(dir_it.path(), file_index);
				if ((file_index + 1) % _icon_num_per_row != 0)
				{
					ImGui::SameLine();
				}
				++file_index;
			}
		}
		for (auto asset_it : _cur_dir_assets)
		{
			DrawAsset(asset_it, file_index);
			if ((file_index + 1) % _icon_num_per_row != 0)
			{
				ImGui::SameLine();
			}
			++file_index;
		}
		//右键空白处
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
					new_material_modal_window_handle = ImGuiWidget::GetGlobalWidgetHandle();
					MarkModalWindoShow(new_material_modal_window_handle);
				}
				if (ImGui::MenuItem("Shader"))
				{
					//ShaderLibrary::Add(_cur_dir + "test_new_shader.shader", "test_shader");
				}
				ImGui::EndMenu();
			}
			if(ImGui::MenuItem("ShowInExplorer"))
			{
#ifdef PLATFORM_WINDOWS
				auto file_dir = PathUtils::ToPlatformPath(FileManager::GetCurDirStr());
				ShellExecute(NULL, L"open", L"explorer", file_dir.c_str(), file_dir.c_str(), SW_SHOWNORMAL);
#endif // PLATFORM_WINDOWS


			}
			ImGui::EndPopup();
		}
		ShowModalDialog("New Material", std::bind(&AssetBrowser::ShowNewMaterialWidget, this), new_material_modal_window_handle);
		if (_dirty)
		{
			OnAssetsChanged();
		}
	}
	void AssetBrowser::ShowNewMaterialWidget()
	{
		AL_ASSERT(true);
		static char buf[256];
		ImGui::InputText("##MaterialName", buf, IM_ARRAYSIZE(buf));
		static Ref<Shader> selected_shader = nullptr;
		int count = 0;
		static int selected_index = -1;
		if (ImGui::BeginCombo("Select Shader: ", selected_shader ? selected_shader->Name().c_str() : "select shader"))
		{
			
			for (auto it = g_pResourceMgr->ResourceBegin<Shader>(); it != g_pResourceMgr->ResourceEnd<Shader>(); it++)
			{
				auto shader = ResourceMgr::IterToRefPtr<Shader>(it);
				if (ImGui::Selectable(shader->Name().c_str(), selected_index == count))
					selected_index = count;
				if (selected_index == count)
					selected_shader = shader;
				++count;
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("OK", ImVec2(120, 0)))
		{
			LOG_INFO("{},{}", String{ buf }, selected_shader ? selected_shader->Name() : "null shader");
			String name{ buf };

			if (!name.empty() && selected_shader)
			{
				String sys_path = ToChar(FileManager::GetCurDirStr());
				if (!sys_path.ends_with("\\") && !sys_path.ends_with("/"))
					sys_path.append("/");
				sys_path.append(name).append(".almat");
				auto asset_path = PathUtils::ExtractAssetPath(sys_path);
				auto new_mat = MakeRef<Material>(selected_shader.get(), name);
				new_mat->IsInternal(selected_shader->Name() == "shaders");
				g_pResourceMgr->CreateAsset(ToWChar(sys_path), new_mat);
			}
			ImGui::CloseCurrentPopup();
			OnAssetsChanged();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
	}
	
	void AssetBrowser::DrawFolder(fs::path dir_path, u32 cur_file_index)
	{
		auto file_name = dir_path.filename();
		auto file_name_str = file_name.string();
		ImGui::PushID(file_name_str.c_str());
		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, kColorBgTransparent);
		ImTextureID tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_folder_icon->GetNativeTextureHandle());
		if (ImGui::ImageButton(tex_id, ImVec2(_preview_tex_size, _preview_tex_size), _uv0, _uv1, 0, kColorBg,kColorTint))
		{
			_selected_file_index = cur_file_index;
			LOG_INFO("selected folder {}", file_name_str.c_str());
		}
		ImGui::PopStyleColor();
		std::string truncatedText = file_name_str.substr(0, static_cast<size_t>(_preview_tex_size / ImGui::CalcTextSize("A").x));
		ImGui::Text("%s", truncatedText.c_str());
		ImGui::EndGroup();
		//进入新的文件夹
		if (_selected_file_index == cur_file_index && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			FileManager::SetCurPath(dir_path);
			g_pLogMgr->LogFormat(L"Move dir to {}", dir_path.wstring());
			MarkDirty();
		}
		ImGui::PopID();
		if (cur_file_index == _selected_file_index)
		{
			DrawBorderFrame();
			_selected_file_sys_path = dir_path.wstring();
			PathUtils::FormatFilePathInPlace(_selected_file_sys_path);
		}
	}
	
	void AssetBrowser::DrawAsset(Asset* asset, u32 cur_file_index)
	{
		if (asset == nullptr)
			return;
		String file_name = ToChar(asset->_name);
		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, kColorBgTransparent);
		ImTextureID tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_file_icon->GetNativeTextureHandle());
		if (asset->_asset_type == EAssetType::kMesh)
		{
			tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_mesh_icon->GetNativeTextureHandle());
		}
		else if (asset->_asset_type == EAssetType::kShader)
		{
			tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_shader_icon->GetNativeTextureHandle());
		}
		else if (asset->_asset_type == EAssetType::kScene)
		{
			tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_scene_icon->GetNativeTextureHandle());
		}
		else if (asset->_asset_type == EAssetType::kTexture2D)
		{	
			tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(asset->As<Texture>()->GetNativeTextureHandle());
			//tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_image_icon->GetNativeTextureHandle());
		}
		else {};
		if (ImGui::ImageButton(tex_id, ImVec2(_preview_tex_size, _preview_tex_size), _uv0, _uv1, 0, kColorBg, kColorTint))
		{
			g_pLogMgr->LogFormat("selected file {}", file_name.c_str());
		}
		ImGui::PopStyleColor();
		std::string truncatedText = file_name.substr(0, static_cast<size_t>(_preview_tex_size / ImGui::CalcTextSize("A").x));
		ImGui::Text("%s", truncatedText.c_str());
		ImGui::EndGroup();
		//点击了文件
		if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1))
		{
			if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				_p_tex_detail_widget->Open(ImGuiWidget::GetGlobalWidgetHandle(), asset->As<Texture>());
			g_pLogMgr->LogFormat("selected asset {}", file_name.c_str());
			_selected_file_sys_path = asset->_asset_path;
			_selected_file_index = cur_file_index;
		}
		if (ImGui::BeginPopupContextItem(file_name.c_str())) // <-- use last item id as popup id
		{
			if (ImGui::MenuItem("Reimport"))
			{
				g_pLogMgr->LogWarning("Waiting for implement");
				//g_pResourceMgr->ImportResourceAsync(dir_path.wstring(), ImportSetting("", false));
			}
			if (ImGui::MenuItem("Rename"))
			{
				MarkModalWindoShow(_selected_file_index);
			}
			if (ImGui::MenuItem("Delete"))
			{
				g_pLogMgr->LogWarning("Delete asset");
				g_pResourceMgr->DeleteAsset(asset);
				MarkDirty();
			}
			ImGui::EndPopup();
		}
		if (cur_file_index == _selected_file_index)
		{
			DrawBorderFrame();
			auto new_name = ShowModalDialog("Rename asset", _selected_file_index);
			if (!new_name.empty())
			{
				g_pResourceMgr->RenameAsset(asset, ToWStr(new_name.c_str()));
				g_pLogMgr->LogWarningFormat("Rename to {}", new_name);
				MarkDirty();
			}
		}
	}
}
