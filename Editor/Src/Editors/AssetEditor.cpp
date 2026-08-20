#include "Editors/AssetEditor.h"

#include "Assets/Asset.h"
#include "Common/EditorPopup.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"

namespace Ailu
{
    namespace Editor
    {
        AssetEditor::AssetEditor(const String &title, Vector2f size)
            : DockWindow(title, size), _editor_title(title)
        {
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
            RefreshTitle();
            RefreshEditor();
            return true;
        }

        void AssetEditor::Close()
        {
            OnClose();
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

            OnAssetReloaded();
            RefreshTitle();
            RefreshEditor();
            return true;
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
