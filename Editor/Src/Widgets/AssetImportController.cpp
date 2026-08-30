#include "Widgets/AssetImportController.h"

#include "Common/EditorPopup.h"
#include "Animation/SkeletonAsset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Framework/Interface/IParser.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/ObjectAssetDropdown.h"
#include "UI/UIFramework.h"

#include <memory>

namespace Ailu
{
    namespace Editor
    {
        using namespace UI;
        namespace
        {
            enum class EImportPopupType : u8
            {
                kDirect,
                kTexture,
                kMesh
            };

            EImportPopupType GetImportPopupType(const WString &sys_path)
            {
                const String ext = fs::path(sys_path).extension().string();
                if (ext == ".fbx" || ext == ".FBX")
                    return EImportPopupType::kMesh;
                if (ResourceMgr::kHDRImageExt.contains(ext) || ResourceMgr::kLDRImageExt.contains(ext))
                    return EImportPopupType::kTexture;
                return EImportPopupType::kDirect;
            }
        }// namespace

        void AssetImportController::QueueFiles(const Vector<WString> &files, const fs::path &target_directory, Vector2f popup_pos)
        {
            if (files.empty())
                return;

            const bool was_empty = _queue.empty();
            _popup_pos = popup_pos;
            for (const auto &file: files)
            {
                LOG_INFO(L"AssetBrowser: drop file {}", file);
                _queue.push_back({file, target_directory});
            }
            if (was_empty)
                ProcessNext();
        }

        void AssetImportController::ProcessNext()
        {
            while (!_queue.empty())
            {
                const PendingImport &item = _queue.front();
                const EImportPopupType type = GetImportPopupType(item._sys_path);
                if (type == EImportPopupType::kDirect)
                {
                    ResourceMgr::Get().ImportResource(item._sys_path, item._target_directory.wstring());
                    NotifyImported();
                    FinishCurrent();
                    continue;
                }
                ShowImportDialog(item);
                return;
            }
        }

