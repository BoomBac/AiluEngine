#include "Editors/SpriteAssetEditor.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "UI/DragDrop.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Log.h"
#include "Common/Undo.h"
#include "Common/EditorPopup.h"
#include "Dock/DockManager.h"
#include "Render/2D/Sprite.h"
#include "Render/2D/SpriteAtlas.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <queue>

using namespace Ailu::UI;

namespace Ailu
{
    using Render::Sprite;
    namespace Editor
    {
        namespace
        {
            constexpr f32 kToolbarHeight     = 28.0f;
            constexpr f32 kStatusBarHeight   = 22.0f;
            constexpr f32 kLeftPanelWidth    = 240.0f;
            constexpr f32 kRightPanelWidth   = 340.0f;
            constexpr f32 kInputHeight       = 22.0f;
            constexpr f32 kTexPreviewSize    = 64.0f;
            constexpr f32 kSpriteZoomStep   = 1.1f;
            constexpr f32 kChessTileSize     = 16.0f;

            const Color kColorUvRect    = Color(0.2f, 0.8f, 1.0f, 1.0f);
            const Color kColorPivot     = Color(0.2f, 1.0f, 0.2f, 1.0f);
            const Color kColorBorder    = Color(1.0f, 0.6f, 0.0f, 1.0f);
            const Color kColorOverlay   = Color(0.0f, 0.0f, 0.0f, 0.5f);

            String GuidToString(const Guid& g) { return g.ToString().substr(0, 8) + "..."; }
            String FormatResolution(u16 w, u16 h) { return std::format("{} x {}", w, h); }
            String FormatFloat(f32 v, i32 decimals = 3) { return std::format("{:.{}f}", v, decimals); }

            struct TextureChoice
            {
                Guid _guid;
                String _name;
                String _path;
            };
        }

        // =====================================================================
        // SpriteAssetEditCommand
        // =====================================================================
        class SpriteAssetEditCommand : public AssetEditCommand
        {
            DECLARE_COMMAND(SpriteAssetEdit)
        public:
            SpriteAssetEditCommand(SpriteAssetEditor* editor,
                                   const SpriteAssetEditData& old_data,
                                   const SpriteAssetEditData& new_data)
                : AssetEditCommand(editor != nullptr ? editor->GetAsset() : nullptr),
                  _editor(editor), _old_data(old_data), _new_data(new_data) {}

        protected:
            bool ApplyEdit(bool) override
            {
                if (_editor == nullptr)
                    return false;
                _editor->ApplyEditData(_new_data);
                return true;
            }

            bool UndoEdit() override
            {
                if (_editor == nullptr)
                    return false;
                _editor->ApplyEditData(_old_data);
                return true;
            }

        private:
            SpriteAssetEditor* _editor;
            SpriteAssetEditData _old_data;
            SpriteAssetEditData _new_data;
        };

        // =====================================================================
        // Constructor
        // =====================================================================
        SpriteAssetEditor::SpriteAssetEditor()
            : AssetEditor("Sprite Editor", Vector2f(1000.0f, 650.0f))
        {
            // Set initial position below the editor toolbar so the title bar is visible and draggable
            SetPosition(Vector2f(120.0f, 60.0f));

            auto* root_vb = _content_root->AddChild<UI::VerticalBox>();
            root_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto* toolbar = root_vb->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kToolbarHeight));
            BuildToolbar(toolbar);

            auto* main_area = root_vb->AddChild<UI::SplitView>();
            main_area->_is_horizontal = true;
            main_area->SetRatio(kLeftPanelWidth / 1000.0f);
            main_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto* left_border = main_area->AddChild<UI::Border>();
            left_border->_bg_color = Color(0.16f, 0.17f, 0.19f, 1.0f);
            auto* left_vb = left_border->AddChild<UI::VerticalBox>();
            left_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            BuildLeftPanel(left_vb);

            auto* right_split = main_area->AddChild<UI::SplitView>();
            right_split->_is_horizontal = true;
            right_split->SetRatio((1000.0f - kLeftPanelWidth - kRightPanelWidth) / (1000.0f - kLeftPanelWidth));

            auto* center_border = right_split->AddChild<UI::Border>();
            center_border->_bg_color = Color(0.12f, 0.13f, 0.14f, 1.0f);
            BuildCenterPanel(center_border);

            auto* right_border = right_split->AddChild<UI::Border>();
            right_border->_bg_color = Color(0.16f, 0.17f, 0.19f, 1.0f);
            auto* right_scroll = right_border->AddChild<UI::ScrollView>();
            right_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto* right_vb = right_scroll->AddChild<UI::VerticalBox>();
            right_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildRightPanel(right_vb);

