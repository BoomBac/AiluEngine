#pragma once
#ifndef __SPRITE_ASSET_EDITOR_H__
#define __SPRITE_ASSET_EDITOR_H__

#include "Dock/DockWindow.h"
#include "UI/UIElement.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "Render/Texture.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Guid.h"

namespace Ailu
{
    namespace UI
    {
        class Image;
        class Text;
        class Button;
        class CheckBox;
        class InputBlock;
        class VerticalBox;
        class HorizontalBox;
        class ScrollView;
    }

    namespace Render 
    {
        class Sprite;
    }

    namespace Editor
    {
        // =========================================================================
        // SpriteAssetEditData
        // =========================================================================
        struct SpriteAssetEditData
        {
            Guid     _texture = Guid::EmptyGuid();
            Vector4f _uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
            Vector2f _pivot   = Vector2f(0.5f, 0.5f);
            f32      _size    = 1.0f;
            Vector4f _border  = Vector4f::kZero;

            bool operator==(const SpriteAssetEditData& other) const
            {
                return _texture == other._texture
                    && _uv_rect == other._uv_rect
                    && _pivot   == other._pivot
                    && _size    == other._size
                    && _border  == other._border;
            }
            bool operator!=(const SpriteAssetEditData& other) const { return !(*this == other); }
        };

        // =========================================================================
        // SpriteEditorDragMode
        // =========================================================================
        enum class SpriteEditorDragMode : u8
        {
            kNone,
            kPanView,
            kMoveUvRect,
            kResizeUvLeft,
            kResizeUvRight,
            kResizeUvTop,
            kResizeUvBottom,
            kResizeUvTopLeft,
            kResizeUvTopRight,
            kResizeUvBottomLeft,
            kResizeUvBottomRight,
            kMovePivot,
            kMoveBorderLeft,
            kMoveBorderRight,
            kMoveBorderTop,
            kMoveBorderBottom
        };

        // =========================================================================
        // SpriteAssetEditor
        // =========================================================================
        class SpritePreviewWidget;
        class SpriteAssetEditCommand;

        class SpriteAssetEditor : public DockWindow
        {
            friend class SpritePreviewWidget;
            friend class SpriteAssetEditCommand;

        public:
            SpriteAssetEditor();
            ~SpriteAssetEditor() override;

            void Update(f32 dt) override;
            void Open(Render::Sprite* asset);
            void Close();

            bool IsDirty() const { return _editing != _original; }
            void ApplyEditData(const SpriteAssetEditData& data);

        private:
            void FitTexture();
            void BuildToolbar(UI::HorizontalBox* toolbar);
            void BuildLeftPanel(UI::VerticalBox* left);
            void BuildCenterPanel(UI::Border* center);
            void BuildRightPanel(UI::VerticalBox* right);
            void BuildStatusBar(UI::HorizontalBox* status_bar);
            void BuildTextureSection(UI::VerticalBox* parent);
            void BuildUvRectSection(UI::VerticalBox* parent);
            void BuildPivotSection(UI::VerticalBox* parent);
            void BuildSizeSection(UI::VerticalBox* parent);
            void BuildBorderSection(UI::VerticalBox* parent);

            void ReadFromAsset();
            void WriteToAsset();
            void Apply();
            void Revert();
            void ValidateEditingData();
            void RefreshAllUI();
            void RefreshAssetInfo();
            void RefreshUvInputs();
            void RefreshPivotInputs();
            void RefreshSizeInputs();
            void RefreshBorderInputs();
            void RefreshStatusBar();
            void RefreshPreview();

            Render::Texture2D* ResolveTexture();
            void OnTextureChanged();

            Vector2f UVToTexturePixel(const Vector2f& uv) const;
            Vector2f TexturePixelToUV(const Vector2f& pixel) const;

            static UI::InputBlock* AddLabeledInput(UI::UIElement* parent, const String& label, f32 value, f32 width = 80.0f);
            static UI::Text* AddSectionTitle(UI::UIElement* parent, const String& title);
            static UI::HorizontalBox* AddPropertyRow(UI::UIElement* parent, const String& label);

        private:
            Render::Sprite *_sprite_asset = nullptr;
            Render::Texture2D* _texture = nullptr;
            SpriteAssetEditData _original;
            SpriteAssetEditData _editing;

            Vector2f _preview_pan  = Vector2f(0.0f, 0.0f);
            f32      _preview_zoom = 1.0f;
            bool     _show_grid    = true;
            bool     _show_pivot   = true;
            bool     _show_border  = true;
            bool     _snap_to_pixel = true;
            bool     _show_alpha   = true;
            int      _background_mode = 0;

