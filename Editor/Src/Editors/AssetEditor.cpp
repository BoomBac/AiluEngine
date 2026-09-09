#include "Editors/AssetEditor.h"

#include "Assets/Asset.h"
#include "Common/EditorPopup.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "UI/Container.h"
#include "UI/Menu.h"

namespace Ailu
{
    namespace Editor
    {
        AssetEditor::AssetEditor(const String &title, Vector2f size)
            : DockWindow(title, size), _editor_title(title)
        {
        }

        AssetEditor::~AssetEditor()
        {
            if (_asset_reload_listener_id != 0u)
                ResourceMgr::Get().RemoveAssetReloadedListener(_asset_reload_listener_id);
        }

        bool AssetEditor::Open(Asset *asset)
        {
            if (asset == nullptr)
                return false;
            if (_asset != nullptr && _asset != asset)
                Close();

            _asset = asset;
            if (!OnOpen())
            {
                _asset = nullptr;
                return false;
            }
            _asset_reload_listener_id = ResourceMgr::Get().AddAssetReloadedListener(
                [this](Asset *reloaded_asset) { HandleAssetReloaded(reloaded_asset); });
            RefreshTitle();
            RefreshEditor();
            return true;
        }

        void AssetEditor::Close()
        {
            if (_asset_reload_listener_id != 0u)
            {
                ResourceMgr::Get().RemoveAssetReloadedListener(_asset_reload_listener_id);
                _asset_reload_listener_id = 0u;
            }
            OnClose();
            _editor_import_setting.reset();
            _asset = nullptr;
            RefreshTitle();
        }

        void AssetEditor::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (_asset == nullptr)
                return;

            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL) ||
                              Input::IsKeyDown(EKey::kCONTROL);
            if (ctrl && Input::IsKeyDownAccurate(EKey::kS))
                Save();

            RefreshTitle();
            OnUpdate(dt);
        }

        void AssetEditor::AddAssetMenu(UI::HorizontalBox *toolbar)
        {
            if (toolbar == nullptr)
                return;

            auto *menu_button = toolbar->AddChild<UI::Button>("Asset");
            menu_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size({54.0f, 0.0f}).Margin({0.0f, 0.0f, 2.0f, 0.0f});
            menu_button->OnMouseClick() += [this, menu_button](UI::UIEvent &event)
            {
                Vector<UI::MenuEntry> file_entries;
                file_entries.push_back({"Save", [this]() { Save(); }});
                file_entries.push_back({"Discard Changes", [this]() { DiscardChanges(); }, true});
                file_entries.push_back({"Close", [this]() { RequestClose(); }});

                Vector<UI::MenuEntry> entries;
                entries.push_back({"File", {}, false, {}, true, false});
                entries.back()._children = std::move(file_entries);
                UI::Menu::ShowAt(menu_button, entries);
                event._is_handled = true;
            };
        }

        void AssetEditor::RequestClose()
        {
            if (IsDirty())
            {
                ShowClosePrompt();
                return;
            }

            Close();
            DockWindow::RequestClose();
        }

        bool AssetEditor::IsDirty() const
        {
            return _asset != nullptr && _asset->IsDirty();
        }

        bool AssetEditor::Save()
        {
            if (_asset == nullptr)
                return false;

            OnBeforeSave();
            if (_editor_import_setting != nullptr)
                ResourceMgr::Get().SetImportSetting(_asset->_asset_path, *_editor_import_setting);
            if (!ResourceMgr::Get().SaveAsset(_asset))
                return false;

            OnAssetSaved();
            RefreshTitle();
            RefreshEditor();
            return true;
        }

        bool AssetEditor::DiscardChanges()
        {
            if (_asset == nullptr)
                return false;
            if (!_asset->IsDirty())
                return true;

            if (!ResourceMgr::Get().ReloadAsset(_asset))
                return false;

            return true;
        }

        void AssetEditor::SetEditorImportSetting(const ImportSetting &setting)
        {
            _editor_import_setting = setting.Clone();
        }

        void AssetEditor::HandleAssetReloaded(Asset *asset)
        {
            if (asset == nullptr || asset != _asset)
                return;

            OnAssetReloaded();
            RefreshTitle();
            RefreshEditor();
        }

        void AssetEditor::RefreshTitle()
        {
            if (_asset == nullptr)
            {
                SetTitle(_editor_title);
                return;
            }

            String asset_name = _asset->Name();
            if (asset_name.empty())
                asset_name = ToChar(_asset->_asset_path);
            SetTitle(_editor_title + " - " + asset_name + (IsDirty() ? " *" : ""));
        }

        void AssetEditor::ShowClosePrompt()
        {
            const Vector2f kPopupSize = {300.0f, 120.0f};
            const auto &main_window = Application::Get().GetWindow();
            const Vector2f main_window_size = {static_cast<f32>(main_window.GetWidth()),
                                             static_cast<f32>(main_window.GetHeight())};
            const Vector2f popup_pos = (main_window_size - kPopupSize) * 0.5f;
            EditorPopup::ShowDialogAt(
                popup_pos, "AssetEditorClosePrompt", "Unsaved Changes", kPopupSize,
                [](UI::VerticalBox *content, UI::Text *)
                {
                    auto *message = content->AddChild<UI::Text>("Save changes before closing?");
                    message->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size({280.0f, 40.0f});
                },
                {
                    {"Save", [this]() -> std::optional<String>
                     {
                         if (!Save())
                             return String("Save failed");
                         Close();
                         DockWindow::RequestClose();
                         return std::nullopt;
                     }},
                    {"Discard", [this]() -> std::optional<String>
                     {
                         if (!DiscardChanges())
                             return String("Discard failed");
                         Close();
                         DockWindow::RequestClose();
                         return std::nullopt;
                     }, true},
                    {"Cancel", []() -> std::optional<String> { return std::nullopt; }}
                });
        }
    }// namespace Editor
}// namespace Ailu
