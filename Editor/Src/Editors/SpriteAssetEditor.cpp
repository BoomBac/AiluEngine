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
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

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
            constexpr f32 kZoomMin           = 0.1f;
            constexpr f32 kZoomMax           = 32.0f;
            constexpr f32 kZoomStep          = 1.1f;
            constexpr f32 kChessTileSize     = 16.0f;

            const Color kColorUvRect    = Color(0.2f, 0.8f, 1.0f, 1.0f);
            const Color kColorPivot     = Color(0.2f, 1.0f, 0.2f, 1.0f);
            const Color kColorBorder    = Color(1.0f, 0.6f, 0.0f, 1.0f);
            const Color kColorOverlay   = Color(0.0f, 0.0f, 0.0f, 0.5f);
            const Color kColorChessA    = Color(0.75f, 0.75f, 0.75f, 1.0f);
            const Color kColorChessB    = Color(0.55f, 0.55f, 0.55f, 1.0f);

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
        class SpriteAssetEditCommand : public ICommand
        {
            DECLARE_COMMAND(SpriteAssetEdit)
        public:
            SpriteAssetEditCommand(SpriteAssetEditor* editor,
                                   const SpriteAssetEditData& old_data,
                                   const SpriteAssetEditData& new_data)
                : _editor(editor), _old_data(old_data), _new_data(new_data) {}

            void Execute() override { if (_editor) _editor->ApplyEditData(_new_data); }
            void Undo() override { if (_editor) _editor->ApplyEditData(_old_data); }

        private:
            SpriteAssetEditor* _editor;
            SpriteAssetEditData _old_data;
            SpriteAssetEditData _new_data;
        };

        // =====================================================================
        // Constructor
        // =====================================================================
        SpriteAssetEditor::SpriteAssetEditor()
            : DockWindow("Sprite Editor", Vector2f(1000.0f, 650.0f))
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
            auto* left_scroll = left_border->AddChild<UI::ScrollView>();
            left_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto* left_vb = left_scroll->AddChild<UI::VerticalBox>();
            left_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
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

        // =====================================================================
        // Open / Close
        // =====================================================================
        void SpriteAssetEditor::Open(Sprite* asset)
        {
            if (!asset) return;
            _sprite_asset = asset;
            ReadFromAsset();
            _original = _editing;
            OnTextureChanged();
            SetTitle("Sprite Editor - " + asset->Name());
            RefreshAllUI();
        }

        void SpriteAssetEditor::Close()
        {
            _sprite_asset = nullptr;
            _texture = nullptr;
        }

        void SpriteAssetEditor::ApplyEditData(const SpriteAssetEditData& data)
        {
            const bool texture_changed = _editing._texture != data._texture;
            _editing = data;
            if (texture_changed)
                OnTextureChanged();
            else
                ValidateEditingData();
            RefreshAllUI();
            RefreshPreview();
        }

        // =====================================================================
        // Update
        // =====================================================================
        void SpriteAssetEditor::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (!_sprite_asset) return;

            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);

            if (ctrl && Input::IsKeyDownAccurate(EKey::kS)) Apply();
            if (ctrl && Input::IsKeyDownAccurate(EKey::kZ)) { if (g_pCommandMgr) g_pCommandMgr->Undo(); }
            if (ctrl && Input::IsKeyDownAccurate(EKey::kY)) { if (g_pCommandMgr) g_pCommandMgr->Redo(); }
            if (Input::IsKeyDownAccurate(EKey::kF)) FitTexture();
            if (Input::IsKeyDownAccurate(EKey::k1)) { _preview_zoom = 1.0f; RefreshPreview(); }
            if (Input::IsKeyDownAccurate(EKey::kG)) { _show_grid = !_show_grid; if (_chk_grid) _chk_grid->SetChecked(_show_grid); RefreshPreview(); }
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
            if (_txt_zoom_label) _txt_zoom_label->SetText(std::format("{}%", (i32)std::round(_preview_zoom * 100.0f)));
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
                    _preview_zoom = std::min(zw, zh);
                    _preview_pan = Vector2f::kZero;
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
            // Convert runtime Ref<Texture2D> to Guid for editor buffer
            _editing._texture = Guid::EmptyGuid();
            if (_sprite_asset->_texture)
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
            if (_editing._texture != Guid::EmptyGuid())
                _sprite_asset->_texture = ResourceMgr::Get().GetRef<Texture2D>(_editing._texture);
            else
                _sprite_asset->_texture = nullptr;
            _sprite_asset->_uv_rect = _editing._uv_rect;
            _sprite_asset->_pivot   = _editing._pivot;
            _sprite_asset->_size    = _editing._size;
            _sprite_asset->_border  = _editing._border;
        }

        void SpriteAssetEditor::Apply()
        {
            if (!_sprite_asset || !IsDirty()) return;
            ValidateEditingData();
            WriteToAsset();
            _original = _editing;
            auto* linked = ResourceMgr::Get().GetLinkedAsset(_sprite_asset);
            if (linked)
            {
                ResourceMgr::Get().SaveAsset(linked);
                LOG_INFO("SpriteAssetEditor: Applied");
            }
            RefreshAllUI();
        }

        void SpriteAssetEditor::Revert()
        {
            if (!IsDirty()) return;
            _editing = _original;
            OnTextureChanged();
            RefreshAllUI();
            RefreshPreview();
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
            d._size.x = std::max(d._size.x, 0.0001f);
            d._size.y = std::max(d._size.y, 0.0001f);
            if (!std::isfinite(d._size.x)) d._size.x = 1.0f;
            if (!std::isfinite(d._size.y)) d._size.y = 1.0f;

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
            RefreshUvInputs();
            RefreshPivotInputs();
            RefreshSizeInputs();
            RefreshBorderInputs();
            RefreshStatusBar();
            if (_img_tex_preview) _img_tex_preview->SetTexture(_texture);
            if (_txt_tex_field)
                _txt_tex_field->SetText(_editing._texture == Guid::EmptyGuid() ? String("None") : GuidToString(_editing._texture));
        }

        void SpriteAssetEditor::RefreshAssetInfo()
        {
            if (!_sprite_asset) return;
            auto* linked = ResourceMgr::Get().GetLinkedAsset(_sprite_asset);
            if (_txt_asset_name) _txt_asset_name->SetText(_sprite_asset->Name());
            if (_txt_asset_type) _txt_asset_type->SetText("SpriteAsset");
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
            if (_size_w) _size_w->SetContent(FormatFloat(_editing._size.x, 1));
            if (_size_h) _size_h->SetContent(FormatFloat(_editing._size.y, 1));
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
                               (i32)std::round(_preview_zoom * 100.0f));
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

            _btn_apply = toolbar->AddChild<UI::Button>("Apply");
            _btn_apply->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            _btn_apply->OnMouseClick() += [this](UI::UIEvent& e) { Apply(); e._is_handled = true; };

            _btn_revert = toolbar->AddChild<UI::Button>("Revert");
            _btn_revert->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 6.0f, 0.0f));
            _btn_revert->OnMouseClick() += [this](UI::UIEvent& e) { Revert(); e._is_handled = true; };

            _btn_undo = toolbar->AddChild<UI::Button>("Undo");
            _btn_undo->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(48.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            _btn_undo->OnMouseClick() += [this](UI::UIEvent& e) { if (g_pCommandMgr) g_pCommandMgr->Undo(); e._is_handled = true; };

            _btn_redo = toolbar->AddChild<UI::Button>("Redo");
            _btn_redo->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(48.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 8.0f, 0.0f));
            _btn_redo->OnMouseClick() += [this](UI::UIEvent& e) { if (g_pCommandMgr) g_pCommandMgr->Redo(); e._is_handled = true; };

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
            _chk_grid->OnMouseClick() += [this](UI::UIEvent& e) { _show_grid = _chk_grid->IsChecked(); RefreshPreview(); e._is_handled = true; };
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
            _btn_1to1->OnMouseClick() += [this](UI::UIEvent& e) { _preview_zoom = 1.0f; RefreshPreview(); e._is_handled = true; };

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
                                ApplyEditData(data);
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
                    ApplyEditData(data);
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

            auto on_norm = [this](f32& f) { return [this, &f](String v) { if (_is_syncing_uv) return; f = std::clamp((f32)std::atof(v.c_str()), 0.0f, 1.0f); ValidateEditingData(); RefreshUvInputs(); RefreshPreview(); }; };
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

            auto on_pix = [this](bool is_x) { return [this, is_x](String v) { if (_is_syncing_uv || !_texture) return; f32 p = (f32)std::atof(v.c_str()); f32 s = (f32)(is_x ? _texture->Width() : _texture->Height()); if (is_x) _editing._uv_rect.x = p / (f32)_texture->Width(); else _editing._uv_rect.y = p / (f32)_texture->Height(); ValidateEditingData(); RefreshUvInputs(); RefreshPreview(); }; };
            _uv_pix_x->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.x = (f32)std::atof(v.c_str()) / (f32)_texture->Width(); ValidateEditingData(); RefreshUvInputs(); RefreshPreview(); };
            _uv_pix_y->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.y = (f32)std::atof(v.c_str()) / (f32)_texture->Height(); ValidateEditingData(); RefreshUvInputs(); RefreshPreview(); };
            _uv_pix_w->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.z = (f32)std::atof(v.c_str()) / (f32)_texture->Width(); ValidateEditingData(); RefreshUvInputs(); RefreshPreview(); };
            _uv_pix_h->_on_content_changed += [this](String v) { if (_is_syncing_uv || !_texture) return; _editing._uv_rect.w = (f32)std::atof(v.c_str()) / (f32)_texture->Height(); ValidateEditingData(); RefreshUvInputs(); RefreshPreview(); };

            auto* br = parent->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 4.0f, 4.0f, 0.0f));

            auto* bf = br->AddChild<UI::Button>("Full Tex");
            bf->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(60.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            bf->OnMouseClick() += [this](UI::UIEvent& e) { _editing._uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f); ValidateEditingData(); RefreshAllUI(); RefreshPreview(); e._is_handled = true; };

            auto* bt = br->AddChild<UI::Button>("Trim");
            bt->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(40.0f, 0.0f));
            bt->OnMouseClick() += [this](UI::UIEvent& e) { LOG_INFO("Trim not implemented"); e._is_handled = true; };
        }

        void SpriteAssetEditor::BuildPivotSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Pivot");

            _pivot_x = AddLabeledInput(parent, "X", _editing._pivot.x);
            _pivot_y = AddLabeledInput(parent, "Y", _editing._pivot.y);

            _pivot_x->_on_content_changed += [this](String v) { if (_is_syncing_pivot) return; _editing._pivot.x = std::clamp((f32)std::atof(v.c_str()), 0.0f, 1.0f); ValidateEditingData(); RefreshPivotInputs(); RefreshPreview(); };
            _pivot_y->_on_content_changed += [this](String v) { if (_is_syncing_pivot) return; _editing._pivot.y = std::clamp((f32)std::atof(v.c_str()), 0.0f, 1.0f); ValidateEditingData(); RefreshPivotInputs(); RefreshPreview(); };

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
                    btn->OnMouseClick() += [this, px=p.x, py=p.y](UI::UIEvent& e) { _editing._pivot = Vector2f(px, py); RefreshPivotInputs(); RefreshPreview(); e._is_handled = true; };
                }
            }
        }

        void SpriteAssetEditor::BuildSizeSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Size");

            _size_w = AddLabeledInput(parent, "W", _editing._size.x);
            _size_h = AddLabeledInput(parent, "H", _editing._size.y);

            _size_w->_on_content_changed += [this](String v) { if (_is_syncing_size) return; f32 val = std::max((f32)std::atof(v.c_str()), 0.0001f); _editing._size.x = val; if (_lock_ratio && _locked_aspect > 0) _editing._size.y = val / _locked_aspect; ValidateEditingData(); RefreshSizeInputs(); };
            _size_h->_on_content_changed += [this](String v) { if (_is_syncing_size) return; f32 val = std::max((f32)std::atof(v.c_str()), 0.0001f); _editing._size.y = val; if (_lock_ratio && _locked_aspect > 0) _editing._size.x = val * _locked_aspect; ValidateEditingData(); RefreshSizeInputs(); };

            auto* lr = parent->AddChild<UI::HorizontalBox>();
            lr->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 20.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 0.0f));
            _chk_lock_ratio = lr->AddChild<UI::CheckBox>();
            _chk_lock_ratio->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(18.0f, 0.0f));
            _chk_lock_ratio->OnMouseClick() += [this](UI::UIEvent& e) { _lock_ratio = _chk_lock_ratio->IsChecked(); if (_lock_ratio && _editing._size.y > 0) _locked_aspect = _editing._size.x / _editing._size.y; e._is_handled = true; };
            auto* ll = lr->AddChild<UI::Text>("Lock Ratio"); ll->_color = Color(0.65f, 0.65f, 0.65f, 1.0f);
            ll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto* br = parent->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 0.0f));

            auto* bu = br->AddChild<UI::Button>("From UV");
            bu->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(60.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            bu->OnMouseClick() += [this](UI::UIEvent& e) { if (_texture) { f32 sw = std::round(_editing._uv_rect.z*(f32)_texture->Width()); f32 sh = std::round(_editing._uv_rect.w*(f32)_texture->Height()); _editing._size = Vector2f(sw, sh); RefreshSizeInputs(); } e._is_handled = true; };

            auto* bz = br->AddChild<UI::Button>("Reset");
            bz->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f));
            bz->OnMouseClick() += [this](UI::UIEvent& e) { _editing._size = Vector2f::kOne; RefreshSizeInputs(); e._is_handled = true; };
        }

        void SpriteAssetEditor::BuildBorderSection(UI::VerticalBox* parent)
        {
            AddSectionTitle(parent, "Border");

            _border_l = AddLabeledInput(parent, "L", _editing._border.y);
            _border_r = AddLabeledInput(parent, "R", _editing._border.x);
            _border_t = AddLabeledInput(parent, "T", _editing._border.w);
            _border_b = AddLabeledInput(parent, "B", _editing._border.z);

            auto on_b = [this](f32& f) { return [this, &f](String v) { if (_is_syncing_border) return; f = std::max((f32)std::atof(v.c_str()), 0.0f); ValidateEditingData(); RefreshBorderInputs(); RefreshPreview(); }; };
            _border_l->_on_content_changed += on_b(_editing._border.y);
            _border_r->_on_content_changed += on_b(_editing._border.x);
            _border_t->_on_content_changed += on_b(_editing._border.w);
            _border_b->_on_content_changed += on_b(_editing._border.z);

            auto* br = parent->AddChild<UI::HorizontalBox>();
            br->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(4.0f, 4.0f, 4.0f, 0.0f));

            auto* bz = br->AddChild<UI::Button>("Reset");
            bz->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            bz->OnMouseClick() += [this](UI::UIEvent& e) { _editing._border = Vector4f::kZero; RefreshBorderInputs(); RefreshPreview(); e._is_handled = true; };

            auto* be = br->AddChild<UI::Button>("Equal");
            be->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(44.0f, 0.0f));
            be->OnMouseClick() += [this](UI::UIEvent& e) { if (_texture) { f32 sw = std::round(_editing._uv_rect.z*(f32)_texture->Width()); f32 sh = std::round(_editing._uv_rect.w*(f32)_texture->Height()); f32 b = std::round(std::min(sw,sh)*0.1f); _editing._border = Vector4f(b,b,b,b); RefreshBorderInputs(); RefreshPreview(); } e._is_handled = true; };
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
                f32 old_z = _editor->_preview_zoom;
                if (e._scroll_delta > 0) _editor->_preview_zoom = std::min(_editor->_preview_zoom * kZoomStep, kZoomMax);
                else if (e._scroll_delta < 0) _editor->_preview_zoom = std::max(_editor->_preview_zoom / kZoomStep, kZoomMin);
                if (_editor->_preview_zoom != old_z) {
                    auto c = GetContentRect();
                    Vector2f ct = Vector2f(c.x + c.z*.5f, c.y + c.w*.5f);
                    f32 r = _editor->_preview_zoom / old_z;
                    _editor->_preview_pan.x = e._mouse_position.x - ct.x - (e._mouse_position.x - ct.x - _editor->_preview_pan.x) * r;
                    _editor->_preview_pan.y = e._mouse_position.y - ct.y - (e._mouse_position.y - ct.y - _editor->_preview_pan.y) * r;
                }
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
            DrawBackground(r, cr);
            if (_editor && _editor->_texture) {
                DrawTexture(r, cr);
                if (_editor->_show_border) DrawBorderOverlay(r);
                if (_editor->_show_pivot) DrawPivotOverlay(r);
                DrawUvRectOverlay(r);
                if (_editor->_show_grid && _editor->_preview_zoom >= 4.0f) DrawPixelGrid(r);
            }
            r.PopScissor();
        }

        void SpritePreviewWidget::DrawBackground(UI::UIRenderer& r, const Vector4f& rect)
        {
            if (!_editor) return;
            UIBrush b; b._type = EUIBrushType::kColor;
            switch (_editor->_background_mode) {
            case 0: { i32 tx=(i32)std::ceil(rect.z/kChessTileSize)+1, ty=(i32)std::ceil(rect.w/kChessTileSize)+1; for(i32 y=0;y<ty;++y) for(i32 x=0;x<tx;++x){b._tint=((x+y)&1)?kColorChessB:kColorChessA; r.DrawQuad(Vector4f(rect.x+x*kChessTileSize,rect.y+y*kChessTileSize,kChessTileSize,kChessTileSize),b,-0.1f);} break; }
            case 1: b._tint=Color(0.1f,0.1f,0.1f,1.0f); r.DrawQuad(rect,b,-0.1f); break;
            case 2: b._tint=Color(0.8f,0.8f,0.8f,1.0f); r.DrawQuad(rect,b,-0.1f); break;
            case 3: b._tint=Colors::kBlack; r.DrawQuad(rect,b,-0.1f); break;
            case 4: b._tint=Colors::kWhite; r.DrawQuad(rect,b,-0.1f); break;
            default: break;
            }
        }

        void SpritePreviewWidget::DrawTexture(UI::UIRenderer& r, const Vector4f& rect)
        {
            auto* tex = _editor->_texture; if(!tex)return;
            f32 tw=(f32)tex->Width(), th=(f32)tex->Height(); if(tw<=0||th<=0)return;
            f32 z=_editor->_preview_zoom; Vector2f p=_editor->_preview_pan;
            f32 cx=rect.x+rect.z*.5f, cy=rect.y+rect.w*.5f;
            // Compute screen-space draw rect directly (avoid matrix construction issues)
            f32 dw=tw*z, dh=th*z;
            f32 dx=cx+p.x-dw*.5f, dy=cy+p.y-dh*.5f;
            ImageDrawOptions o;
            o._uv_rect=Vector4f(0,0,1,1);
            o._tint=_editor->_show_alpha?Colors::kWhite:Color(1,1,1,1);
            r.DrawImage(tex, Vector4f(dx, dy, dw, dh), o);
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

        void SpritePreviewWidget::DrawPixelGrid(UI::UIRenderer& r)
        {
            if(!_editor||!_editor->_texture)return;
            auto tr=GetTextureDisplayRect(); f32 tw=(f32)_editor->_texture->Width(),th=(f32)_editor->_texture->Height(),z=_editor->_preview_zoom;
            if(z<4.0f)return; auto cr=GetContentRect();
            f32 sx=std::max(0.0f,(cr.x-tr.x)/z),sy=std::max(0.0f,(cr.y-tr.y)/z),ex=std::min(tw,(cr.x+cr.z-tr.x)/z),ey=std::min(th,(cr.y+cr.w-tr.y)/z);
            Color gc(1,1,1,0.15f);
            for(f32 px=std::floor(sx);px<=ex;px+=1){Vector2f sp=TexturePixelToScreen(Vector2f(px,0));r.DrawLine(Vector2f(sp.x,cr.y),Vector2f(sp.x,cr.y+cr.w),0.5f,gc,0.8f);}
            for(f32 py=std::floor(sy);py<=ey;py+=1){Vector2f sp=TexturePixelToScreen(Vector2f(0,py));r.DrawLine(Vector2f(cr.x,sp.y),Vector2f(cr.x+cr.z,sp.y),0.5f,gc,0.8f);}
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
            f32 ux=uv.x*tw,uy=uv.y*th,uw=uv.z*tw,uh=uv.w*th,hr=kHandleRadius/std::max(_editor->_preview_zoom,0.1f);
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

        Vector2f SpritePreviewWidget::ScreenToPreview(const Vector2f& sp) const {
            auto c=GetContentRect(); f32 z=_editor?_editor->_preview_zoom:1.0f; Vector2f p=_editor?_editor->_preview_pan:Vector2f::kZero;
            return Vector2f((sp.x-c.x-c.z*.5f-p.x)/z,(sp.y-c.y-c.w*.5f-p.y)/z);
        }
        Vector2f SpritePreviewWidget::PreviewToScreen(const Vector2f& pp) const {
            auto c=GetContentRect(); f32 z=_editor?_editor->_preview_zoom:1.0f; Vector2f p=_editor?_editor->_preview_pan:Vector2f::kZero;
            return Vector2f(c.x+c.z*.5f+p.x+pp.x*z,c.y+c.w*.5f+p.y+pp.y*z);
        }
        Vector2f SpritePreviewWidget::ScreenToTexturePixel(const Vector2f& sp) const {
            auto c=GetContentRect(); f32 z=_editor?_editor->_preview_zoom:1.0f; Vector2f p=_editor?_editor->_preview_pan:Vector2f::kZero;
            f32 tw=_editor&&_editor->_texture?(f32)_editor->_texture->Width():256.0f,th=_editor&&_editor->_texture?(f32)_editor->_texture->Height():256.0f;
            f32 px=(sp.x-c.x-c.z*.5f-p.x)/z,py=(sp.y-c.y-c.w*.5f-p.y)/z;
            return Vector2f(px+tw*.5f,py+th*.5f);
        }
        Vector2f SpritePreviewWidget::TexturePixelToScreen(const Vector2f& tp) const {
            auto c=GetContentRect(); f32 z=_editor?_editor->_preview_zoom:1.0f; Vector2f p=_editor?_editor->_preview_pan:Vector2f::kZero;
            f32 tw=_editor&&_editor->_texture?(f32)_editor->_texture->Width():256.0f,th=_editor&&_editor->_texture?(f32)_editor->_texture->Height():256.0f;
            f32 px=tp.x-tw*.5f,py=tp.y-th*.5f;
            return Vector2f(c.x+c.z*.5f+p.x+px*z,c.y+c.w*.5f+p.y+py*z);
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
        Vector4f SpritePreviewWidget::GetTextureDisplayRect() const {
            if(!_editor||!_editor->_texture)return Vector4f(0,0,0,0);
            Vector2f tl=TexturePixelToScreen(Vector2f(0,0)),br=TexturePixelToScreen(Vector2f((f32)_editor->_texture->Width(),(f32)_editor->_texture->Height()));
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
            case SpriteEditorDragMode::kPanView:{Vector2f sp=ScreenToPreview(_editor->_drag_start_mouse),cp=ScreenToPreview(mp);_editor->_preview_pan=_editor->_preview_pan+(cp-sp)*_editor->_preview_zoom;_editor->_drag_start_mouse=mp;break;}
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