            SpriteEditorDragMode _drag_mode = SpriteEditorDragMode::kNone;
            Vector2f             _drag_start_mouse = Vector2f::kZero;
            SpriteAssetEditData  _drag_start_data;

            bool _is_syncing_uv = false;
            bool _is_syncing_pivot = false;
            bool _is_syncing_size = false;
            bool _is_syncing_border = false;

            UI::Button*   _btn_apply = nullptr;
            UI::Button*   _btn_revert = nullptr;
            UI::Button*   _btn_undo = nullptr;
            UI::Button*   _btn_redo = nullptr;
            UI::CheckBox* _chk_snap = nullptr;
            UI::CheckBox* _chk_grid = nullptr;
            UI::CheckBox* _chk_pivot = nullptr;
            UI::CheckBox* _chk_border = nullptr;
            UI::Text*     _txt_zoom_label = nullptr;
            UI::Button*   _btn_fit = nullptr;
            UI::Button*   _btn_1to1 = nullptr;

            SpritePreviewWidget* _preview = nullptr;

            UI::Text* _txt_asset_name = nullptr;
            UI::Text* _txt_asset_path = nullptr;
            UI::Text* _txt_asset_guid = nullptr;
            UI::Text* _txt_asset_type = nullptr;
            UI::Text* _txt_tex_name = nullptr;
            UI::Text* _txt_tex_resolution = nullptr;
            UI::Text* _txt_tex_format = nullptr;
            UI::Text* _txt_sprite_size = nullptr;

            UI::Image*     _img_tex_preview = nullptr;
            UI::Text*      _txt_tex_field = nullptr;
            UI::Button*    _btn_select_tex = nullptr;
            UI::Button*    _btn_clear_tex = nullptr;

            UI::InputBlock* _uv_norm_x = nullptr;
            UI::InputBlock* _uv_norm_y = nullptr;
            UI::InputBlock* _uv_norm_w = nullptr;
            UI::InputBlock* _uv_norm_h = nullptr;
            UI::InputBlock* _uv_pix_x = nullptr;
            UI::InputBlock* _uv_pix_y = nullptr;
            UI::InputBlock* _uv_pix_w = nullptr;
            UI::InputBlock* _uv_pix_h = nullptr;

            UI::InputBlock* _pivot_x = nullptr;
            UI::InputBlock* _pivot_y = nullptr;

            UI::InputBlock* _size_input = nullptr;

            UI::InputBlock* _border_l = nullptr;
            UI::InputBlock* _border_r = nullptr;
            UI::InputBlock* _border_t = nullptr;
            UI::InputBlock* _border_b = nullptr;

            UI::Text* _status_text = nullptr;
        };

        // =========================================================================
        // SpritePreviewWidget
        // =========================================================================
        class SpritePreviewWidget : public UI::UIElement
        {
        public:
            explicit SpritePreviewWidget(SpriteAssetEditor* editor);
            Vector2f MeasureDesiredSize() override;

        protected:
            void RenderImpl(UI::UIRenderer& r) override;
            void Update(f32 dt) override;

        private:
            void DrawBackground(UI::UIRenderer& r, const Vector4f& rect);
            void DrawTexture(UI::UIRenderer& r, const Vector4f& rect);
            void DrawUvRectOverlay(UI::UIRenderer& r);
            void DrawPixelGrid(UI::UIRenderer& r);
            void DrawPivotOverlay(UI::UIRenderer& r);
            void DrawBorderOverlay(UI::UIRenderer& r);

            SpriteEditorDragMode HitTest(const Vector2f& screen_pos) const;

            Vector2f ScreenToPreview(const Vector2f& screen_pos) const;
            Vector2f PreviewToScreen(const Vector2f& preview_pos) const;
            Vector2f ScreenToTexturePixel(const Vector2f& screen_pos) const;
            Vector2f TexturePixelToScreen(const Vector2f& tex_pixel) const;

            Vector4f GetUvRectInPixels() const;
            Vector4f GetUvRectInScreen() const;
            Vector4f GetTextureDisplayRect() const;

            static constexpr f32 kHandleRadius = 6.0f;
            Vector<Vector4f> GetUvHandles() const;

            void ProcessDrag(const Vector2f& mouse_pos);

        private:
            SpriteAssetEditor* _editor = nullptr;
        };

    } // namespace Editor
} // namespace Ailu

#endif // __SPRITE_ASSET_EDITOR_H__
