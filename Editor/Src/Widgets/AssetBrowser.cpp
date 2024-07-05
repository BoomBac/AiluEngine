#include <functional>
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"
#include "Framework/Common/Path.h"
#include "Render/Shader.h"
#include "Render/Material.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/FileManager.h"
#include "Render/Texture.h"

#include "Widgets/AssetBrowser.h"
#include "Widgets/CommonTextureWidget.h"
#include "Common/Selection.h"

namespace Ailu
{
	namespace Editor
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
			ImVec2 ret;
			ret.x = vmax.x - vmin.x;
			ret.y = vmax.y - vmin.y;
			return ret;
		}

		static void DrawBorderFrame()
		{
			ImVec2 cur_img_pos = ImGui::GetCursorPos();
			ImVec2 imgMin = ImGui::GetItemRectMin();
			ImVec2 imgMax = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRect(imgMin, imgMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 1.0f);
		};

		void AssetBrowser::DrawTreePannelHelper(const WString& cur_path, String& indent)
		{
			fs::path p(cur_path);
			std::string file_name = p.filename().string();
			static ImVec2 icon_size(8, 8);
			if (fs::is_directory(p))
			{
				ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
				// 添加一个占位符，使其在布局中垂直方向居中对齐
				float lineHeight = ImGui::GetTextLineHeightWithSpacing();
				float iconPosY = (lineHeight - icon_size.y) * 0.5f;
				auto& pa = FileManager::GetCurSysPathStr();
				bool should_expand = pa.find(cur_path) != pa.npos;
				if (should_expand)
				{
					nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
				}
				bool opened = ImGui::TreeNodeEx(std::format("##{}", file_name).c_str(), nodeFlags);
				if (should_expand)
				{
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					ImVec2 treeNodeMin = ImGui::GetItemRectMin();
					ImVec2 treeNodeMax = ImGui::GetItemRectMax();
					ImVec4 bgColor = ImVec4(1.0f, 0.43f, 0.098f, 0.25f);
					drawList->AddRectFilled(treeNodeMin, treeNodeMax, ImGui::GetColorU32(bgColor));
				}
				if (ImGui::IsItemClicked())
				{
					FileManager::SetCurPath(cur_path);
					MarkDirty();
				}
				if (ImGui::BeginDragDropSource())
				{
					_draging_file = cur_path;
					PathUtils::FormatFilePathInPlace(_draging_file);
					ImGui::SetDragDropPayload(kDragFolderTreeType.c_str(), &_draging_file, _draging_file.size() * sizeof(wchar_t));
					ImGui::Text("Dragging %s", file_name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					//右侧面板拖动到树视图
					const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragFolderType.c_str());
					if (payload)
					{
						auto source_file_path = PathUtils::FormatFilePath(*(WString*)(payload->Data));
						WString target_dir = PathUtils::FormatFilePath(cur_path);
						target_dir.append(L"/");
						WString target_dir_path = PathUtils::FormatFilePath(target_dir.append(PathUtils::GetFileName(source_file_path, true)));
						OnFileDropDown(source_file_path, target_dir_path);
					}
					//树视图内拖动
					const ImGuiPayload* payload1 = ImGui::AcceptDragDropPayload(kDragFolderTreeType.c_str());
					if (payload1)
					{
						auto source_file_path = PathUtils::FormatFilePath(*(WString*)(payload1->Data));
						WString target_dir = PathUtils::FormatFilePath(cur_path);
						target_dir.append(L"/");
						WString target_dir_path = PathUtils::FormatFilePath(target_dir.append(PathUtils::GetFileName(source_file_path)));
						if (OnFileDropDown(source_file_path, target_dir_path))
						{
							if (source_file_path == FileManager::GetCurSysDirStr())
							{
								FileManager::SetCurPath(target_dir_path);
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(_folder_icon->GetNativeTextureHandle()), icon_size);
				ImGui::SameLine();
				ImGui::Text(file_name.c_str());

				if (opened)
				{
					indent.append("--");
					for (const auto& entry : fs::directory_iterator(p))
					{
						if (fs::is_directory(entry))
							DrawTreePannelHelper(PathUtils::FormatFilePath(entry.path().wstring()), indent);
					}
					ImGui::TreePop();
					indent = indent.substr(0, indent.size() - 2);
				}
			}
		}

		AssetBrowser::AssetBrowser(ImGuiWidget* asset_detail_widget) : ImGuiWidget("AssetBrowser")
		{
			_is_hide_common_widget_info = true;
			g_pResourceMgr->AddAssetChangedListener(std::bind(&AssetBrowser::OnUpdateAssetList, this));
			_p_tex_detail_widget = dynamic_cast<TextureDetailView*>(asset_detail_widget);
			_left_pannel_width = 200;
		}

		AssetBrowser::~AssetBrowser()
		{
		}

		void AssetBrowser::Open(const i32& handle)
		{
			ImGuiWidget::Open(handle);
			_folder_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
			_file_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"file.alasset");
			_mesh_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"3d.alasset");
			_shader_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"shader.alasset");
			_image_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"image.alasset");
			_scene_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"scene.alasset");
			_material_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"material.alasset");
			MarkDirty();
		}

		void AssetBrowser::Close(i32 handle)
		{
			ImGuiWidget::Close(handle);
		}

		void AssetBrowser::OnUpdateAssetList()
		{
			_cur_dir_assets.clear();
			SearchFilterByDirectory filter({ PathUtils::ExtractAssetPath(FileManager::GetCurSysDirStr()) });
			_cur_dir_assets = std::move(g_pResourceMgr->GetAssets(filter));
			_selected_file_index = (u32)(-1);
			_selected_file_sys_path = FileManager::GetCurSysDirStr();
			_dirty = false;
		}

		void AssetBrowser::ShowImpl()
		{
			if (_dirty)
				OnUpdateAssetList();
			_right_pannel_width = _size.x - _left_pannel_width - 10;
			// 定义一个矩形区域来表示分割线的位置
			ImVec2 pos = ImGui::GetCursorScreenPos();
			pos.x += _left_pannel_width + 2;
			ImVec2 size = ImVec2(2.0f, ImGui::GetContentRegionAvail().y);
			ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
			bool hovered, held;
			ImGui::BeginChild("LeftPane", ImVec2(_left_pannel_width, 0), true);
			// 左侧内容
			DrawTreePannel();
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::SplitterBehavior(bb, ImGui::GetID("Splitter"), ImGuiAxis_X, &_left_pannel_width, &_right_pannel_width, 50.0f, 100.0f, 4.0f, 0.0f, ImGui::GetColorU32(ImGuiCol_Separator));
			// 右侧内容
			ImGui::BeginChild("RightPane", ImVec2(_right_pannel_width, 0), true);
			DrawMainPannel();
			ImGui::EndChild();
			ShowModalDialog("New Material", std::bind(&AssetBrowser::ShowNewMaterialWidget, this), _new_material_modal_window_handle);
		}

		void AssetBrowser::ShowNewMaterialWidget()
		{
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
					String sys_path = ToChar(FileManager::GetCurSysDirStr());
					if (!sys_path.ends_with("\\") && !sys_path.ends_with("/"))
						sys_path.append("/");
					sys_path.append(name).append(".alasset");
					auto asset_path = PathUtils::ExtractAssetPath(sys_path);
					auto new_mat = MakeRef<Material>(selected_shader.get(), name);
					new_mat->IsInternal(selected_shader->Name() == "shaders");
					g_pResourceMgr->CreateAsset(ToWChar(asset_path), new_mat);
					g_pResourceMgr->SaveAllUnsavedAssets();
				}
				ImGui::CloseCurrentPopup();
				OnUpdateAssetList();
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
			if (ImGui::ImageButton(tex_id, ImVec2(_preview_tex_size, _preview_tex_size), _uv0, _uv1, 0, kColorBg, kColorTint))
			{
				_selected_file_index = cur_file_index;
				LOG_INFO("selected folder {}", file_name_str.c_str());
			}
			ImGui::PushID(cur_file_index);
			if (ImGui::BeginDragDropSource())
			{
				_draging_file = dir_path.wstring();
				PathUtils::FormatFilePathInPlace(_draging_file);
				ImGui::SetDragDropPayload(kDragFolderType.c_str(), &_draging_file, _draging_file.size() * sizeof(wchar_t));
				ImGui::Text("Dragging %s", file_name.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragFolderType.c_str());
				if (payload)
				{
					auto source_file_sys_path = *(WString*)(payload->Data);
					WString target_dir = PathUtils::FormatFilePath(dir_path.wstring());
					target_dir.append(L"/");
					WString target_dir_path = PathUtils::FormatFilePath(target_dir.append(PathUtils::GetFileName(source_file_sys_path, true)));
					OnFileDropDown(source_file_sys_path, target_dir_path);
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
			std::string truncatedText = file_name_str.substr(0, static_cast<size_t>(_preview_tex_size / ImGui::CalcTextSize("A").x));
			if (_selected_file_rename_index == cur_file_index)
			{
				memcpy(_rename_buffer, file_name_str.c_str(), file_name_str.size());
				ImGui::SetNextItemWidth(_preview_tex_size);
				ImGui::SetKeyboardFocusHere();
				if (ImGui::InputText("##rename_file", _rename_buffer, 256, ImGuiInputTextFlags_::ImGuiInputTextFlags_EnterReturnsTrue))
				{
					if (strcmp(file_name_str.c_str(), _rename_buffer) != 0)
					{
						WString path_str = dir_path.wstring();
						PathUtils::FormatFilePathInPlace(path_str);
						WString new_dir = path_str.substr(0, path_str.find_last_of(L"/") + 1) + ToWChar(_rename_buffer);
						new_dir.append(L"/");
						SearchFilterByDirectory filter({ PathUtils::ExtractAssetPath(path_str) });
						for (auto p : g_pResourceMgr->GetAssets(filter))
						{
							WString new_asset_path = PathUtils::ExtractAssetPath(new_dir + PathUtils::GetFileName(p->_asset_path, true));
							g_pResourceMgr->MoveAsset(p, new_asset_path);
						}
						FileManager::RenameDirectory(dir_path, fs::path(new_dir));
					}
					_selected_file_rename_index = -1;
				}
			}
			else
			{
				ImGui::Text("%s", truncatedText.c_str());
			}
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
			if (ImGui::BeginPopupContextItem(file_name_str.c_str())) // <-- use last item id as popup id
			{
				if (ImGui::MenuItem("Rename"))
				{
					_selected_file_rename_index = cur_file_index;
				}
				if (ImGui::MenuItem("Delete"))
				{
					WString path_str = dir_path.wstring();
					PathUtils::FormatFilePathInPlace(path_str);
					SearchFilterByDirectory filter({ PathUtils::ExtractAssetPath(path_str) });
					for (auto p : g_pResourceMgr->GetAssets(filter))
					{
						g_pResourceMgr->DeleteAsset(p);
					}
					FileManager::DeleteDirectory(dir_path);
					g_pLogMgr->LogWarningFormat(L"Delete directory {}", dir_path.wstring());
					MarkDirty();
				}
				ImGui::EndPopup();
			}
		}

		void AssetBrowser::DrawAsset(Asset* asset, u32 cur_file_index)
		{
			String file_name = ToChar(asset->_name);
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
			}
			else if (asset->_asset_type == EAssetType::kMaterial)
				tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_material_icon->GetNativeTextureHandle());
			else {};
			ImGui::PushStyleColor(ImGuiCol_Button, kColorBgTransparent);
			ImGui::BeginGroup();
			if (tex_id == 0)
			{
				tex_id = TEXTURE_HANDLE_TO_IMGUI_TEXID(_file_icon->GetNativeTextureHandle());
			}
			if (ImGui::ImageButton(tex_id, ImVec2(_preview_tex_size, _preview_tex_size), _uv0, _uv1, 0, kColorBg, kColorTint))
			{
				g_pLogMgr->LogFormat("selected file {}", file_name.c_str());
				Selection::AddAndRemovePreSelection(asset->_p_obj.get());
			}
			ImGui::PushID(asset->_name.c_str());
			if (ImGui::BeginDragDropSource())
			{
				if (cur_file_index == _selected_file_index)
				{
					_draging_file = PathUtils::GetResSysPath(asset->_asset_path);
					ImGui::SetDragDropPayload(kDragFolderType.c_str(), &_draging_file, _draging_file.size() * sizeof(wchar_t));
					ImGui::SetDragDropPayload(kDragAssetMesh.c_str(), &asset, sizeof(Asset*));
					s_draging_asset = asset;
					ImGui::Text("Dragging %s", file_name.c_str());
					//ImGui::Text("Dragging %p", &asset);
				}
				ImGui::EndDragDropSource();
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
			std::string truncatedText = file_name.substr(0, static_cast<size_t>(_preview_tex_size / ImGui::CalcTextSize("A").x));
			if (_selected_file_rename_index != cur_file_index)
			{
				ImGui::Text("%s", truncatedText.c_str());
			}
			else
			{
				memcpy(_rename_buffer, file_name.c_str(), file_name.size());
				ImGui::SetNextItemWidth(_preview_tex_size);
				ImGui::SetKeyboardFocusHere();
				if (ImGui::InputText("##rename_file", _rename_buffer, 256, ImGuiInputTextFlags_::ImGuiInputTextFlags_EnterReturnsTrue))
				{
					if (strcmp(file_name.c_str(), _rename_buffer) != 0)
					{
						WString new_name = ToWStr(_rename_buffer);
						WString old_path = PathUtils::GetResSysPath(asset->_asset_path);
						WString new_path = PathUtils::RenameFile(old_path, new_name);
						g_pResourceMgr->RenameAsset(asset, new_name);
						FileManager::RenameDirectory(old_path, new_path);
						g_pLogMgr->LogWarningFormat("Rename to {}", _rename_buffer);
						MarkDirty();
					}
					_selected_file_rename_index = -1;
				}
			}
			ImGui::EndGroup();
			//点击了文件
			if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1))
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (asset->_asset_type == EAssetType::kTexture2D)
					{
						_p_tex_detail_widget->Open(ImGuiWidget::GetGlobalWidgetHandle(), asset->As<Texture>());
					}
					else if (asset->_asset_type == EAssetType::kScene)
					{
						g_pSceneMgr->OpenScene(asset->_asset_path);
					}
				}
				Selection::AddAndRemovePreSelection(asset->_p_obj.get());
				g_pLogMgr->LogFormat("selected asset {}", file_name.c_str());
				_selected_file_sys_path = PathUtils::GetResSysPath(asset->_asset_path);
				_selected_file_index = cur_file_index;
			}
			if (ImGui::BeginPopupContextItem(file_name.c_str())) // <-- use last item id as popup id
			{
				if (ImGui::MenuItem("Reimport"))
				{
					g_pLogMgr->LogWarning("Waiting for implement");
				}
				if (ImGui::MenuItem("Rename"))
				{
					_selected_file_rename_index = cur_file_index;
				}
				if (ImGui::MenuItem("Delete"))
				{
					g_pLogMgr->LogWarningFormat(L"Delete asset: {}", asset->_asset_path);
					g_pResourceMgr->DeleteAsset(asset);
					FileManager::DeleteDirectory(PathUtils::GetResSysPath(asset->_asset_path));
					MarkDirty();
				}
				ImGui::EndPopup();
			}
			if (cur_file_index == _selected_file_index)
			{
				DrawBorderFrame();
			}
		}

		void AssetBrowser::DrawTreePannel()
		{
			String indent = "";
			DrawTreePannelHelper(g_pResourceMgr->GetProjectRootSysPath(), indent);
		}

		void AssetBrowser::DrawMainPannel()
		{
			if (ImGui::Button("<-"))
			{
				FileManager::BackToParent();
				OnUpdateAssetList();
			}
			ImGui::SameLine();
			const fs::path& cur_path = FileManager::GetCurSysPath();
			_window_size = GetCurImguiWindowSize();
			_asset_content_size = _window_size;
			_asset_content_size.x *= 0.9f;
			_asset_content_size.y *= 0.8f;
			_paddinged_preview_tex_size_padding = _preview_tex_size * 1.15f;
			ImGui::SliderFloat("IconSize", &_preview_tex_size, 16, 256, "%.0f");
            auto wmax = ImGui::GetWindowContentRegionMax();
            auto wmin = ImGui::GetWindowContentRegionMin();
			u32 window_width = (u32)(wmax.x - wmin.y);
			_icon_num_per_row = window_width / (u32)_paddinged_preview_tex_size_padding;
			_icon_num_per_row += _icon_num_per_row == 0 ? 1 : 0;

			fs::directory_iterator curdir_it{ cur_path };
			ImGui::Text("Current dir: %s,Selected id %d", ToChar(_selected_file_sys_path).data(), _selected_file_index);
			int file_index = 0;
			//绘制文件夹图标
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
			//绘制资产
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
						FileManager::CreateDirectory(FileManager::GetCurSysDirStr() + L"/NewFolder");
					}
					if (ImGui::MenuItem("Material"))
					{
						_new_material_modal_window_handle = ImGuiWidget::GetGlobalWidgetHandle();
						MarkModalWindoShow(_new_material_modal_window_handle);
					}
					if (ImGui::MenuItem("Shader"))
					{
						//ShaderLibrary::Add(_cur_dir + "test_new_shader.shader", "test_shader");
					}
					if (ImGui::MenuItem("Scene"))
					{
						auto scene = SceneMgr::Create(L"new_scene");
						g_pResourceMgr->CreateAsset(FileManager::GetCurAssetDirStr() + L"/new_scene.almap", scene);
						g_pResourceMgr->SaveAllUnsavedAssets();
						MarkDirty();
					}
					ImGui::EndMenu();
				}

				if (ImGui::MenuItem("ShowInExplorer"))
				{
#ifdef PLATFORM_WINDOWS
					auto file_dir = PathUtils::ToPlatformPath(FileManager::GetCurSysDirStr());
					ShellExecute(NULL, L"open", L"explorer", file_dir.c_str(), file_dir.c_str(), SW_SHOWNORMAL);
#endif // PLATFORM_WINDOWS
				}
				ImGui::EndPopup();
			}
		}

		bool AssetBrowser::OnFileDropDown(const WString& from, const WString& to)
		{
			fs::path src_p(from);
			WString source_file_asset_path = PathUtils::ExtractAssetPath(from);
			if (fs::is_directory(src_p))
			{
				if (FileManager::CopyDirectory(from, to))
				{
					WString to_dir = to;
					to_dir.append(L"/");//新资产的父目录为新创建的目录
					for (auto asset : g_pResourceMgr->GetAssets(SearchFilterByDirectory{ {source_file_asset_path} }))
					{
						WString new_asset_path = to_dir.append(PathUtils::GetFileName(asset->_asset_path, true));
						g_pResourceMgr->MoveAsset(asset, PathUtils::ExtractAssetPath(new_asset_path));
					}
					FileManager::DeleteDirectory(from);
					g_pLogMgr->LogWarningFormat(L"Cut folder: from {} to {}", from, to);
					MarkDirty();
					return true;
				}
			}
			else
			{
				if (FileManager::CopyFile(from, to))
				{
					for (auto asset : g_pResourceMgr->GetAssets(SearchFilterByPath{ {source_file_asset_path} }))
					{
						g_pResourceMgr->MoveAsset(asset, PathUtils::ExtractAssetPath(to));
					}
					FileManager::DeleteDirectory(from);
					g_pLogMgr->LogWarningFormat(L"Cut file: from {} to {}", from, to);
					MarkDirty();
					return true;
				}
			}
			return false;
		}
	}
}