        void AssetImportController::ShowImportDialog(const PendingImport &item)
        {
            const String file_name = fs::path(item._sys_path).filename().string();
            const WString sys_path = item._sys_path;
            const fs::path target_directory = item._target_directory;
            const auto finish_popup = [this]()
            {
                UIManager::Get()->HidePopup();
                FinishCurrent();
            };

            switch (GetImportPopupType(item._sys_path))
            {
            case EImportPopupType::kTexture:
            {
                auto setting = std::make_shared<TextureImportSetting>(TextureImportSetting::Default());
                EditorPopup::ShowDialogAt(_popup_pos, "AssetBrowserTextureImportPrompt",
                                          std::format("Import Texture: {}", file_name), {320.0f, 180.0f},
                                          [setting, file_name](UI::VerticalBox *content, UI::Text *)
                                          {
                                              auto *file_text = content->AddChild<Text>(std::format("File: {}", file_name));
                                              file_text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              file_text->_horizontal_align = EAlignment::kLeft;

                                              auto *srgb = EditorPopup::AddCheckBoxRow(content, "sRGB", setting->_is_sRGB);
                                              srgb->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_sRGB = checked;
                                              };

                                              auto *mipmap = EditorPopup::AddCheckBoxRow(content, "Generate Mipmap", setting->_generate_mipmap);
                                              mipmap->_on_click += [setting](bool checked)
                                              {
                                                  setting->_generate_mipmap = checked;
                                              };

                                              auto *readable = EditorPopup::AddCheckBoxRow(content, "Readable", setting->_is_readable);
                                              readable->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_readable = checked;
                                              };
                                          },
                                          {
                                                  {"Import", [this, sys_path, target_directory, setting, finish_popup]() -> std::optional<String>
                                                   {
                                                       ResourceMgr::Get().ImportResource(sys_path, target_directory.wstring(), *setting);
                                                       NotifyImported();
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false}
                                          },
                                          {}, true);
                return;
            }
            case EImportPopupType::kMesh:
            {
                auto setting = std::make_shared<MeshImportSetting>(MeshImportSetting::Default());
                setting->_import_flag |= MeshImportSetting::kImportFlagMesh;
                EditorPopup::ShowDialogAt(_popup_pos, "AssetBrowserMeshImportPrompt",
                                          std::format("Import Mesh: {}", file_name), {320.0f, 270.0f},
                                          [setting, file_name](UI::VerticalBox *content, UI::Text *)
                                          {
                                              auto *file_text = content->AddChild<Text>(std::format("File: {}", file_name));
                                              file_text->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              file_text->_horizontal_align = EAlignment::kLeft;

                                              const bool import_mesh = (setting->_import_flag & MeshImportSetting::kImportFlagMesh) != 0;
                                              auto *mesh = EditorPopup::AddCheckBoxRow(content, "Import Mesh", import_mesh);
                                              mesh->_on_click += [setting](bool checked)
                                              {
                                                  if (checked)
                                                      setting->_import_flag |= MeshImportSetting::kImportFlagMesh;
                                                  else
                                                      setting->_import_flag &= ~MeshImportSetting::kImportFlagMesh;
                                              };

                                              const bool import_skeleton =
                                                  (setting->_import_flag & MeshImportSetting::kImportFlagSkeleton) != 0;
                                              auto *skeleton = EditorPopup::AddCheckBoxRow(content, "Import Skeleton",
                                                                                           import_skeleton);
                                              skeleton->_on_click += [setting](bool checked)
                                              {
                                                  if (checked)
                                                      setting->_import_flag |= MeshImportSetting::kImportFlagSkeleton;
                                                  else
                                                      setting->_import_flag &= ~MeshImportSetting::kImportFlagSkeleton;
                                              };

                                              const bool import_material = (setting->_import_flag & MeshImportSetting::kImportFlagMaterial) != 0;
                                              auto *materials = EditorPopup::AddCheckBoxRow(content, "Import Materials", import_material);
                                              materials->_on_click += [setting](bool checked)
                                              {
                                                  if (checked)
                                                      setting->_import_flag |= MeshImportSetting::kImportFlagMaterial;
                                                  else
                                                      setting->_import_flag &= ~MeshImportSetting::kImportFlagMaterial;
                                              };

                                              auto *combine_mesh = EditorPopup::AddCheckBoxRow(content, "Combine Meshes", setting->_is_combine_mesh);
                                              combine_mesh->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_combine_mesh = checked;
                                              };

                                              const bool import_animation = (setting->_import_flag & MeshImportSetting::kImportFlagAnimation) != 0;
                                              auto *animation = EditorPopup::AddCheckBoxRow(content, "Import Animation", import_animation);
                                              animation->_on_click += [setting](bool checked)
                                              {
                                                  if (checked)
                                                      setting->_import_flag |= MeshImportSetting::kImportFlagAnimation;
                                                  else
                                                      setting->_import_flag &= ~MeshImportSetting::kImportFlagAnimation;
                                              };

                                              auto *skeleton_row = content->AddChild<HorizontalBox>();
                                              skeleton_row->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              skeleton_row->AddChild<Text>("Skeleton")
                                                  ->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto)
                                                  .FillRate(1.0f);
                                              auto *skeleton_dropdown = skeleton_row->AddChild<ObjectAssetDropdown>(SkeletonAsset::StaticType());
                                              skeleton_dropdown->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto)
                                                  .FillRate(1.0f);
                                              skeleton_dropdown->SetAllowNone(true);
                                              skeleton_dropdown->SetSelectedGuid(setting->_skeleton);
                                              skeleton_dropdown->_on_object_asset_selected += [setting](Asset *, Object *, const Guid &guid)
                                              {
                                                  setting->_skeleton = guid;
                                              };
                                          },
                                          {
                                                  {"Import", [this, sys_path, target_directory, setting, finish_popup]() -> std::optional<String>
                                                   {
                                                       ResourceMgr::Get().ImportResource(sys_path, target_directory.wstring(), *setting);
                                                       NotifyImported();
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false}
                                          },
                                          {}, true);
                return;
            }
            case EImportPopupType::kDirect:
            default:
                ResourceMgr::Get().ImportResource(sys_path, target_directory.wstring());
                NotifyImported();
                finish_popup();
                return;
            }
        }

        void AssetImportController::FinishCurrent()
        {
            if (!_queue.empty())
                _queue.erase(_queue.begin());
            ProcessNext();
        }

        void AssetImportController::NotifyImported()
        {
            if (_on_imported)
                _on_imported();
        }
    }// namespace Editor
}// namespace Ailu