            auto* status_bar = root_vb->AddChild<UI::HorizontalBox>();
            status_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kStatusBarHeight));
            BuildStatusBar(status_bar);
        }

        SpriteAssetEditor::~SpriteAssetEditor() = default;

        void SpriteAssetEditor::OnBeforeSave()
        {
            if (GetAsset() == nullptr || !HasDraftChanges())
                return;
            if (!GetAsset()->IsDirty())
                GetAsset()->MarkModified();

            if (_sprite_atlas != nullptr)
            {
                StoreSelectedSprite();
                WriteAtlasToAssets();
                return;
            }

            ValidateEditingData();
            WriteToAsset();
            if (_selected_sprite_index >= 0 && _selected_sprite_index < static_cast<i32>(_sprite_items.size()))
                _sprite_asset->Name(_sprite_items[_selected_sprite_index]._name);
        }

        void SpriteAssetEditor::OnAssetSaved()
        {
            if (_sprite_atlas != nullptr)
            {
                _original_atlas_sprites = _sprite_atlas->Sprites();
                _original_atlas_sprite_guids.clear();
                for (const auto &sprite : _original_atlas_sprites)
                    _original_atlas_sprite_guids.push_back(ResourceMgr::Get().GetAssetGuid(sprite.get()));
                _original_sprite_count = static_cast<u32>(_sprite_items.size());
                for (auto &item : _sprite_items)
                {
                    item._original = item._editing;
                    item._original_name = item._name;
                }
            }
            else if (_sprite_asset != nullptr)
            {
                ReadFromAsset();
                _original = _editing;
                StoreSelectedSprite();
                _original_sprite_count = static_cast<u32>(_sprite_items.size());
            }
            RefreshAllUI();
        }

        void SpriteAssetEditor::OnAssetReloaded()
        {
            _sprite_asset = GetAssetObject<Render::Sprite>();
            _sprite_atlas = GetAssetObject<Render::SpriteAtlas>();
            if (_sprite_asset != nullptr)
                OnOpen();
            else if (_sprite_atlas != nullptr)
                OnOpen();
        }

        // =====================================================================
        // Open / Close
        // =====================================================================
        bool SpriteAssetEditor::OnOpen()
        {
            Sprite *asset = GetAssetObject<Sprite>();
            Render::SpriteAtlas *atlas = GetAssetObject<Render::SpriteAtlas>();
            if (asset == nullptr && atlas == nullptr)
                return false;
            if (asset == nullptr)
            {
                asset = nullptr;
                _sprite_atlas = atlas;
            }
            if (atlas != nullptr)
            {
                _sprite_atlas = atlas;
                _sprite_asset = nullptr;
                _original = SpriteAssetEditData();
                _editing = SpriteAssetEditData();
                _texture = nullptr;
                _supports_multiple_sprites = true;
                _sprite_items.clear();
                _selected_sprite_indices.clear();
                _original_atlas_sprites = atlas->Sprites();
                _original_atlas_sprite_guids.clear();
                _selected_sprite_index = -1;
                u32 sprite_index = 0u;
                for (const auto &sprite_ref : atlas->Sprites())
                {
                    Sprite *sprite = sprite_ref.get();
                    if (!sprite)
                    {
                        ++sprite_index;
                        continue;
                    }
                    _sprite_asset = sprite;
                    ReadFromAsset();
                    _original = _editing;
                    const String display_name = sprite->Name().empty()
                        ? MakeAtlasSpriteName(sprite_index) : sprite->Name();
                    _sprite_items.push_back(SpriteAssetEditItem{
                        ResourceMgr::Get().GetAssetGuid(sprite), display_name, display_name, sprite, _original, _editing});
                    _original_atlas_sprite_guids.push_back(ResourceMgr::Get().GetAssetGuid(sprite));
                    ++sprite_index;
                }
                _selected_sprite_index = _sprite_items.empty() ? -1 : 0;
                if (_selected_sprite_index >= 0)
                    _selected_sprite_indices = {_selected_sprite_index};
                LoadSelectedSprite();
                _original_sprite_count = static_cast<u32>(_sprite_items.size());
                OnTextureChanged();
                StoreSelectedSprite();
                SetTitle("Sprite Atlas Editor - " + atlas->Name());
                RefreshAllUI();
                return true;
            }

            _sprite_asset = asset;
            _sprite_atlas = nullptr;
            _supports_multiple_sprites = false;
            _sprite_items.clear();
            _selected_sprite_indices.clear();
            _selected_sprite_index = -1;
            ReadFromAsset();
            _original = _editing;
            _sprite_items.push_back(SpriteAssetEditItem{
                ResourceMgr::Get().GetAssetGuid(asset), asset->Name(), asset->Name(), asset, _original, _editing});
            _selected_sprite_index = 0;
            _selected_sprite_indices = {0};
            _original_sprite_count = static_cast<u32>(_sprite_items.size());
            OnTextureChanged();
            SetTitle("Sprite Editor - " + asset->Name());
            RefreshAllUI();
            return true;
        }

        void SpriteAssetEditor::OnClose()
        {
            if (_sprite_atlas && IsDirty())
                Revert();
            _sprite_asset = nullptr;
            _sprite_atlas = nullptr;
            _texture = nullptr;
            _sprite_items.clear();
            _original_atlas_sprites.clear();
            _original_atlas_sprite_guids.clear();
            _sprite_list_items.clear();
            _selected_sprite_indices.clear();
            _selected_sprite_index = -1;
            _original_sprite_count = 0;
        }

        void SpriteAssetEditor::ApplyEditData(const SpriteAssetEditData& data)
        {
            const bool texture_changed = _editing._texture != data._texture;
            _editing = data;
            if (texture_changed)
                OnTextureChanged();
            else
                ValidateEditingData();
            CommitLiveEdit(false);
            StoreSelectedSprite();
            RefreshAllUI();
            RefreshPreview();
        }

        // =====================================================================
        // Update
        // =====================================================================
        void SpriteAssetEditor::Update(f32 dt)
        {
            AssetEditor::Update(dt);
            if (!_sprite_asset && !_sprite_atlas) return;

            if (GetAsset() != nullptr && !GetAsset()->IsDirty() && HasDraftChanges())
                GetAsset()->MarkModified();

            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);

            if (ctrl && Input::IsKeyDownAccurate(EKey::kZ)) { if (g_pCommandMgr) g_pCommandMgr->Undo(); }
            if (ctrl && Input::IsKeyDownAccurate(EKey::kY)) { if (g_pCommandMgr) g_pCommandMgr->Redo(); }
            if (Input::IsKeyDownAccurate(EKey::kF)) FitTexture();
            if (Input::IsKeyDownAccurate(EKey::k1)) { if (_preview) _preview->ResetZoom(); RefreshPreview(); }
            if (Input::IsKeyDownAccurate(EKey::kG)) {
                _show_grid = !_show_grid;
                if (_chk_grid) _chk_grid->SetChecked(_show_grid);
                if (_preview) _preview->SetShowPixelGrid(_show_grid);
                RefreshPreview();
            }
            if (Input::IsKeyDownAccurate(EKey::kP)) { _show_pivot = !_show_pivot; if (_chk_pivot) _chk_pivot->SetChecked(_show_pivot); RefreshPreview(); }
            if (Input::IsKeyDownAccurate(EKey::kB)) { _show_border = !_show_border; if (_chk_border) _chk_border->SetChecked(_show_border); RefreshPreview(); }
            if (Input::IsKeyDownAccurate(EKey::kESCAPE))
            {
                if (_drag_mode != SpriteEditorDragMode::kNone)
                {
                    if (_drag_mode != SpriteEditorDragMode::kPanView)
                        _editing = _drag_start_data;
                    _drag_mode = SpriteEditorDragMode::kNone;
                    RefreshAllUI();
                    RefreshPreview();
                }
            }

            bool dirty = IsDirty();
            if (_btn_apply) _btn_apply->SetInteractiveEnabled(dirty);
            if (_btn_revert) _btn_revert->SetInteractiveEnabled(dirty);
            if (_txt_zoom_label)
                _txt_zoom_label->SetText(std::format("{}%", _preview ?
                    (i32)std::round(_preview->GetZoom() * 100.0f) : 100));
            RefreshStatusBar();
        }

        void SpriteAssetEditor::FitTexture()
        {
            if (_texture && _preview)
            {
                auto ps = _preview->GetContentRect();
                f32 tw = (f32)_texture->Width(), th = (f32)_texture->Height();
                if (tw > 0.0f && th > 0.0f)
                {
                    f32 zw = ps.z / tw, zh = ps.w / th;
                    _preview->SetZoom(std::min(zw, zh));
                    _preview->SetPan(Vector2f::kZero);
                    RefreshPreview();
                }
            }
        }

        // =====================================================================
        // Core Operations
        // =====================================================================
        void SpriteAssetEditor::ReadFromAsset()
        {
            if (!_sprite_asset) return;
            _editing._texture = _sprite_asset->_texture.GetGuid();
            if (_editing._texture.IsEmpty() && _sprite_asset->_texture)
            {
                auto* linked = ResourceMgr::Get().GetLinkedAsset(_sprite_asset->_texture.get());
                if (linked)
                    _editing._texture = linked->GetGuid();
            }
            _editing._uv_rect = _sprite_asset->_uv_rect;
            _editing._pivot   = _sprite_asset->_pivot;
            _editing._size    = _sprite_asset->_size;
            _editing._border  = _sprite_asset->_border;
        }

        void SpriteAssetEditor::WriteToAsset()
        {
            if (!_sprite_asset) return;
            // Convert Guid back to runtime Ref<Texture2D>
            Ref<Texture2D> texture;
            if (!_editing._texture.IsEmpty())
                texture = ResourceMgr::Get().GetRef<Texture2D>(_editing._texture);
            _sprite_asset->_texture.Set(_editing._texture, std::move(texture));
            _sprite_asset->_uv_rect = _editing._uv_rect;
            _sprite_asset->_pivot   = _editing._pivot;
            _sprite_asset->_size    = _editing._size;
            _sprite_asset->_border  = _editing._border;
        }

        void SpriteAssetEditor::CommitLiveEdit(bool mark_dirty)
        {
            if (_supports_multiple_sprites || _sprite_asset == nullptr)
                return;

            ValidateEditingData();
            if (mark_dirty && _editing != _original && GetAsset() != nullptr && g_pCommandMgr)
            {
                const SpriteAssetEditData old_data = _original;
                const SpriteAssetEditData new_data = _editing;
                g_pCommandMgr->ExecuteCommand(std::make_unique<SpriteAssetEditCommand>(this, old_data, new_data));
                return;
            }
            WriteToAsset();
            if (_selected_sprite_index >= 0 && _selected_sprite_index < static_cast<i32>(_sprite_items.size()))
                _sprite_asset->Name(_sprite_items[_selected_sprite_index]._name);
            _original = _editing;
            StoreSelectedSprite();
            if (mark_dirty && GetAsset() != nullptr && !GetAsset()->IsDirty())
                GetAsset()->MarkModified();
        }

        void SpriteAssetEditor::Apply()
        {
            if ((!_sprite_asset && !_sprite_atlas) || !IsDirty())
                return;

            if (_sprite_atlas)
            {
                StoreSelectedSprite();
                WriteAtlasToAssets();
                auto *linked = ResourceMgr::Get().GetLinkedAsset(_sprite_atlas);
                if (linked)
                {
                    ResourceMgr::Get().MarkAssetDirty(linked);
                    ResourceMgr::Get().SaveAsset(linked);
                    LOG_INFO("SpriteAtlasEditor: Applied");
                }
                _original_atlas_sprites = _sprite_atlas->Sprites();
                _original_atlas_sprite_guids.clear();
                for (const auto &sprite : _original_atlas_sprites)
                    _original_atlas_sprite_guids.push_back(ResourceMgr::Get().GetAssetGuid(sprite.get()));
                _original_sprite_count = static_cast<u32>(_sprite_items.size());
                RefreshAllUI();
                return;
            }

            AssetEditor::Save();
        }

        void SpriteAssetEditor::WriteAtlasToAssets()
        {
            const i32 selected_index = _selected_sprite_index;
            StoreSelectedSprite();
            Guid atlas_texture_guid = _editing._texture;
            if (atlas_texture_guid.IsEmpty() && _sprite_atlas->Texture() != nullptr)
                atlas_texture_guid = ResourceMgr::Get().GetAssetGuid(_sprite_atlas->Texture().get());
            Ref<Texture2D> atlas_texture;
            if (!atlas_texture_guid.IsEmpty())
                atlas_texture = ResourceMgr::Get().GetRef<Texture2D>(atlas_texture_guid);
            _sprite_atlas->SetTexture(atlas_texture_guid, std::move(atlas_texture));
            for (auto &item : _sprite_items)
            {
                if (!item._asset)
                    continue;
                _sprite_asset = item._asset;
                _editing = item._editing;
                _editing._texture = atlas_texture_guid;
                _original = item._original;
                _texture = ResolveTexture();
                ValidateEditingData();
                WriteToAsset();
                item._asset->Name(item._name);
                item._editing = _editing;
                item._original = _editing;
                item._original_name = item._name;
            }

            _selected_sprite_index = selected_index;
            LoadSelectedSprite();
            OnTextureChanged();
        }

        void SpriteAssetEditor::Revert()
        {
            if (!IsDirty()) return;
            if (_sprite_atlas)
            {
                for (const auto &item : _sprite_items)
                    ResourceMgr::Get().UnregisterSubAsset(item._guid);
                _sprite_atlas->Sprites() = _original_atlas_sprites;
                auto *owner = ResourceMgr::Get().GetLinkedAsset(_sprite_atlas);
                for (u32 i = 0u; owner != nullptr && i < _original_atlas_sprites.size(); ++i)
                {
                    const Guid guid = i < _original_atlas_sprite_guids.size()
                        ? _original_atlas_sprite_guids[i] : Guid::Generate();
                    ResourceMgr::Get().RegisterSubAsset(owner, guid, _original_atlas_sprites[i],
                                                        _original_atlas_sprites[i]->Name());
                }
                _sprite_items.clear();
                _sprite_asset = nullptr;
                _selected_sprite_indices.clear();
                _selected_sprite_index = -1;
                for (const auto &sprite_ref : _original_atlas_sprites)
                {
                    if (!sprite_ref)
                        continue;
                    _sprite_asset = sprite_ref.get();
                    ReadFromAsset();
                    _original = _editing;
                    const Guid guid = ResourceMgr::Get().GetAssetGuid(sprite_ref.get());
                    _sprite_items.push_back(SpriteAssetEditItem{
                        guid, sprite_ref->Name(), sprite_ref->Name(), sprite_ref.get(), _original, _editing});
                }
                _selected_sprite_index = _sprite_items.empty() ? -1 : 0;
                if (_selected_sprite_index >= 0)
                    _selected_sprite_indices = {_selected_sprite_index};
                LoadSelectedSprite();
                OnTextureChanged();
                RefreshAllUI();
                RefreshPreview();
                return;
            }
            AssetEditor::DiscardChanges();
        }

        void SpriteAssetEditor::ValidateEditingData()
        {
            auto& d = _editing;
            d._uv_rect.x = std::clamp(d._uv_rect.x, 0.0f, 1.0f);
            d._uv_rect.y = std::clamp(d._uv_rect.y, 0.0f, 1.0f);
            d._uv_rect.z = std::clamp(d._uv_rect.z, 0.0f, 1.0f);
            d._uv_rect.w = std::clamp(d._uv_rect.w, 0.0f, 1.0f);

            if (_texture)
            {
                f32 tw = (f32)_texture->Width(), th = (f32)_texture->Height();
                f32 mw = 1.0f / std::max(tw, 1.0f), mh = 1.0f / std::max(th, 1.0f);
                d._uv_rect.z = std::max(d._uv_rect.z, mw);
                d._uv_rect.w = std::max(d._uv_rect.w, mh);
                if (d._uv_rect.x + d._uv_rect.z > 1.0f) d._uv_rect.z = 1.0f - d._uv_rect.x;
                if (d._uv_rect.y + d._uv_rect.w > 1.0f) d._uv_rect.w = 1.0f - d._uv_rect.y;
            }

            d._pivot.x = std::clamp(d._pivot.x, 0.0f, 1.0f);
            d._pivot.y = std::clamp(d._pivot.y, 0.0f, 1.0f);
            d._size = std::max(d._size, 0.0001f);
            if (!std::isfinite(d._size)) d._size = 1.0f;

            d._border.x = std::max(d._border.x, 0.0f);
            d._border.y = std::max(d._border.y, 0.0f);
            d._border.z = std::max(d._border.z, 0.0f);
            d._border.w = std::max(d._border.w, 0.0f);

            if (_texture)
            {
                f32 sw = std::round(d._uv_rect.z * (f32)_texture->Width());
                f32 sh = std::round(d._uv_rect.w * (f32)_texture->Height());
                f32 left = d._border.y, right = d._border.x;
                f32 bottom = d._border.z, top = d._border.w;
                if (left + right > sw) { f32 s = sw / std::max(left + right, 1.0f); d._border.y = left * s; d._border.x = right * s; }
                if (top + bottom > sh) { f32 s = sh / std::max(top + bottom, 1.0f); d._border.w = top * s; d._border.z = bottom * s; }
            }

            auto cn = [](f32& v, f32 d = 0.0f) { if (!std::isfinite(v)) v = d; };
            cn(d._border.x); cn(d._border.y); cn(d._border.z); cn(d._border.w);
            cn(d._uv_rect.x, 0.0f); cn(d._uv_rect.y, 0.0f); cn(d._uv_rect.z, 1.0f); cn(d._uv_rect.w, 1.0f);
            cn(d._pivot.x, 0.5f); cn(d._pivot.y, 0.5f);
        }

        // =====================================================================
        // Refresh
        // =====================================================================
        void SpriteAssetEditor::RefreshAllUI()
        {
            RefreshAssetInfo();
            RefreshNameInput();
            RefreshUvInputs();
            RefreshPivotInputs();
            RefreshSizeInputs();
            RefreshBorderInputs();
            RefreshStatusBar();
            if (_img_tex_preview) _img_tex_preview->SetTexture(_texture);
            if (_txt_tex_field)
                _txt_tex_field->SetText(_editing._texture == Guid::EmptyGuid() ? String("None") : GuidToString(_editing._texture));
            const bool single_selection = _selected_sprite_indices.size() == 1u;
            if (_txt_multi_selection_notice)
            {
                _txt_multi_selection_notice->SetText(_selected_sprite_indices.empty()
                    ? "No sprite selected."
                    : "Multiple selection editing is not supported.");
                _txt_multi_selection_notice->SetVisible(!single_selection);
            }
            const auto set_inspector_enabled = [single_selection](UI::UIElement *element)
            {
                if (element != nullptr)
                    element->SetInteractiveEnabled(single_selection);
            };
            set_inspector_enabled(_sprite_name_input);
            set_inspector_enabled(_btn_select_tex);
            set_inspector_enabled(_btn_clear_tex);
            set_inspector_enabled(_uv_norm_x);
            set_inspector_enabled(_uv_norm_y);
            set_inspector_enabled(_uv_norm_w);
            set_inspector_enabled(_uv_norm_h);
            set_inspector_enabled(_uv_pix_x);
            set_inspector_enabled(_uv_pix_y);
            set_inspector_enabled(_uv_pix_w);
            set_inspector_enabled(_uv_pix_h);
            set_inspector_enabled(_pivot_x);
            set_inspector_enabled(_pivot_y);
            set_inspector_enabled(_size_input);
            set_inspector_enabled(_border_l);
            set_inspector_enabled(_border_r);
            set_inspector_enabled(_border_t);
            set_inspector_enabled(_border_b);
            RefreshSpriteList();
        }

        void SpriteAssetEditor::RefreshSpriteList()
        {
            if (!_sprite_list)
                return;

            _sprite_list->ClearItems();
            _sprite_list_items.clear();
            for (i32 i = 0; i < static_cast<i32>(_sprite_items.size()); ++i)
            {
                auto item = MakeRef<UI::Text>(_sprite_items[i]._name);
                _sprite_list->AddItem(item);
                _sprite_list_items.push_back(item.get());
                item->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 1.0f, 4.0f, 1.0f));
            }
            _sprite_list->SetSelectedIndices(_selected_sprite_indices);

            const bool can_edit_list = _supports_multiple_sprites;
            const bool has_single_selection = _selected_sprite_indices.size() <= 1u;
            if (_btn_add_sprite)
                _btn_add_sprite->SetInteractiveEnabled(can_edit_list && has_single_selection);
            if (_btn_remove_sprite)
            {
                const bool can_remove = can_edit_list && _sprite_items.size() > 1u && !_selected_sprite_indices.empty();
                _btn_remove_sprite->SetInteractiveEnabled(can_remove);
                if (_btn_delete_sprite)
                    _btn_delete_sprite->SetInteractiveEnabled(can_remove);
            }
            if (_btn_toolbar_add)
                _btn_toolbar_add->SetInteractiveEnabled(can_edit_list && has_single_selection);
            if (_btn_grid_slice)
                _btn_grid_slice->SetInteractiveEnabled(can_edit_list && _texture != nullptr);
            if (_btn_auto_slice)
                _btn_auto_slice->SetInteractiveEnabled(can_edit_list && _texture != nullptr);
            if (_btn_duplicate_sprite)
                _btn_duplicate_sprite->SetInteractiveEnabled(can_edit_list && has_single_selection && !_sprite_items.empty());
        }

        void SpriteAssetEditor::SelectSprite(i32 index)
        {
            if (index < 0 || index >= static_cast<i32>(_sprite_items.size()))
                return;

            SelectSprites({index}, index);
        }

        void SpriteAssetEditor::SelectSprites(const Vector<i32> &indices, i32 primary_index)
        {
            Vector<i32> valid_indices;
            for (i32 index : indices)
            {
                if (index >= 0 && index < static_cast<i32>(_sprite_items.size()) &&
                    std::find(valid_indices.begin(), valid_indices.end(), index) == valid_indices.end())
                    valid_indices.push_back(index);
            }
            if (valid_indices.empty())
            {
                StoreSelectedSprite();
                _selected_sprite_indices.clear();
                _selected_sprite_index = -1;
                _sprite_asset = nullptr;
                _editing = SpriteAssetEditData();
                _texture = nullptr;
                RefreshAllUI();
                RefreshPreview();
                return;
            }

            StoreSelectedSprite();
            _selected_sprite_indices = valid_indices;
            if (valid_indices.size() == 1u)
            {
                _selected_sprite_index = valid_indices.front();
                LoadSelectedSprite();
                OnTextureChanged();
            }
            else
            {
                _selected_sprite_index = -1;
            }
            RefreshAllUI();
            RefreshPreview();
        }

        String SpriteAssetEditor::MakeAtlasSpriteName(u32 index) const
        {
            const String atlas_name = _sprite_atlas != nullptr && !_sprite_atlas->Name().empty()
                ? _sprite_atlas->Name() : String("SpriteAtlas");
            return std::format("{}_{}", atlas_name, index);
        }

        void SpriteAssetEditor::AddSprite()
        {
            if (!_supports_multiple_sprites || !_sprite_atlas)
                return;

            StoreSelectedSprite();
            u32 name_index = static_cast<u32>(_sprite_items.size());
            String name = MakeAtlasSpriteName(name_index);
            auto has_name = [this](const String &candidate)
            {
                return std::any_of(_sprite_items.begin(), _sprite_items.end(),
                                   [&candidate](const SpriteAssetEditItem &item) { return item._name == candidate; });
            };
            while (has_name(name))
                name = MakeAtlasSpriteName(++name_index);
            auto sprite = MakeRef<Sprite>(name);
            if (!_editing._texture.IsEmpty())
            {
                Ref<Render::Texture2D> texture = ResourceMgr::Get().GetRef<Render::Texture2D>(_editing._texture);
                sprite->_texture.Set(_editing._texture, std::move(texture));
            }
            if (sprite->_texture != nullptr)
                _sprite_atlas->SetTexture(sprite->_texture.GetGuid(), sprite->_texture.Get());
            sprite->_uv_rect = _editing._uv_rect;
            sprite->_pivot = _editing._pivot;
            sprite->_size = _editing._size;
            sprite->_border = _editing._border;
            const Guid guid = Guid::Generate();
            auto *owner = ResourceMgr::Get().GetLinkedAsset(_sprite_atlas);
            if (!owner)
                return;
            ResourceMgr::Get().RegisterSubAsset(owner, guid, sprite, name);
            _sprite_atlas->Sprites().push_back(sprite);

            SpriteAssetEditItem item;
            item._guid = guid;
            item._name = name;
            item._original_name = name;
            item._asset = sprite.get();
            item._original = _editing;
            item._editing = _editing;
            _sprite_items.push_back(item);
            _selected_sprite_index = static_cast<i32>(_sprite_items.size() - 1u);
            _selected_sprite_indices = {_selected_sprite_index};
            LoadSelectedSprite();
            RefreshAllUI();
            RefreshPreview();
        }

        void SpriteAssetEditor::RemoveSprite()
        {
            if (!_supports_multiple_sprites || !_sprite_atlas || _sprite_items.size() <= 1u)
                return;

            StoreSelectedSprite();
            Vector<i32> remove_indices = _selected_sprite_indices;
            if (remove_indices.empty() && _selected_sprite_index >= 0)
                remove_indices.push_back(_selected_sprite_index);
            if (remove_indices.empty())
                return;
            std::sort(remove_indices.begin(), remove_indices.end());
            remove_indices.erase(std::unique(remove_indices.begin(), remove_indices.end()), remove_indices.end());
            while (remove_indices.size() >= _sprite_items.size())
                remove_indices.pop_back();

            i32 next_index = remove_indices.front();
            for (auto it = remove_indices.rbegin(); it != remove_indices.rend(); ++it)
            {
                const i32 index = *it;
                if (index < 0 || index >= static_cast<i32>(_sprite_items.size()))
                    continue;
                const SpriteAssetEditItem item = _sprite_items[index];
                ResourceMgr::Get().UnregisterSubAsset(item._guid);
                _sprite_atlas->Sprites().erase(
                    std::remove_if(_sprite_atlas->Sprites().begin(), _sprite_atlas->Sprites().end(),
                                   [&item](const Ref<Sprite> &sprite) { return sprite.get() == item._asset; }),
                    _sprite_atlas->Sprites().end());
                _sprite_items.erase(_sprite_items.begin() + index);
            }
            _selected_sprite_index = std::min(next_index, static_cast<i32>(_sprite_items.size() - 1u));
            _selected_sprite_indices = {_selected_sprite_index};
            LoadSelectedSprite();
            OnTextureChanged();
            RefreshAllUI();
            RefreshPreview();
        }

        void SpriteAssetEditor::DuplicateSprite()
        {
            AddSprite();
        }

        void SpriteAssetEditor::ShowGridSlicePopup()
        {
            if (!_supports_multiple_sprites || !_sprite_atlas || !_texture)
                return;

            auto cell_width = std::make_shared<u32>(32u);
            auto cell_height = std::make_shared<u32>(32u);
            auto padding = std::make_shared<u32>(0u);
            auto spacing = std::make_shared<u32>(0u);
            auto root = MakeRef<UI::Border>();
            root->GetSlot()->Size(Vector2f(260.0f, 190.0f));
            root->Thickness(1.0f);
            root->CornerRadius(4.0f);
            root->SlotPadding() = UI::Padding(6.0f);
            root->_bg_color = Color(0.095f, 0.10f, 0.11f, 0.98f);
            root->_border_color = Color(0.45f, 0.50f, 0.56f, 0.85f);
            auto *layout = root->AddChild<UI::VerticalBox>();
            layout->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto add_input = [&layout](const String &label, u32 value, const std::function<void(String)> &on_changed)
            {
                auto *row = layout->AddChild<UI::HorizontalBox>();
                row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight));
                auto *text = row->AddChild<UI::Text>(label);
                text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(90.0f, 0.0f));
                auto *input = row->AddChild<UI::InputBlock>(std::to_string(value));
                input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                input->_on_content_changed += std::function<void(String)>(on_changed);
            };
            add_input("Cell Width", *cell_width, [cell_width](String value)
            {
                if (auto parsed = StringUtils::ParseUInt32(value); parsed.has_value())
                    *cell_width = parsed.value();
            });
            add_input("Cell Height", *cell_height, [cell_height](String value)
            {
                if (auto parsed = StringUtils::ParseUInt32(value); parsed.has_value())
                    *cell_height = parsed.value();
            });
            add_input("Padding", *padding, [padding](String value)
            {
                if (auto parsed = StringUtils::ParseUInt32(value); parsed.has_value())
                    *padding = parsed.value();
            });
            add_input("Spacing", *spacing, [spacing](String value)
            {
                if (auto parsed = StringUtils::ParseUInt32(value); parsed.has_value())
                    *spacing = parsed.value();
            });

            auto *slice = layout->AddChild<UI::Button>("Slice");
            slice->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 24.0f)).Margin(Vector4f(0.0f, 6.0f, 0.0f, 0.0f));
            slice->OnMouseClick() += [this, cell_width, cell_height, padding, spacing](UI::UIEvent &event)
            {
                SliceGrid(*cell_width, *cell_height, *padding, *spacing);
                UI::UIManager::Get()->HidePopup();
                event._is_handled = true;
            };

            const auto rect = _btn_grid_slice->GetArrangeRect();
            UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, root);
        }

        void SpriteAssetEditor::SliceGrid(u32 cell_width, u32 cell_height, u32 padding, u32 spacing)
        {
            if (!_supports_multiple_sprites || !_sprite_atlas || !_texture || cell_width == 0u || cell_height == 0u)
                return;

            const u32 texture_width = _texture->Width();
            const u32 texture_height = _texture->Height();
            if (texture_width == 0u || texture_height == 0u || padding >= texture_width || padding >= texture_height)
                return;
            auto *owner = ResourceMgr::Get().GetLinkedAsset(_sprite_atlas);
            if (!owner)
                return;
            if (!_editing._texture.IsEmpty())
            {
                auto texture = ResourceMgr::Get().GetRef<Texture2D>(_editing._texture);
                _sprite_atlas->SetTexture(_editing._texture, std::move(texture));
            }

            StoreSelectedSprite();
            for (const auto &item : _sprite_items)
                ResourceMgr::Get().UnregisterSubAsset(item._guid);
            _sprite_atlas->Sprites().clear();
            _sprite_items.clear();

            const u64 stride_x = static_cast<u64>(cell_width) + spacing;
            const u64 stride_y = static_cast<u64>(cell_height) + spacing;
            const u64 max_x = texture_width - padding;
            const u64 max_y = texture_height - padding;
            u32 index = 0u;
            for (u64 y = padding; y + cell_height <= max_y; y += stride_y)
            {
                for (u64 x = padding; x + cell_width <= max_x; x += stride_x)
                {
                    const String name = MakeAtlasSpriteName(index++);
                    auto sprite = MakeRef<Sprite>(name);
                    sprite->_texture.Set(_sprite_atlas->TextureRef().GetGuid(), _sprite_atlas->TextureRef().Get());
                    sprite->_uv_rect = Vector4f(static_cast<f32>(x) / texture_width,
                                                1.0f - static_cast<f32>(y + cell_height) / texture_height,
                                                static_cast<f32>(cell_width) / texture_width,
                                                static_cast<f32>(cell_height) / texture_height);
                    const Guid guid = Guid::Generate();
                    ResourceMgr::Get().RegisterSubAsset(owner, guid, sprite, name);
                    _sprite_atlas->Sprites().push_back(sprite);
                    SpriteAssetEditItem item;
                    item._guid = guid;
                    item._name = name;
                    item._original_name = name;
                    item._asset = sprite.get();
                    item._original._texture = ResourceMgr::Get().GetAssetGuid(_sprite_atlas->Texture().get());
                    item._original._uv_rect = sprite->_uv_rect;
                    item._original._pivot = sprite->_pivot;
                    item._original._size = sprite->_size;
                    item._original._border = sprite->_border;
                    item._editing = item._original;
                    _sprite_items.push_back(item);
                }
            }

            _selected_sprite_index = _sprite_items.empty() ? -1 : 0;
            _selected_sprite_indices.clear();
            if (_selected_sprite_index >= 0)
                _selected_sprite_indices = {_selected_sprite_index};
            LoadSelectedSprite();
            OnTextureChanged();
            RefreshAllUI();
            RefreshPreview();
        }

        void SpriteAssetEditor::SliceAlpha(f32 threshold)
        {
            if (!_supports_multiple_sprites || !_sprite_atlas || !_texture)
                return;
            const u32 width = _texture->Width();
            const u32 height = _texture->Height();
            if (width == 0u || height == 0u)
                return;
            Color first_pixel;
            if (!_texture->TryGetPixel(0u, 0u, first_pixel))
            {
                LOG_WARNING("SpriteAtlasEditor: Auto Slice requires CPU texture pixel data");
                return;
            }
            threshold = std::clamp(threshold, 0.0f, 1.0f);
            auto *owner = ResourceMgr::Get().GetLinkedAsset(_sprite_atlas);
            if (!owner)
                return;

            Vector<u8> visited(width * height, 0u);
            Vector<Vector4Int> bounds;
            const auto is_opaque = [this, threshold](u32 x, u32 y)
            {
                Color color;
                return _texture->TryGetPixel(static_cast<u16>(x), static_cast<u16>(y), color) && color.a > threshold;
            };
            const Ref<Texture2D> atlas_texture = _sprite_atlas->Texture();
            if (!atlas_texture)
            {
                LOG_WARNING("SpriteAtlasEditor: Auto Slice skipped because the atlas texture is missing");
                return;
            }
            for (u32 y = 0u; y < height; ++y)
            {
                for (u32 x = 0u; x < width; ++x)
                {
                    const u32 offset = y * width + x;
                    if (visited[offset] != 0u || !is_opaque(x, y))
                        continue;
                    visited[offset] = 1u;
                    std::queue<Vector2Int> pending;
                    pending.emplace(static_cast<i32>(x), static_cast<i32>(y));
                    Vector4Int rect(static_cast<i32>(x), static_cast<i32>(y), static_cast<i32>(x), static_cast<i32>(y));
                    while (!pending.empty())
                    {
                        const Vector2Int pixel = pending.front();
                        pending.pop();
                        rect.x = std::min(rect.x, pixel.x);
                        rect.y = std::min(rect.y, pixel.y);
                        rect.z = std::max(rect.z, pixel.x);
                        rect.w = std::max(rect.w, pixel.y);
                        const Array<Vector2Int, 4> kNeighbors = {
                            Vector2Int(1, 0), Vector2Int(-1, 0), Vector2Int(0, 1), Vector2Int(0, -1)};
                        for (const auto &neighbor : kNeighbors)
                        {
                            const i32 nx = pixel.x + neighbor.x;
                            const i32 ny = pixel.y + neighbor.y;
                            if (nx < 0 || ny < 0 || nx >= static_cast<i32>(width) || ny >= static_cast<i32>(height))
                                continue;
                            const u32 neighbor_offset = static_cast<u32>(ny) * width + static_cast<u32>(nx);
                            if (visited[neighbor_offset] != 0u || !is_opaque(static_cast<u32>(nx), static_cast<u32>(ny)))
                                continue;
                            visited[neighbor_offset] = 1u;
                            pending.emplace(nx, ny);
                        }
                    }
                    bounds.push_back(rect);
                }
            }

            StoreSelectedSprite();
            for (const auto &item : _sprite_items)
                ResourceMgr::Get().UnregisterSubAsset(item._guid);
            _sprite_atlas->Sprites().clear();
            _sprite_items.clear();
            const Guid texture_guid = ResourceMgr::Get().GetAssetGuid(atlas_texture.get());
            u32 index = 0u;
            for (const auto &rect : bounds)
            {
                const u32 sprite_width = static_cast<u32>(rect.z - rect.x + 1);
                const u32 sprite_height = static_cast<u32>(rect.w - rect.y + 1);
                const String name = MakeAtlasSpriteName(index++);
                auto sprite = MakeRef<Sprite>(name);
                sprite->_texture.Set(texture_guid, atlas_texture);
                sprite->_uv_rect = Vector4f(static_cast<f32>(rect.x) / width,
                                            1.0f - static_cast<f32>(rect.y + static_cast<i32>(sprite_height)) / height,
                                            static_cast<f32>(sprite_width) / width,
                                            static_cast<f32>(sprite_height) / height);
                const Guid guid = Guid::Generate();
                ResourceMgr::Get().RegisterSubAsset(owner, guid, sprite, name);
                _sprite_atlas->Sprites().push_back(sprite);
                SpriteAssetEditItem item;
                item._guid = guid;
                item._name = name;
                item._original_name = name;
                item._asset = sprite.get();
                item._original._texture = texture_guid;
                item._original._uv_rect = sprite->_uv_rect;
                item._original._pivot = sprite->_pivot;
                item._original._size = sprite->_size;
                item._original._border = sprite->_border;
                item._editing = item._original;
                _sprite_items.push_back(item);
            }
            _selected_sprite_index = _sprite_items.empty() ? -1 : 0;
            _selected_sprite_indices.clear();
            if (_selected_sprite_index >= 0)
                _selected_sprite_indices = {_selected_sprite_index};
            LoadSelectedSprite();
            OnTextureChanged();
            RefreshAllUI();
            RefreshPreview();
        }

        void SpriteAssetEditor::StoreSelectedSprite()
        {
            const bool invalid_index = _selected_sprite_index < 0 ||
                _selected_sprite_index >= static_cast<i32>(_sprite_items.size());
            if (invalid_index)
                return;
            _sprite_items[_selected_sprite_index]._original = _original;
            _sprite_items[_selected_sprite_index]._editing = _editing;
        }

        void SpriteAssetEditor::LoadSelectedSprite()
        {
            if (_selected_sprite_index < 0 || _selected_sprite_index >= static_cast<i32>(_sprite_items.size()))
                return;
            _original = _sprite_items[_selected_sprite_index]._original;
            _editing = _sprite_items[_selected_sprite_index]._editing;
            _sprite_asset = _sprite_items[_selected_sprite_index]._asset;
            RefreshNameInput();
        }

        void SpriteAssetEditor::RefreshAssetInfo()
        {
            if (!_sprite_asset && !_sprite_atlas)
                return;
            Object *asset_object = _sprite_atlas ? static_cast<Object *>(_sprite_atlas) : _sprite_asset;
            auto* linked = ResourceMgr::Get().GetLinkedAsset(asset_object);
            if (_txt_asset_name) _txt_asset_name->SetText(_sprite_atlas ? _sprite_atlas->Name() : _sprite_asset->Name());
            if (_txt_asset_type) _txt_asset_type->SetText(_sprite_atlas ? "SpriteAtlas" : "SpriteAsset");
            if (_txt_asset_guid) _txt_asset_guid->SetText(linked ? linked->GetGuid().ToString() : "-");
            if (linked && _txt_asset_path)
                _txt_asset_path->SetText(ToChar(linked->_asset_path));
            if (_texture)
            {
                if (_txt_tex_name) _txt_tex_name->SetText(_texture->Name());
                if (_txt_tex_resolution) _txt_tex_resolution->SetText(FormatResolution(_texture->Width(), _texture->Height()));
                if (_txt_tex_format) _txt_tex_format->SetText(std::format("{}", (i32)_texture->PixelFormat()));
                f32 sw = std::round(_editing._uv_rect.z * (f32)_texture->Width());
                f32 sh = std::round(_editing._uv_rect.w * (f32)_texture->Height());
                if (_txt_sprite_size) _txt_sprite_size->SetText(std::format("{} x {}", (i32)sw, (i32)sh));
            }
            else
            {
                if (_txt_tex_name) _txt_tex_name->SetText("None");
                if (_txt_tex_resolution) _txt_tex_resolution->SetText("-");
                if (_txt_tex_format) _txt_tex_format->SetText("-");
                if (_txt_sprite_size) _txt_sprite_size->SetText("-");
            }
        }

        void SpriteAssetEditor::RefreshUvInputs()
        {
            if (_is_syncing_uv) return;
            _is_syncing_uv = true;
            auto& d = _editing;
            if (_uv_norm_x) _uv_norm_x->SetContent(FormatFloat(d._uv_rect.x));
            if (_uv_norm_y) _uv_norm_y->SetContent(FormatFloat(d._uv_rect.y));
            if (_uv_norm_w) _uv_norm_w->SetContent(FormatFloat(d._uv_rect.z));
            if (_uv_norm_h) _uv_norm_h->SetContent(FormatFloat(d._uv_rect.w));
            if (_texture)
            {
                if (_uv_pix_x) _uv_pix_x->SetContent(std::to_string((i32)std::round(d._uv_rect.x * _texture->Width())));
                if (_uv_pix_y) _uv_pix_y->SetContent(std::to_string((i32)std::round(d._uv_rect.y * _texture->Height())));
                if (_uv_pix_w) _uv_pix_w->SetContent(std::to_string((i32)std::round(d._uv_rect.z * _texture->Width())));
                if (_uv_pix_h) _uv_pix_h->SetContent(std::to_string((i32)std::round(d._uv_rect.w * _texture->Height())));
            }
            else { if (_uv_pix_x) _uv_pix_x->SetContent("-"); if (_uv_pix_y) _uv_pix_y->SetContent("-"); if (_uv_pix_w) _uv_pix_w->SetContent("-"); if (_uv_pix_h) _uv_pix_h->SetContent("-"); }
            _is_syncing_uv = false;
        }

        void SpriteAssetEditor::RefreshPivotInputs()
        {
            if (_is_syncing_pivot) return;
            _is_syncing_pivot = true;
            if (_pivot_x) _pivot_x->SetContent(FormatFloat(_editing._pivot.x));
            if (_pivot_y) _pivot_y->SetContent(FormatFloat(_editing._pivot.y));
            _is_syncing_pivot = false;
        }

        void SpriteAssetEditor::RefreshSizeInputs()
        {
            if (_is_syncing_size) return;
            _is_syncing_size = true;
            if (_size_input) _size_input->SetContent(FormatFloat(_editing._size, 1));
            _is_syncing_size = false;
        }

        void SpriteAssetEditor::RefreshBorderInputs()
        {
            if (_is_syncing_border) return;
            _is_syncing_border = true;
            if (_border_l) _border_l->SetContent(std::format("{}", (i32)std::round(_editing._border.y)));
            if (_border_r) _border_r->SetContent(std::format("{}", (i32)std::round(_editing._border.x)));
            if (_border_t) _border_t->SetContent(std::format("{}", (i32)std::round(_editing._border.w)));
            if (_border_b) _border_b->SetContent(std::format("{}", (i32)std::round(_editing._border.z)));
            _is_syncing_border = false;
        }

        void SpriteAssetEditor::RefreshNameInput()
        {
            if (_is_syncing_name || !_sprite_name_input)
                return;
            _is_syncing_name = true;
            const String name = _selected_sprite_index >= 0 && _selected_sprite_index < static_cast<i32>(_sprite_items.size())
                ? _sprite_items[_selected_sprite_index]._name : String();
            _sprite_name_input->SetContent(name);
            _is_syncing_name = false;
        }

        void SpriteAssetEditor::RefreshStatusBar()
        {
            if (!_status_text) return;
            String text;
            if (_texture)
            {
                text += "Tex: " + _texture->Name() + "   " + FormatResolution(_texture->Width(), _texture->Height()) + "   ";
                i32 sw = (i32)std::round(_editing._uv_rect.z * _texture->Width());
                i32 sh = (i32)std::round(_editing._uv_rect.w * _texture->Height());
                text += std::format("Spr: {}x{}   ", sw, sh);
            }
            else { text += "No Texture   "; }
            text += std::format("UV: {:.3f},{:.3f},{:.3f},{:.3f}   Z: {}%",
                               _editing._uv_rect.x, _editing._uv_rect.y,
                               _editing._uv_rect.z, _editing._uv_rect.w,
                                _preview ? (i32)std::round(_preview->GetZoom() * 100.0f) : 100);
            _status_text->SetText(text);
        }

        void SpriteAssetEditor::RefreshPreview() { if (_preview) _preview->InvalidateLayout(); }

        // =====================================================================
        // Texture
        // =====================================================================
        Render::Texture2D* SpriteAssetEditor::ResolveTexture()
        {
            if (_editing._texture == Guid::EmptyGuid()) return nullptr;
            Ref<Render::Texture2D> tex = ResourceMgr::Get().GetRef<Render::Texture2D>(_editing._texture);
            if (!tex) tex = ResourceMgr::Get().Load<Render::Texture2D>(_editing._texture);
            return tex.get();
        }

        void SpriteAssetEditor::OnTextureChanged()
        {
            _texture = ResolveTexture();
            ValidateEditingData();
            if (_preview)
            {
                _preview->SetTexture(_texture);
                _preview->SetShowPixelGrid(_show_grid);
            }
            if (_img_tex_preview) _img_tex_preview->SetTexture(_texture);
            RefreshPreview();
            RefreshAssetInfo();
        }

        // =====================================================================
        // Coordinate Conversion
        // =====================================================================
        Vector2f SpriteAssetEditor::UVToTexturePixel(const Vector2f& uv) const
        {
            if (!_texture) return Vector2f::kZero;
            return Vector2f(uv.x * (f32)_texture->Width(), (1.0f - uv.y) * (f32)_texture->Height());
        }

        Vector2f SpriteAssetEditor::TexturePixelToUV(const Vector2f& pixel) const
        {
            if (!_texture || _texture->Width() == 0 || _texture->Height() == 0) return Vector2f::kZero;
            return Vector2f(pixel.x / (f32)_texture->Width(), 1.0f - pixel.y / (f32)_texture->Height());
        }

        // =====================================================================
        // UI Helpers
        // =====================================================================
        UI::InputBlock* SpriteAssetEditor::AddLabeledInput(UI::UIElement* parent, const String& label, f32 value, f32)
        {
            auto* row = parent->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight));
            auto* lbl = row->AddChild<UI::Text>(label);
            lbl->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            lbl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(50.0f, 0.0f));
            auto* input = row->AddChild<UI::InputBlock>(FormatFloat(value));
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            return input;
        }

        UI::Text* SpriteAssetEditor::AddSectionTitle(UI::UIElement* parent, const String& title)
        {
            auto* txt = parent->AddChild<UI::Text>(title);
            txt->_color = Color(0.85f, 0.85f, 0.85f, 1.0f);
            txt->FontSize(13.0f);
            txt->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 20.0f)).Margin(Vector4f(4.0f, 6.0f, 0.0f, 2.0f));
            return txt;
        }

        UI::HorizontalBox* SpriteAssetEditor::AddPropertyRow(UI::UIElement* parent, const String& label)
        {
            auto* row = parent->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(4.0f, 0.0f, 4.0f, 0.0f));
            auto* lbl = row->AddChild<UI::Text>(label);
            lbl->_color = Color(0.65f, 0.65f, 0.65f, 1.0f);
            lbl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(50.0f, 0.0f));
            return row;
        }

        // =====================================================================
        // Build Toolbar
        // =====================================================================
        void SpriteAssetEditor::BuildToolbar(UI::HorizontalBox* toolbar)
        {
            toolbar->SlotPadding() = UI::Padding(4.0f, 2.0f, 4.0f, 2.0f);

            _btn_apply = toolbar->AddChild<UI::Button>("Save");
            _btn_apply->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            _btn_apply->OnMouseClick() += [this](UI::UIEvent& e) { Apply(); e._is_handled = true; };

            _btn_revert = toolbar->AddChild<UI::Button>("Discard");
            _btn_revert->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 6.0f, 0.0f));
            _btn_revert->OnMouseClick() += [this](UI::UIEvent& e) { Revert(); e._is_handled = true; };

            _btn_undo = toolbar->AddChild<UI::Button>("Undo");
            _btn_undo->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(48.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            _btn_undo->OnMouseClick() += [this](UI::UIEvent& e) { if (g_pCommandMgr) g_pCommandMgr->Undo(); e._is_handled = true; };

            _btn_redo = toolbar->AddChild<UI::Button>("Redo");
            _btn_redo->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(48.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 8.0f, 0.0f));
            _btn_redo->OnMouseClick() += [this](UI::UIEvent& e) { if (g_pCommandMgr) g_pCommandMgr->Redo(); e._is_handled = true; };

            _btn_toolbar_add = toolbar->AddChild<UI::Button>("Add");
            _btn_toolbar_add->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(44.0f, 0.0f));
            _btn_toolbar_add->OnMouseClick() += [this](UI::UIEvent &event) { AddSprite(); event._is_handled = true; };

            _btn_grid_slice = toolbar->AddChild<UI::Button>("Slice Grid");
            _btn_grid_slice->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(72.0f, 0.0f));
            _btn_grid_slice->OnMouseClick() += [this](UI::UIEvent &event)
            {
                ShowGridSlicePopup();
                event._is_handled = true;
            };

            _btn_auto_slice = toolbar->AddChild<UI::Button>("Auto Slice");
            _btn_auto_slice->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(72.0f, 0.0f));
            _btn_auto_slice->OnMouseClick() += [this](UI::UIEvent &event)
            {
                SliceAlpha(0.1f);
                event._is_handled = true;
            };

            _btn_duplicate_sprite = toolbar->AddChild<UI::Button>("Duplicate");
            _btn_duplicate_sprite->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(68.0f, 0.0f));
            _btn_duplicate_sprite->OnMouseClick() += [this](UI::UIEvent &event)
            {
                DuplicateSprite();
                event._is_handled = true;
            };

            _btn_delete_sprite = toolbar->AddChild<UI::Button>("Delete");
            _btn_delete_sprite->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(52.0f, 0.0f));
            _btn_delete_sprite->OnMouseClick() += [this](UI::UIEvent &event)
            {
                RemoveSprite();
                event._is_handled = true;
            };

            auto* sep1 = toolbar->AddChild<UI::Text>("|");
            sep1->_color = Color(0.4f, 0.4f, 0.4f, 1.0f);
            sep1->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(12.0f, 0.0f));

            _chk_snap = toolbar->AddChild<UI::CheckBox>(); _chk_snap->SetChecked(true);
            _chk_snap->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(20.0f, 0.0f));
            _chk_snap->OnMouseClick() += [this](UI::UIEvent& e) { _snap_to_pixel = _chk_snap->IsChecked(); e._is_handled = true; };
            auto* snap_l = toolbar->AddChild<UI::Text>("Snap"); snap_l->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            snap_l->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(32.0f, 0.0f));

            _chk_grid = toolbar->AddChild<UI::CheckBox>(); _chk_grid->SetChecked(true);
            _chk_grid->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(20.0f, 0.0f));
            _chk_grid->OnMouseClick() += [this](UI::UIEvent& e) {
                _show_grid = _chk_grid->IsChecked();
                if (_preview) _preview->SetShowPixelGrid(_show_grid);
                RefreshPreview();
                e._is_handled = true;
            };
            auto* grid_l = toolbar->AddChild<UI::Text>("Grid"); grid_l->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            grid_l->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(28.0f, 0.0f));

            _chk_pivot = toolbar->AddChild<UI::CheckBox>(); _chk_pivot->SetChecked(true);
            _chk_pivot->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(20.0f, 0.0f));
            _chk_pivot->OnMouseClick() += [this](UI::UIEvent& e) { _show_pivot = _chk_pivot->IsChecked(); RefreshPreview(); e._is_handled = true; };
            auto* pvt_l = toolbar->AddChild<UI::Text>("Pivot"); pvt_l->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            pvt_l->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(34.0f, 0.0f));

            _chk_border = toolbar->AddChild<UI::CheckBox>(); _chk_border->SetChecked(true);
            _chk_border->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(20.0f, 0.0f));
            _chk_border->OnMouseClick() += [this](UI::UIEvent& e) { _show_border = _chk_border->IsChecked(); RefreshPreview(); e._is_handled = true; };
            auto* bdr_l = toolbar->AddChild<UI::Text>("Border"); bdr_l->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            bdr_l->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(38.0f, 0.0f));

            auto* sep2 = toolbar->AddChild<UI::Text>("|");
            sep2->_color = Color(0.4f, 0.4f, 0.4f, 1.0f);
            sep2->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(12.0f, 0.0f));

            _txt_zoom_label = toolbar->AddChild<UI::Text>("100%");
            _txt_zoom_label->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            _txt_zoom_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f));

            _btn_fit = toolbar->AddChild<UI::Button>("Fit");
            _btn_fit->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(32.0f, 0.0f)).Margin(Vector4f(2.0f, 0.0f, 2.0f, 0.0f));
            _btn_fit->OnMouseClick() += [this](UI::UIEvent& e) { FitTexture(); e._is_handled = true; };

            _btn_1to1 = toolbar->AddChild<UI::Button>("1:1");
            _btn_1to1->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(32.0f, 0.0f));
            _btn_1to1->OnMouseClick() += [this](UI::UIEvent& e) { if (_preview) _preview->ResetZoom(); RefreshPreview(); e._is_handled = true; };

            auto* spacer = toolbar->AddChild<UI::Text>("");
            spacer->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        }

        // =====================================================================
        // Build Left Panel
        // =====================================================================
        void SpriteAssetEditor::BuildLeftPanel(UI::VerticalBox* left)
        {
            left->SlotPadding() = UI::Padding(4.0f);

            AddSectionTitle(left, "Asset Info");
            _txt_asset_name = left->AddChild<UI::Text>("-");
            _txt_asset_name->_color = Color(0.8f, 0.8f, 0.8f, 1.0f);
            _txt_asset_name->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 16.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));

            auto* pl = left->AddChild<UI::Text>("Path:"); pl->_color = Color(0.5f, 0.5f, 0.5f, 1.0f); pl->FontSize(11.0f);
            pl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 2.0f, 0.0f, 0.0f));
            _txt_asset_path = left->AddChild<UI::Text>("-"); _txt_asset_path->_color = Color(0.6f, 0.6f, 0.6f, 1.0f); _txt_asset_path->FontSize(11.0f);
            _txt_asset_path->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));

            auto* gl = left->AddChild<UI::Text>("GUID:"); gl->_color = Color(0.5f, 0.5f, 0.5f, 1.0f); gl->FontSize(11.0f);
            gl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 2.0f, 0.0f, 0.0f));
            _txt_asset_guid = left->AddChild<UI::Text>("-"); _txt_asset_guid->_color = Color(0.5f, 0.5f, 0.5f, 1.0f); _txt_asset_guid->FontSize(10.0f);
            _txt_asset_guid->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));

            _txt_asset_type = left->AddChild<UI::Text>("-"); _txt_asset_type->_color = Color(0.6f, 0.6f, 0.6f, 1.0f);
            _txt_asset_type->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));

            auto* sep = left->AddChild<UI::Border>(); sep->_bg_color = Color(0.3f, 0.3f, 0.3f, 1.0f);
            sep->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 1.0f)).Margin(Vector4f(0.0f, 6.0f, 0.0f, 6.0f));

            AddSectionTitle(left, "Texture");

            auto make_row = [&](const String& lbl, UI::Text*& out) {
                auto* r = left->AddChild<UI::HorizontalBox>();
                r->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 16.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));
                auto* l = r->AddChild<UI::Text>(lbl); l->_color = Color(0.5f, 0.5f, 0.5f, 1.0f); l->FontSize(11.0f);
                l->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f));
                out = r->AddChild<UI::Text>("-"); out->_color = Color(0.6f, 0.6f, 0.6f, 1.0f); out->FontSize(11.0f);
                out->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            };
            make_row("Name:", _txt_tex_name);
            make_row("Res:", _txt_tex_resolution);
            make_row("Format:", _txt_tex_format);
            make_row("Sprite:", _txt_sprite_size);

            BuildSpriteListSection(left);
        }

        void SpriteAssetEditor::BuildSpriteListSection(UI::VerticalBox* parent)
        {
            auto* sep = parent->AddChild<UI::Border>();
            sep->_bg_color = Color(0.3f, 0.3f, 0.3f, 1.0f);
            sep->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 1.0f)).Margin(Vector4f(0.0f, 6.0f, 0.0f, 6.0f));

            AddSectionTitle(parent, "Sprites");

            auto* button_row = parent->AddChild<UI::HorizontalBox>();
            button_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 24.0f));

            _btn_add_sprite = button_row->AddChild<UI::Button>("Add");
            _btn_add_sprite->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(52.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            _btn_add_sprite->OnMouseClick() += [this](UI::UIEvent& e)
            {
                AddSprite();
                e._is_handled = true;
            };

            _btn_remove_sprite = button_row->AddChild<UI::Button>("Remove");
            _btn_remove_sprite->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(68.0f, 0.0f));
            _btn_remove_sprite->OnMouseClick() += [this](UI::UIEvent& e)
            {
                RemoveSprite();
                e._is_handled = true;
            };
            _btn_add_sprite->SetInteractiveEnabled(false);
            _btn_remove_sprite->SetInteractiveEnabled(false);

            _sprite_list = parent->AddChild<UI::ListView>();
            _sprite_list->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .Margin(Vector4f(0.0f, 4.0f, 0.0f, 0.0f));
            _sprite_list->SetStyleId("DropdownPopup");
            _sprite_list->SetBorder(Color(0.25f, 0.26f, 0.29f, 1.0f), 1.0f);
            _sprite_list->_on_item_clicked += [this](UI::UIElement *, i32 index)
            {
                SelectSprites(_sprite_list->GetSelectedIndices(), index);
            };
        }

        // =====================================================================
        // Build Center Panel
        // =====================================================================
        void SpriteAssetEditor::BuildCenterPanel(UI::Border* center)
        {
            _preview = center->AddChild<SpritePreviewWidget>(this);
            _preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            UI::DropHandler handler;
            handler._can_drop = [](const UI::DragPayload& p) -> bool { return p._type == UI::EDragType::kTexture || p._type == UI::EDragType::kFile; };
            handler._on_drop = [this](const UI::DragPayload&, f32, f32) { LOG_INFO("Drop in SpriteAssetEditor preview"); };
            _preview->SetDropHandler(std::move(handler));
        }

        // =====================================================================
        // Build Right Panel
        // =====================================================================
        void SpriteAssetEditor::BuildRightPanel(UI::VerticalBox* right)
        {
            right->SlotPadding() = UI::Padding(4.0f);
            AddSectionTitle(right, "Sprite");
            _txt_multi_selection_notice = right->AddChild<UI::Text>("Multiple selection editing is not supported.");
            _txt_multi_selection_notice->_color = Color(1.0f, 0.75f, 0.25f, 1.0f);
            _txt_multi_selection_notice->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 30.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 4.0f));
            _txt_multi_selection_notice->SetVisible(false);
            auto *name_row = AddPropertyRow(right, "Name");
            _sprite_name_input = name_row->AddChild<UI::InputBlock>("");
            _sprite_name_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _sprite_name_input->_on_content_changed += [this](String value)
            {
                if (_is_syncing_name || _selected_sprite_index < 0 ||
                    _selected_sprite_index >= static_cast<i32>(_sprite_items.size()))
                    return;
                _sprite_items[_selected_sprite_index]._name = value;
                CommitLiveEdit();
                RefreshSpriteList();
                RefreshStatusBar();
            };
            BuildTextureSection(right);
            BuildUvRectSection(right);
            BuildPivotSection(right);
            BuildSizeSection(right);
            BuildBorderSection(right);
        }

        void SpriteAssetEditor::BuildTextureSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Texture");

            auto* tr = parent->AddChild<UI::HorizontalBox>();
            tr->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, kTexPreviewSize)).Margin(Vector4f(4.0f, 4.0f, 4.0f, 4.0f));

            _img_tex_preview = tr->AddChild<UI::Image>();
            _img_tex_preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size(Vector2f(kTexPreviewSize, kTexPreviewSize));

            auto* ti = tr->AddChild<UI::VerticalBox>();
            ti->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).Margin(Vector4f(6.0f, 0.0f, 0.0f, 0.0f));

            _txt_tex_field = ti->AddChild<UI::Text>("None");
            _txt_tex_field->_color = Color(0.8f, 0.8f, 0.8f, 1.0f);
            _txt_tex_field->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 18.0f));

            auto* br = ti->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(0.0f, 4.0f, 0.0f, 0.0f));

            _btn_select_tex = br->AddChild<UI::Button>("Select");
            _btn_select_tex->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            _btn_select_tex->OnMouseClick() += [this](UI::UIEvent& e)
            {
                constexpr f32 kPopupWidth = 320.0f;
                constexpr f32 kPopupHeight = 360.0f;
                constexpr f32 kSearchHeight = 26.0f;
                constexpr f32 kItemHeight = 28.0f;

                auto root = MakeRef<UI::Border>();
                root->Name("SpriteTexturePicker");
                root->GetSlot()->Size({kPopupWidth, kPopupHeight});
                root->Thickness(1.0f);
                root->CornerRadius(4.0f);
                root->SlotPadding() = UI::Padding(5.0f);
                root->_bg_color = Color(0.095f, 0.10f, 0.11f, 0.98f);
                root->_border_color = Color(0.45f, 0.50f, 0.56f, 0.85f);

                auto* layout = root->AddChild<UI::VerticalBox>();
                layout->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

                auto* search = layout->AddChild<UI::InputBlock>("");
                search->Name("TextureSearch");
                search->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, kSearchHeight}).Margin({2.0f, 2.0f, 2.0f, 5.0f});

                auto* list = layout->AddChild<UI::ListView>();
                list->Name("TextureChoices");
                list->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                list->SetStyleId("DropdownPopup");

                Vector<TextureChoice> choices;
                for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                {
                    Asset* asset = it->second.get();
                    if (!asset || asset->_asset_type != Render::Texture2D::StaticType())
                        continue;

                    TextureChoice choice;
                    choice._guid = asset->GetGuid();
                    choice._name = asset->_p_obj ? asset->_p_obj->Name() : asset->Name();
                    choice._path = ToChar(asset->_asset_path);
                    if (choice._name.empty())
                        choice._name = ToChar(PathUtils::GetFileName(asset->_asset_path));
                    choices.emplace_back(std::move(choice));
                }
                std::sort(choices.begin(), choices.end(), [](const TextureChoice& lhs, const TextureChoice& rhs)
                {
                    const String left = StringUtils::ToLower(lhs._name);
                    const String right = StringUtils::ToLower(rhs._name);
                    return left == right ? lhs._path < rhs._path : left < right;
                });

                auto all_choices = std::make_shared<Vector<TextureChoice>>(std::move(choices));
                auto rebuild_list = [this, list, all_choices, kItemHeight](const String& query)
                {
                    list->ClearItems();
                    const String needle = StringUtils::ToLower(query);
                    i32 visible_count = 0;
                    for (const TextureChoice& choice : *all_choices)
                    {
                        const String name = StringUtils::ToLower(choice._name);
                        const String path = StringUtils::ToLower(choice._path);
                        if (!needle.empty() && name.find(needle) == String::npos && path.find(needle) == String::npos)
                            continue;

                        auto item = MakeRef<UI::Button>(choice._name + "  [" + choice._path + "]");
                        list->AddItem(item);
                        item->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                            .Size({0.0f, kItemHeight}).Margin({2.0f, 1.0f, 2.0f, 1.0f});
                        const Guid guid = choice._guid;
                        item->OnMouseClick() += [this, guid](UI::UIEvent& event)
                        {
                            SpriteAssetEditData data = _editing;
                            data._texture = guid;
                            if (g_pCommandMgr)
                                g_pCommandMgr->ExecuteCommand(std::make_unique<SpriteAssetEditCommand>(this, _editing, data));
                            else
                            {
                                ApplyEditData(data);
                                if (GetAsset() != nullptr && !GetAsset()->IsDirty())
                                    GetAsset()->MarkModified();
                            }
                            UI::UIManager::Get()->HidePopup();
                            event._is_handled = true;
                        };
                        ++visible_count;
                    }

                    if (visible_count == 0)
                    {
                        auto empty = MakeRef<UI::Text>(needle.empty() ? "No textures found" : "No matching textures");
                        list->AddItem(empty);
                        empty->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                            .Size({0.0f, kItemHeight}).Margin({6.0f, 4.0f, 6.0f, 4.0f});
                        empty->_color = Color(0.55f, 0.55f, 0.55f, 1.0f);
                    }
                };

                rebuild_list("");
                search->_on_content_changed += [rebuild_list](String value) { rebuild_list(value); };
                auto close_on_escape = [](UI::UIEvent& event)
                {
                    if (event._key_code == EKey::kESCAPE)
                    {
                        UI::UIManager::Get()->HidePopup();
                        event._is_handled = true;
                    }
                };
                root->OnKeyDown() += close_on_escape;
                search->OnKeyDown() += close_on_escape;

                const Vector4f rect = e._current_target->GetArrangeRect();
                UI::UIManager::Get()->HidePopup();
                UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, root);
                search->RequestFocus();
                e._is_handled = true;
            };

            _btn_clear_tex = br->AddChild<UI::Button>("Clear");
            _btn_clear_tex->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(48.0f, 0.0f));
            _btn_clear_tex->OnMouseClick() += [this](UI::UIEvent& e) {
                SpriteAssetEditData data = _editing;
                data._texture = Guid::EmptyGuid();
                if (g_pCommandMgr)
                    g_pCommandMgr->ExecuteCommand(std::make_unique<SpriteAssetEditCommand>(this, _editing, data));
                else
                {
                    ApplyEditData(data);
                    if (GetAsset() != nullptr && !GetAsset()->IsDirty())
                        GetAsset()->MarkModified();
                }
                e._is_handled = true;
            };
        }

        void SpriteAssetEditor::BuildUvRectSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "UV Rect");

            auto* nl = parent->AddChild<UI::Text>("Normalized");
            nl->_color = Color(0.5f, 0.5f, 0.5f, 1.0f); nl->FontSize(11.0f);
            nl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 2.0f, 0.0f, 0.0f));

            _uv_norm_x = AddLabeledInput(parent, "X", _editing._uv_rect.x);
            _uv_norm_y = AddLabeledInput(parent, "Y", _editing._uv_rect.y);
            _uv_norm_w = AddLabeledInput(parent, "W", _editing._uv_rect.z);
            _uv_norm_h = AddLabeledInput(parent, "H", _editing._uv_rect.w);

            auto on_norm = [this](f32& f) { return [this, &f](String v) { if (_is_syncing_uv) return; f = std::clamp((f32)std::atof(v.c_str()), 0.0f, 1.0f); ValidateEditingData(); CommitLiveEdit(); RefreshUvInputs(); RefreshPreview(); }; };
            _uv_norm_x->_on_content_changed += on_norm(_editing._uv_rect.x);
            _uv_norm_y->_on_content_changed += on_norm(_editing._uv_rect.y);
            _uv_norm_w->_on_content_changed += on_norm(_editing._uv_rect.z);
            _uv_norm_h->_on_content_changed += on_norm(_editing._uv_rect.w);

            auto* pl = parent->AddChild<UI::Text>("Pixels");
            pl->_color = Color(0.5f, 0.5f, 0.5f, 1.0f); pl->FontSize(11.0f);
            pl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 14.0f)).Margin(Vector4f(4.0f, 6.0f, 0.0f, 0.0f));

            _uv_pix_x = AddLabeledInput(parent, "X", 0.0f);
            _uv_pix_y = AddLabeledInput(parent, "Y", 0.0f);
            _uv_pix_w = AddLabeledInput(parent, "W", 0.0f);
            _uv_pix_h = AddLabeledInput(parent, "H", 0.0f);

            auto on_pix = [this](bool is_x) { return [this, is_x](String v) { if (_is_syncing_uv || !_texture) return; f32 p = (f32)std::atof(v.c_str()); if (is_x) _editing._uv_rect.x = p / (f32)_texture->Width(); else _editing._uv_rect.y = p / (f32)_texture->Height(); ValidateEditingData(); CommitLiveEdit(); RefreshUvInputs(); RefreshPreview(); }; };
            _uv_pix_x->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.x = (f32)std::atof(v.c_str()) / (f32)_texture->Width(); ValidateEditingData(); CommitLiveEdit(); RefreshUvInputs(); RefreshPreview(); };
            _uv_pix_y->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.y = (f32)std::atof(v.c_str()) / (f32)_texture->Height(); ValidateEditingData(); CommitLiveEdit(); RefreshUvInputs(); RefreshPreview(); };
            _uv_pix_w->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.z = (f32)std::atof(v.c_str()) / (f32)_texture->Width(); ValidateEditingData(); CommitLiveEdit(); RefreshUvInputs(); RefreshPreview(); };
            _uv_pix_h->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.w = (f32)std::atof(v.c_str()) / (f32)_texture->Height(); ValidateEditingData(); CommitLiveEdit(); RefreshUvInputs(); RefreshPreview(); };

            auto* br = parent->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 4.0f, 4.0f, 0.0f));

            auto* bf = br->AddChild<UI::Button>("Full Tex");
            bf->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(60.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            bf->OnMouseClick() += [this](UI::UIEvent& e) { _editing._uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f); ValidateEditingData(); CommitLiveEdit(); RefreshAllUI(); RefreshPreview(); e._is_handled = true; };

            auto* bt = br->AddChild<UI::Button>("Trim");
            bt->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(40.0f, 0.0f));
            bt->OnMouseClick() += [this](UI::UIEvent& e) { LOG_INFO("Trim not implemented"); e._is_handled = true; };
        }

        void SpriteAssetEditor::BuildPivotSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Pivot");

            _pivot_x = AddLabeledInput(parent, "X", _editing._pivot.x);
            _pivot_y = AddLabeledInput(parent, "Y", _editing._pivot.y);

            _pivot_x->_on_content_changed += [this](String v) { if (_is_syncing_pivot) return; _editing._pivot.x = std::clamp((f32)std::atof(v.c_str()), 0.0f, 1.0f); ValidateEditingData(); CommitLiveEdit(); RefreshPivotInputs(); RefreshPreview(); };
            _pivot_y->_on_content_changed += [this](String v) { if (_is_syncing_pivot) return; _editing._pivot.y = std::clamp((f32)std::atof(v.c_str()), 0.0f, 1.0f); ValidateEditingData(); CommitLiveEdit(); RefreshPivotInputs(); RefreshPreview(); };

            struct PP { const char* lbl; f32 x; f32 y; };
            static const PP pr[9] = { {"TL",0,1},{"T",.5f,1},{"TR",1,1},{"L",0,.5f},{"C",.5f,.5f},{"R",1,.5f},{"BL",0,0},{"B",.5f,0},{"BR",1,0} };

            auto* gr = parent->AddChild<UI::VerticalBox>();
            gr->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 72.0f)).Margin(Vector4f(4.0f, 4.0f, 4.0f, 0.0f));

            for (i32 r = 0; r < 3; ++r) {
                auto* hb = gr->AddChild<UI::HorizontalBox>();
                hb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                for (i32 c = 0; c < 3; ++c) {
                    auto& p = pr[r*3+c];
                    auto* btn = hb->AddChild<UI::Button>(p.lbl);
                    btn->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).Margin(Vector4f(1.0f));
                    btn->OnMouseClick() += [this, px=p.x, py=p.y](UI::UIEvent& e) { _editing._pivot = Vector2f(px, py); CommitLiveEdit(); RefreshPivotInputs(); RefreshPreview(); e._is_handled = true; };
                }
            }
        }

        void SpriteAssetEditor::BuildSizeSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Size");

            _size_input = AddLabeledInput(parent, "Size", _editing._size);
            _size_input->_on_content_changed += [this](String v) {
                if (_is_syncing_size) return;
                _editing._size = std::max((f32)std::atof(v.c_str()), 0.0001f);
                ValidateEditingData();
                CommitLiveEdit();
                RefreshSizeInputs();
            };

            auto* br = parent->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 0.0f));

            auto* bu = br->AddChild<UI::Button>("From UV");
            bu->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(60.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            bu->OnMouseClick() += [this](UI::UIEvent& e) {
                if (_texture)
                    _editing._size = std::max(std::round(_editing._uv_rect.w * (f32)_texture->Height()), 0.0001f);
                CommitLiveEdit();
                RefreshSizeInputs();
                e._is_handled = true;
            };

            auto* bz = br->AddChild<UI::Button>("Reset");
            bz->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f));
            bz->OnMouseClick() += [this](UI::UIEvent& e) { _editing._size = 1.0f; CommitLiveEdit(); RefreshSizeInputs(); e._is_handled = true; };
        }

        void SpriteAssetEditor::BuildBorderSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Border");

            _border_l = AddLabeledInput(parent, "L", _editing._border.y);
            _border_r = AddLabeledInput(parent, "R", _editing._border.x);
            _border_t = AddLabeledInput(parent, "T", _editing._border.w);
            _border_b = AddLabeledInput(parent, "B", _editing._border.z);

            auto on_b = [this](f32& f) { return [this, &f](String v) { if (_is_syncing_border) return; f = std::max((f32)std::atof(v.c_str()), 0.0f); ValidateEditingData(); CommitLiveEdit(); RefreshBorderInputs(); RefreshPreview(); }; };
            _border_l->_on_content_changed += on_b(_editing._border.y);
            _border_r->_on_content_changed += on_b(_editing._border.x);
            _border_t->_on_content_changed += on_b(_editing._border.w);
            _border_b->_on_content_changed += on_b(_editing._border.z);

            auto* br = parent->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 4.0f, 4.0f, 0.0f));

            auto* bz = br->AddChild<UI::Button>("Reset");
            bz->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            bz->OnMouseClick() += [this](UI::UIEvent& e) { _editing._border = Vector4f::kZero; CommitLiveEdit(); RefreshBorderInputs(); RefreshPreview(); e._is_handled = true; };

            auto* be = br->AddChild<UI::Button>("Equal");
            be->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f));
            be->OnMouseClick() += [this](UI::UIEvent& e) { if (_texture) { f32 sw = std::round(_editing._uv_rect.z*(f32)_texture->Width()); f32 sh = std::round(_editing._uv_rect.w*(f32)_texture->Height()); f32 b = std::round(std::min(sw,sh)*0.1f); _editing._border = Vector4f(b,b,b,b); CommitLiveEdit(); RefreshBorderInputs(); RefreshPreview(); } e._is_handled = true; };
        }

        // =====================================================================
        // Build Status Bar
        // =====================================================================
        void SpriteAssetEditor::BuildStatusBar(UI::HorizontalBox* status_bar)
        {
            status_bar->SlotPadding() = UI::Padding(6.0f, 2.0f, 6.0f, 2.0f);
            auto* border = dynamic_cast<UI::Border*>(status_bar->GetParent());
            if (border) { border->_bg_color = Color(0.14f, 0.15f, 0.16f, 1.0f); border->Thickness(Vector4f(0.0f, 1.0f, 0.0f, 0.0f)); border->_border_color = Color(0.25f, 0.26f, 0.28f, 1.0f); }
            _status_text = status_bar->AddChild<UI::Text>("Ready");
            _status_text->_color = Color(0.55f, 0.55f, 0.55f, 1.0f);
            _status_text->FontSize(11.0f);
            _status_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        }

        // =====================================================================
        // SpritePreviewWidget
        // =====================================================================
        SpritePreviewWidget::SpritePreviewWidget(SpriteAssetEditor* editor) : _editor(editor)
        {
            SetNavigationEnabled(false);
            SetWantsMouseEvents(true);
            SetInteractiveEnabled(true);

            OnMouseDown() += [this](UI::UIEvent& e) {
                if (!_editor || !_editor->_texture) return;
                auto mode = HitTest(e._mouse_position);
                _editor->_drag_start_mouse = e._mouse_position;
                _editor->_drag_start_data = _editor->_editing;
                // Alt+LeftButton or MiddleButton always pans the view, regardless of hit test
                if (Input::IsKeyDown(EKey::kMENU) || Input::IsKeyDown(EKey::kMBUTTON)) {
                    _editor->_drag_mode = SpriteEditorDragMode::kPanView;
                } else {
                    _editor->_drag_mode = mode;
                }
                e._is_handled = true;
            };

            OnMouseMove() += [this](UI::UIEvent& e) {
                if (_editor && _editor->_drag_mode != SpriteEditorDragMode::kNone) { ProcessDrag(e._mouse_position); _editor->RefreshPreview(); e._is_handled = true; }
            };

            OnMouseUp() += [this](UI::UIEvent&) {
                if (!_editor || _editor->_drag_mode == SpriteEditorDragMode::kNone) return;
                auto mode = _editor->_drag_mode;
                _editor->_drag_mode = SpriteEditorDragMode::kNone;
                if (mode != SpriteEditorDragMode::kPanView && _editor->_editing != _editor->_drag_start_data && g_pCommandMgr)
                    g_pCommandMgr->ExecuteCommand(std::make_unique<SpriteAssetEditCommand>(_editor, _editor->_drag_start_data, _editor->_editing));
                _editor->RefreshAllUI();
            };

            OnMouseScroll() += [this](UI::UIEvent& e) {
                if (!_editor) return;
                if (e._scroll_delta > 0) _editor->_preview->ZoomAt(e._mouse_position, kSpriteZoomStep);
                else if (e._scroll_delta < 0) _editor->_preview->ZoomAt(e._mouse_position, 1.0f / kSpriteZoomStep);
                _editor->RefreshPreview();
                e._is_handled = true;
            };
        }

        Vector2f SpritePreviewWidget::MeasureDesiredSize() { return Vector2f(400.0f, 300.0f); }
        void SpritePreviewWidget::Update(f32 dt) { UIElement::Update(dt); }

        void SpritePreviewWidget::RenderImpl(UI::UIRenderer& r)
        {
            auto cr = GetContentRect();
            if (cr.z <= 0 || cr.w <= 0) return;
            r.PushScissor(cr);
            if (_editor && _editor->_selected_sprite_indices.empty())
            {
                DrawBackground(r, cr);
                r.DrawText("No sprite selected.", Vector2f(cr.x + 16.0f, cr.y + cr.w * 0.5f - 8.0f),
                           Matrix4x4f::Identity(), 14.0f, Color(0.7f, 0.7f, 0.7f, 1.0f));
            }
            else if (_editor && _editor->_selected_sprite_indices.size() > 1u)
            {
                DrawBackground(r, cr);
                r.DrawText("Multiple selection preview is not supported.",
                           Vector2f(cr.x + 16.0f, cr.y + cr.w * 0.5f - 8.0f), Matrix4x4f::Identity(), 14.0f,
                           Color(1.0f, 0.75f, 0.25f, 1.0f));
            }
            else if (_editor && _editor->_texture) {
                TexturePreviewWidget::RenderImpl(r);
                r.PopScissor();
                return;
            }
            r.PopScissor();
        }

        void SpritePreviewWidget::DrawOverlay(UI::UIRenderer& r, const Vector4f&)
        {
            if (_editor == nullptr)
                return;
            if (_editor->_show_border) DrawBorderOverlay(r);
            if (_editor->_show_pivot) DrawPivotOverlay(r);
            DrawUvRectOverlay(r);
        }

        void SpritePreviewWidget::DrawUvRectOverlay(UI::UIRenderer& r)
        {
            if(!_editor||!_editor->_texture)return;
            Vector4f us=GetUvRectInScreen(); if(us.z<=0||us.w<=0)return;
            auto cr=GetContentRect(); f32 d=0.5f;
            UIBrush b; b._type=EUIBrushType::kColor; b._tint=kColorOverlay;
            if(us.y>cr.y) r.DrawQuad(Vector4f(cr.x,cr.y,cr.z,us.y-cr.y),b,d);
            f32 bt=us.y+us.w; if(bt<cr.y+cr.w) r.DrawQuad(Vector4f(cr.x,bt,cr.z,cr.y+cr.w-bt),b,d);
            if(us.x>cr.x) r.DrawQuad(Vector4f(cr.x,us.y,us.x-cr.x,us.w),b,d);
            f32 rt=us.x+us.z; if(rt<cr.x+cr.z) r.DrawQuad(Vector4f(rt,us.y,cr.x+cr.z-rt,us.w),b,d);
            r.DrawBox(Vector2f(us.x,us.y),Vector2f(us.z,us.w),2.0f,kColorUvRect,d+0.1f);
            auto hs=GetUvHandles(); for(auto& h:hs){UIBrush hb;hb._type=EUIBrushType::kColor;hb._tint=kColorUvRect;r.DrawQuad(h,hb,d+0.2f);}
        }

        void SpritePreviewWidget::DrawPivotOverlay(UI::UIRenderer& r)
        {
            if(!_editor||!_editor->_texture)return;
            Vector4f us=GetUvRectInScreen(); if(us.z<=0||us.w<=0)return;
            auto& pv=_editor->_editing._pivot; auto& uv=_editor->_editing._uv_rect;
            f32 tw=(f32)_editor->_texture->Width(), th=(f32)_editor->_texture->Height();
            f32 spw=uv.z*tw, sph=uv.w*th, px=uv.x*tw+pv.x*spw, py=uv.y*th+pv.y*sph;
            Vector2f ps=TexturePixelToScreen(Vector2f(px,py)); f32 d=0.9f;
            f32 ls=TexturePixelToScreen(Vector2f(uv.x*tw,py)).x, rs=TexturePixelToScreen(Vector2f((uv.x+uv.z)*tw,py)).x;
            f32 ts=TexturePixelToScreen(Vector2f(px,uv.y*th)).y, bs=TexturePixelToScreen(Vector2f(px,(uv.y+uv.w)*th)).y;
            r.DrawLine(Vector2f(ls,ps.y),Vector2f(rs,ps.y),1.0f,kColorPivot,d);
            r.DrawLine(Vector2f(ps.x,ts),Vector2f(ps.x,bs),1.0f,kColorPivot,d);
            f32 rp=4.0f; UIBrush br; br._type=EUIBrushType::kColor; br._tint=kColorPivot;
            r.DrawQuad(Vector4f(ps.x-rp,ps.y-rp,rp*2,rp*2),br,d+0.1f);
        }

        void SpritePreviewWidget::DrawBorderOverlay(UI::UIRenderer& r)
        {
            if(!_editor||!_editor->_texture)return;
            auto& bd=_editor->_editing._border; auto& uv=_editor->_editing._uv_rect;
            f32 tw=(f32)_editor->_texture->Width(),th=(f32)_editor->_texture->Height();
            f32 sl=uv.x*tw,sr=(uv.x+uv.z)*tw,st=uv.y*th,sb=(uv.y+uv.w)*th;
            f32 bl=sl+bd.y,br=sr-bd.x,bb=st+bd.z,bt=sb-bd.w;
            auto cr=GetContentRect(); f32 d=0.7f;
            Vector2f sbl=TexturePixelToScreen(Vector2f(bl,0)),sbr=TexturePixelToScreen(Vector2f(br,0));
            Vector2f sbt=TexturePixelToScreen(Vector2f(0,bt)),sbb=TexturePixelToScreen(Vector2f(0,bb));
            r.DrawLine(Vector2f(sbl.x,cr.y),Vector2f(sbl.x,cr.y+cr.w),2.0f,kColorBorder,d);
            r.DrawLine(Vector2f(sbr.x,cr.y),Vector2f(sbr.x,cr.y+cr.w),2.0f,kColorBorder,d);
            r.DrawLine(Vector2f(cr.x,sbt.y),Vector2f(cr.x+cr.z,sbt.y),2.0f,kColorBorder,d);
            r.DrawLine(Vector2f(cr.x,sbb.y),Vector2f(cr.x+cr.z,sbb.y),2.0f,kColorBorder,d);
        }

        SpriteEditorDragMode SpritePreviewWidget::HitTest(const Vector2f& sp) const
        {
            if(!_editor||!_editor->_texture)return SpriteEditorDragMode::kNone;
            Vector2f tp=ScreenToTexturePixel(sp);
            auto& uv=_editor->_editing._uv_rect; auto& pv=_editor->_editing._pivot; auto& bd=_editor->_editing._border;
            f32 tw=(f32)_editor->_texture->Width(),th=(f32)_editor->_texture->Height();
            f32 ux=uv.x*tw,uy=uv.y*th,uw=uv.z*tw,uh=uv.w*th,hr=kHandleRadius/std::max(GetZoom(),0.1f);
            auto d2=[](f32 x1,f32 y1,f32 x2,f32 y2){return (x1-x2)*(x1-x2)+(y1-y2)*(y1-y2);};
            auto ne=[](f32 v,f32 t){return std::abs(v-t);};
            auto ir=[](f32 v,f32 lo,f32 hi){return v>=lo&&v<=hi;};

            f32 ppx=ux+pv.x*uw,ppy=uy+pv.y*uh;
            if(d2(tp.x,tp.y,ppx,ppy)<=hr*hr)return SpriteEditorDragMode::kMovePivot;
            if(d2(tp.x,tp.y,ux,uy)<=hr*hr)return SpriteEditorDragMode::kResizeUvTopLeft;
            if(d2(tp.x,tp.y,ux+uw,uy)<=hr*hr)return SpriteEditorDragMode::kResizeUvTopRight;
            if(d2(tp.x,tp.y,ux,uy+uh)<=hr*hr)return SpriteEditorDragMode::kResizeUvBottomLeft;
            if(d2(tp.x,tp.y,ux+uw,uy+uh)<=hr*hr)return SpriteEditorDragMode::kResizeUvBottomRight;
            if(ne(tp.x,ux)<=hr&&ir(tp.y,uy,uy+uh))return SpriteEditorDragMode::kResizeUvLeft;
            if(ne(tp.x,ux+uw)<=hr&&ir(tp.y,uy,uy+uh))return SpriteEditorDragMode::kResizeUvRight;
            if(ne(tp.y,uy)<=hr&&ir(tp.x,ux,ux+uw))return SpriteEditorDragMode::kResizeUvTop;
            if(ne(tp.y,uy+uh)<=hr&&ir(tp.x,ux,ux+uw))return SpriteEditorDragMode::kResizeUvBottom;
            f32 blp=ux+bd.y,brp=ux+uw-bd.x,btp=uy+uh-bd.w,bbp=uy+bd.z;
            if(ne(tp.x,blp)<=hr&&ir(tp.y,uy,uy+uh))return SpriteEditorDragMode::kMoveBorderLeft;
            if(ne(tp.x,brp)<=hr&&ir(tp.y,uy,uy+uh))return SpriteEditorDragMode::kMoveBorderRight;
            if(ne(tp.y,btp)<=hr&&ir(tp.x,ux,ux+uw))return SpriteEditorDragMode::kMoveBorderTop;
            if(ne(tp.y,bbp)<=hr&&ir(tp.x,ux,ux+uw))return SpriteEditorDragMode::kMoveBorderBottom;
            // When UV rect covers the entire texture, kMoveUvRect can't actually move
            // (clamped to [0, 1-uv.z]=[0,0]), so fall through to kPanView instead
            bool uv_covers_tex = (uv.x <= 0.001f && uv.y <= 0.001f && uv.x + uv.z >= 0.999f && uv.y + uv.w >= 0.999f);
            if(!uv_covers_tex && ir(tp.x,ux,ux+uw)&&ir(tp.y,uy,uy+uh))return SpriteEditorDragMode::kMoveUvRect;
            if(ir(tp.x,0,tw)&&ir(tp.y,0,th))return SpriteEditorDragMode::kPanView;
            return SpriteEditorDragMode::kNone;
        }

        Vector4f SpritePreviewWidget::GetUvRectInPixels() const {
            if(!_editor||!_editor->_texture)return Vector4f(0,0,0,0);
            auto& uv=_editor->_editing._uv_rect; f32 tw=(f32)_editor->_texture->Width(),th=(f32)_editor->_texture->Height();
            return Vector4f(uv.x*tw,uv.y*th,uv.z*tw,uv.w*th);
        }
        Vector4f SpritePreviewWidget::GetUvRectInScreen() const {
            auto pr=GetUvRectInPixels(); Vector2f tl=TexturePixelToScreen(Vector2f(pr.x,pr.y)),br=TexturePixelToScreen(Vector2f(pr.x+pr.z,pr.y+pr.w));
            return Vector4f(std::min(tl.x,br.x),std::min(tl.y,br.y),std::abs(br.x-tl.x),std::abs(br.y-tl.y));
        }
        Vector<Vector4f> SpritePreviewWidget::GetUvHandles() const {
            Vector<Vector4f> h; Vector4f us=GetUvRectInScreen(); f32 hs=kHandleRadius*2;
            h.push_back(Vector4f(us.x-kHandleRadius,us.y-kHandleRadius,hs,hs)); h.push_back(Vector4f(us.x+us.z-kHandleRadius,us.y-kHandleRadius,hs,hs));
            h.push_back(Vector4f(us.x-kHandleRadius,us.y+us.w-kHandleRadius,hs,hs)); h.push_back(Vector4f(us.x+us.z-kHandleRadius,us.y+us.w-kHandleRadius,hs,hs));
            f32 mx=us.x+us.z*.5f,my=us.y+us.w*.5f;
            h.push_back(Vector4f(mx-kHandleRadius,us.y-kHandleRadius,hs,hs));h.push_back(Vector4f(mx-kHandleRadius,us.y+us.w-kHandleRadius,hs,hs));
            h.push_back(Vector4f(us.x-kHandleRadius,my-kHandleRadius,hs,hs));h.push_back(Vector4f(us.x+us.z-kHandleRadius,my-kHandleRadius,hs,hs));
            return h;
        }

        void SpritePreviewWidget::ProcessDrag(const Vector2f& mp)
        {
            if(!_editor||_editor->_drag_mode==SpriteEditorDragMode::kNone)return;
            auto& md=_editor->_drag_mode; auto& d=_editor->_editing; auto& uv=d._uv_rect; auto* tex=_editor->_texture;
            if(!tex)return; Vector2f tp=ScreenToTexturePixel(mp); f32 tw=(f32)tex->Width(),th=(f32)tex->Height();
            bool snap=_editor->_snap_to_pixel,alt=Input::IsKeyDown(EKey::kMENU),shift=Input::IsKeyDown(EKey::kLSHIFT)||Input::IsKeyDown(EKey::kRSHIFT);
            if(alt)snap=false; f32 ux=uv.x*tw,uy=uv.y*th,uw=uv.z*tw,uh=uv.w*th;

            switch(md){
            case SpriteEditorDragMode::kPanView:{Vector2f sp=ScreenToPreview(_editor->_drag_start_mouse),cp=ScreenToPreview(mp);SetPan(GetPan()+(cp-sp)*GetZoom());_editor->_drag_start_mouse=mp;break;}
            case SpriteEditorDragMode::kMoveUvRect:{Vector2f stp=ScreenToTexturePixel(_editor->_drag_start_mouse);f32 dx=tp.x-stp.x,dy=tp.y-stp.y;uv.x=std::clamp(uv.x+dx/tw,0.0f,1.0f-uv.z);uv.y=std::clamp(uv.y+dy/th,0.0f,1.0f-uv.w);_editor->_drag_start_mouse=mp;break;}
            case SpriteEditorDragMode::kResizeUvLeft:{uv.x=std::clamp(tp.x/tw,0.0f,(ux+uw-1)/tw);uv.z=(ux+uw)/tw-uv.x;break;}
            case SpriteEditorDragMode::kResizeUvRight:{uv.z=std::clamp(tp.x/tw-uv.x,1.0f/tw,1.0f-uv.x);break;}
            case SpriteEditorDragMode::kResizeUvTop:{uv.y=std::clamp(tp.y/th,0.0f,(uy+uh-1)/th);uv.w=(uy+uh)/th-uv.y;break;}
            case SpriteEditorDragMode::kResizeUvBottom:{uv.w=std::clamp(tp.y/th-uv.y,1.0f/th,1.0f-uv.y);break;}
            case SpriteEditorDragMode::kResizeUvTopLeft:{uv.x=std::clamp(tp.x/tw,0.0f,(ux+uw-1)/tw);uv.y=std::clamp(tp.y/th,0.0f,(uy+uh-1)/th);uv.z=(ux+uw)/tw-uv.x;uv.w=(uy+uh)/th-uv.y;break;}
            case SpriteEditorDragMode::kResizeUvTopRight:{uv.y=std::clamp(tp.y/th,0.0f,(uy+uh-1)/th);uv.z=std::clamp(tp.x/tw-uv.x,1.0f/tw,1.0f-uv.x);uv.w=(uy+uh)/th-uv.y;break;}
            case SpriteEditorDragMode::kResizeUvBottomLeft:{uv.x=std::clamp(tp.x/tw,0.0f,(ux+uw-1)/tw);uv.w=std::clamp(tp.y/th-uv.y,1.0f/th,1.0f-uv.y);uv.z=(ux+uw)/tw-uv.x;break;}
            case SpriteEditorDragMode::kResizeUvBottomRight:{uv.z=std::clamp(tp.x/tw-uv.x,1.0f/tw,1.0f-uv.x);uv.w=std::clamp(tp.y/th-uv.y,1.0f/th,1.0f-uv.y);break;}
            case SpriteEditorDragMode::kMovePivot:{f32 lx=tp.x-ux,ly=tp.y-uy,nx=lx/std::max(uw,1.0f),ny=ly/std::max(uh,1.0f);if(shift){nx=std::round(nx*2)*.5f;ny=std::round(ny*2)*.5f;}d._pivot=Vector2f(std::clamp(nx,0.0f,1.0f),std::clamp(ny,0.0f,1.0f));break;}
            case SpriteEditorDragMode::kMoveBorderLeft:{f32 v=std::clamp(tp.x-ux,0.0f,uw-d._border.x);d._border.y=snap?std::round(v):v;break;}
            case SpriteEditorDragMode::kMoveBorderRight:{f32 v=std::clamp(ux+uw-tp.x,0.0f,uw-d._border.y);d._border.x=snap?std::round(v):v;break;}
            case SpriteEditorDragMode::kMoveBorderTop:{f32 v=std::clamp(uy+uh-tp.y,0.0f,uh-d._border.z);d._border.w=snap?std::round(v):v;break;}
            case SpriteEditorDragMode::kMoveBorderBottom:{f32 v=std::clamp(tp.y-uy,0.0f,uh-d._border.w);d._border.z=snap?std::round(v):v;break;}
            default:break;
            }
            _editor->ValidateEditingData();
            if(md!=SpriteEditorDragMode::kPanView){_editor->RefreshUvInputs();_editor->RefreshPivotInputs();_editor->RefreshBorderInputs();_editor->RefreshAssetInfo();_editor->RefreshStatusBar();}
        }

    } // namespace Editor
} // namespace Ailu
