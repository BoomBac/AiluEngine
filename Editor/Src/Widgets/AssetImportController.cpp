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

        AssetImportController::AssetImportController(EditorProgressService &service, ImportCallback import_resource)
            : _service(service), _import_resource(std::move(import_resource)) {}

        AssetImportController::~AssetImportController()
        {
            _lifetime.reset();
            if (_popup_group_id) UIManager::Get()->HidePopupGroup(_popup_group_id);
            if (_progress.Id()) { _progress.SetMessage("Import queue owner closed"); _progress.Cancel(); }
        }

        void AssetImportController::QueueFiles(const Vector<WString> &files, const fs::path &target_directory, Vector2f popup_pos)
        {
            if (files.empty()) return;
            if (_progress.IsCancellationRequested()) CancelQueued();
            if (_queue.empty())
            {
                _progress = _service.BeginTask("Asset import", 0, true);
                if (!_progress.Id()) return;
                _total = _succeeded = _skipped = _failed = 0;
                _last_error.clear();
                _progress.SetMessage("Queued files; cancel stops pending imports");
            }
            _popup_pos = popup_pos;
            for (const auto &file : files) _queue.push_back({file, target_directory});
            _total += files.size();
            _progress.SetTotal(_total);
        }

        void AssetImportController::Tick()
        {
            if (_queue.empty()) return;
            if (_progress.IsCancellationRequested()) { CancelQueued(); return; }
            if (!_waiting_for_settings) ProcessNext();
        }

        void AssetImportController::ProcessNext()
        {
            const PendingImport item = _queue.front();
            if (!_prepared)
            {
                _progress.SetMessage("Preparing: " + ToChar(fs::path(item._sys_path).filename().wstring()));
                _prepared = true;
                return; // Give UI a frame to show the filename before a synchronous call can stall it.
            }
            if (GetImportPopupType(item._sys_path) == EImportPopupType::kDirect)
                ImportCurrent(ImportSetting::Default());
            else
            {
                _waiting_for_settings = true;
                _progress.SetMessage("Waiting for settings: " + ToChar(fs::path(item._sys_path).filename().wstring()));
                try { ShowImportDialog(item); }
                catch (const std::exception &error)
                {
                    _last_error = error.what();
                    LOG_ERROR("Asset import dialog: {}", error.what());
                    const u64 popup_group_id = _popup_group_id;
                    _popup_group_id = 0;
                    FinishCurrent(false);
                    if (popup_group_id) UIManager::Get()->HidePopupGroup(popup_group_id);
                }
            }
        }

        void AssetImportController::ImportCurrent(const ImportSetting &setting)
        {
            if (_queue.empty()) return;
            if (_progress.IsCancellationRequested()) { CancelQueued(); return; }
            const PendingImport item = _queue.front();
            bool success = false;
            try
            {
                _progress.SetMessage("Importing: " + ToChar(fs::path(item._sys_path).filename().wstring()));
                success = _import_resource ? _import_resource(item._sys_path, item._target_directory.wstring(), setting) :
                    ResourceMgr::Get().ImportResource(item._sys_path, item._target_directory.wstring(), setting) != nullptr;
                if (!success) _last_error = "No resource produced: " + ToChar(fs::path(item._sys_path).filename().wstring());
            }
            catch (const std::exception &error) { _last_error = error.what(); LOG_ERROR("Asset import: {}", error.what()); }
            catch (...) { _last_error = "Unknown import exception"; LOG_ERROR("Asset import: unknown exception"); }
            FinishCurrent(success);
            if (success) NotifyImported();
        }

        void AssetImportController::ShowImportDialog(const PendingImport &item)
        {
            const String file_name = fs::path(item._sys_path).filename().string();
            const std::weak_ptr<int> lifetime = _lifetime;
            const auto on_closed = [this, lifetime]()
            {
                if (lifetime.expired()) return;
                _popup_group_id = 0;
                if (_waiting_for_settings) FinishCurrent(false, true);
            };
            const auto finish_popup = [this, lifetime](const ImportSetting *setting)
            {
                if (lifetime.expired() || !_waiting_for_settings) return;
                const u64 popup_group_id = _popup_group_id;
                if (setting) ImportCurrent(*setting);
                else FinishCurrent(false, true);
                UIManager::Get()->HidePopupGroup(popup_group_id);
            };
            _popup_group_id = UIManager::Get()->CreatePopupGroup();

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

                                              auto *srgb = EditorPopup::AddCheckBoxRow(content, "sRGB", setting->_is_srgb);
                                              srgb->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_srgb = checked;
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
                                                  {"Import", [setting, finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup(setting.get());
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup(nullptr);
                                                       return std::nullopt;
                                                   }, false, false}
                                          },
                                          {}, true, on_closed, _popup_group_id);
                return;
            }
            case EImportPopupType::kMesh:
            {
                auto setting = std::make_shared<MeshImportSetting>(MeshImportSetting::Default());
                setting->_import_flag |= MeshImportSetting::kImportFlagMesh;
                EditorPopup::ShowDialogAt(_popup_pos, "AssetBrowserMeshImportPrompt",
                                          std::format("Import Mesh: {}", file_name), {320.0f, 300.0f},
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

                                              auto *all_animation_stacks = EditorPopup::AddCheckBoxRow(
                                                  content, "Import All Animation Stacks", setting->_import_all_animation_stacks);
                                              all_animation_stacks->_on_click += [setting](bool checked)
                                              {
                                                  setting->_import_all_animation_stacks = checked;
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
                                                  {"Import", [setting, finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup(setting.get());
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup(nullptr);
                                                       return std::nullopt;
                                                   }, false, false}
                                          },
                                          {}, true, on_closed, _popup_group_id);
                return;
            }
            case EImportPopupType::kDirect:
            default:
                ImportCurrent(ImportSetting::Default());
                return;
            }
        }

        void AssetImportController::FinishCurrent(bool success, bool skipped)
        {
            if (_queue.empty()) return;
            if (skipped) ++_skipped;
            else if (success) ++_succeeded;
            else ++_failed;
            _progress.Advance();
            _queue.erase(_queue.begin());
            _prepared = _waiting_for_settings = false;
            if (_queue.empty()) FinishBatch();
        }

        String AssetImportController::Summary() const
        {
            return std::format("Success {} / Skipped {} / Failed {}", _succeeded, _skipped, _failed);
        }

        void AssetImportController::FinishBatch(bool cancelled)
        {
            const String summary = Summary();
            _progress.SetMessage(summary);
            if (cancelled) _progress.Cancel();
            else if (_failed) _progress.Fail(summary + "; " + _last_error);
            else _progress.Complete();
            LOG_INFO("Asset import batch: {}{}", summary, cancelled ? " (cancelled)" : "");
            _progress = {};
        }

        void AssetImportController::CancelQueued()
        {
            // Main-thread safe point: never interrupts an ImportResource call in progress.
            const u64 popup_group_id = _popup_group_id;
            _popup_group_id = 0;
            _waiting_for_settings = _prepared = false;
            if (popup_group_id) UIManager::Get()->HidePopupGroup(popup_group_id);
            _skipped += _queue.size();
            _progress.Advance(_queue.size());
            _queue.clear();
            FinishBatch(true);
        }

        void AssetImportController::NotifyImported()
        {
            if (_on_imported)
                _on_imported();
        }
    }// namespace Editor
}// namespace Ailu
