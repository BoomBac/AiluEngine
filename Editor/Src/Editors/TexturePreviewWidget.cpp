#include "Editors/TexturePreviewWidget.h"

#include "Framework/Common/Input.h"
#include "Render/Texture.h"
#include "UI/Basic.h"
#include "UI/Style/UIStyleBasic.h"
#include "UI/UIRenderer.h"

#include <algorithm>
#include <cmath>

namespace Ailu::Editor
{
    namespace
    {
        const Color kCheckerColorA = Color(0.75f, 0.75f, 0.75f, 1.0f);
        const Color kCheckerColorB = Color(0.55f, 0.55f, 0.55f, 1.0f);
        const Color kPreviewBackground = Color(0.10f, 0.11f, 0.12f, 1.0f);
        const Color kPixelGridColor = Color(1.0f, 1.0f, 1.0f, 0.15f);
    }

    TexturePreviewWidget::TexturePreviewWidget()
    {
        SetWantsMouseEvents(true);
        SetInteractiveEnabled(true);
        OnMouseDown() += [this](UI::UIEvent &event) { HandleMouseDown(event); };
        OnMouseUp() += [this](UI::UIEvent &event) { HandleMouseUp(event); };
        OnMouseMove() += [this](UI::UIEvent &event) { HandleMouseMove(event); };
        OnMouseScroll() += [this](UI::UIEvent &event) { HandleMouseScroll(event); };
    }

    Vector2f TexturePreviewWidget::MeasureDesiredSize()
    {
        return Vector2f(400.0f, 300.0f);
    }

    void TexturePreviewWidget::SetTexture(Render::Texture *texture)
    {
        if (_texture == texture)
            return;
        _texture = texture;
        _mip = 0u;
        Fit();
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetMip(u32 mip)
    {
        _mip = _texture == nullptr ? 0u : std::min(mip, static_cast<u32>(_texture->MipmapLevel() - 1u));
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetChannelMask(const Vector4f &mask)
    {
        _channel_mask = mask;
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetShowCheckerboard(bool show)
    {
        _show_checkerboard = show;
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetShowPixelGrid(bool show)
    {
        _show_pixel_grid = show;
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetOverlayCallback(OverlayCallback callback)
    {
        _overlay_callback = std::move(callback);
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetNavigationEnabled(bool enabled)
    {
        _navigation_enabled = enabled;
        _is_dragging = false;
    }

    void TexturePreviewWidget::Fit()
    {
        if (_texture == nullptr || _texture->Width() == 0u || _texture->Height() == 0u)
            return;
        const Vector4f rect = GetContentRect();
        if (rect.z <= 0.0f || rect.w <= 0.0f)
            return;
        _zoom = std::clamp(std::min(rect.z / static_cast<f32>(_texture->Width()),
                                    rect.w / static_cast<f32>(_texture->Height())), kZoomMin, kZoomMax);
        _pan = Vector2f::kZero;
        InvalidatePaint();
    }

    void TexturePreviewWidget::ResetZoom()
    {
        _zoom = 1.0f;
        _pan = Vector2f::kZero;
        InvalidatePaint();
    }

    void TexturePreviewWidget::SetZoom(f32 zoom)
    {
        _zoom = std::clamp(zoom, kZoomMin, kZoomMax);
        InvalidatePaint();
    }

    void TexturePreviewWidget::ZoomAt(const Vector2f &screen_pos, f32 factor)
    {
        const f32 old_zoom = _zoom;
        SetZoom(old_zoom * factor);
        if (old_zoom <= 0.0f || _zoom == old_zoom)
            return;
        const Vector4f rect = GetContentRect();
        const Vector2f center(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
        const f32 ratio = _zoom / old_zoom;
        _pan.x = screen_pos.x - center.x - (screen_pos.x - center.x - _pan.x) * ratio;
        _pan.y = screen_pos.y - center.y - (screen_pos.y - center.y - _pan.y) * ratio;
        InvalidatePaint();
    }

    Vector4f TexturePreviewWidget::GetTextureDisplayRect() const
    {
        if (_texture == nullptr)
            return Vector4f::kZero;
        const Vector2f top_left = TexturePixelToScreen(Vector2f::kZero);
        const Vector2f bottom_right = TexturePixelToScreen(Vector2f(static_cast<f32>(_texture->Width()),
                                                                      static_cast<f32>(_texture->Height())));
        return Vector4f(std::min(top_left.x, bottom_right.x), std::min(top_left.y, bottom_right.y),
                        std::abs(bottom_right.x - top_left.x), std::abs(bottom_right.y - top_left.y));
    }

    Vector2f TexturePreviewWidget::ScreenToPreview(const Vector2f &screen_pos) const
    {
        const Vector4f rect = GetContentRect();
        return Vector2f((screen_pos.x - rect.x - rect.z * 0.5f - _pan.x) / _zoom,
                        (screen_pos.y - rect.y - rect.w * 0.5f - _pan.y) / _zoom);
    }

    Vector2f TexturePreviewWidget::PreviewToScreen(const Vector2f &preview_pos) const
    {
        const Vector4f rect = GetContentRect();
        return Vector2f(rect.x + rect.z * 0.5f + _pan.x + preview_pos.x * _zoom,
                        rect.y + rect.w * 0.5f + _pan.y + preview_pos.y * _zoom);
    }

    Vector2f TexturePreviewWidget::ScreenToTexturePixel(const Vector2f &screen_pos) const
    {
        const f32 width = _texture != nullptr ? static_cast<f32>(_texture->Width()) : 256.0f;
        const f32 height = _texture != nullptr ? static_cast<f32>(_texture->Height()) : 256.0f;
        const Vector2f preview = ScreenToPreview(screen_pos);
        return Vector2f(preview.x + width * 0.5f, preview.y + height * 0.5f);
    }

    Vector2f TexturePreviewWidget::TexturePixelToScreen(const Vector2f &texture_pixel) const
    {
        const f32 width = _texture != nullptr ? static_cast<f32>(_texture->Width()) : 256.0f;
        const f32 height = _texture != nullptr ? static_cast<f32>(_texture->Height()) : 256.0f;
        return PreviewToScreen(Vector2f(texture_pixel.x - width * 0.5f, texture_pixel.y - height * 0.5f));
    }

    void TexturePreviewWidget::RenderImpl(UI::UIRenderer &renderer)
    {
        const Vector4f rect = GetContentRect();
        if (rect.z <= 0.0f || rect.w <= 0.0f)
            return;
        renderer.PushScissor(rect);
        DrawBackground(renderer, rect);
        if (_texture != nullptr)
        {
            DrawTexture(renderer, rect);
            if (_show_pixel_grid && _zoom >= 4.0f)
                DrawPixelGrid(renderer, rect);
            const Vector4f texture_rect = GetTextureDisplayRect();
            DrawOverlay(renderer, texture_rect);
            if (_overlay_callback)
                _overlay_callback(renderer, texture_rect);
        }
        renderer.PopScissor();
    }

    void TexturePreviewWidget::DrawOverlay(UI::UIRenderer &, const Vector4f &)
    {
    }

    void TexturePreviewWidget::DrawBackground(UI::UIRenderer &renderer, const Vector4f &rect) const
    {
        UI::UIBrush brush;
        brush._type = UI::EUIBrushType::kColor;
        if (!_show_checkerboard)
        {
            brush._tint = kPreviewBackground;
            renderer.DrawQuad(rect, brush, -0.1f);
            return;
        }
        const i32 tile_x = static_cast<i32>(std::ceil(rect.z / kCheckerTileSize)) + 1;
        const i32 tile_y = static_cast<i32>(std::ceil(rect.w / kCheckerTileSize)) + 1;
        for (i32 y = 0; y < tile_y; ++y)
        {
            for (i32 x = 0; x < tile_x; ++x)
            {
                brush._tint = ((x + y) & 1) != 0 ? kCheckerColorB : kCheckerColorA;
                renderer.DrawQuad(Vector4f(rect.x + x * kCheckerTileSize, rect.y + y * kCheckerTileSize,
                                           kCheckerTileSize, kCheckerTileSize), brush, -0.1f);
            }
        }
    }

    void TexturePreviewWidget::DrawTexture(UI::UIRenderer &renderer, const Vector4f &) const
    {
        if (_texture == nullptr || _texture->Width() == 0u || _texture->Height() == 0u)
            return;
        const Vector4f rect = GetTextureDisplayRect();
        UI::ImageDrawOptions options;
        options._uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
        options._tint = Color(_channel_mask.x, _channel_mask.y, _channel_mask.z, _channel_mask.w);
        renderer.DrawImage(_texture, rect, options);
    }

    void TexturePreviewWidget::DrawPixelGrid(UI::UIRenderer &renderer, const Vector4f &rect) const
    {
        if (_texture == nullptr)
            return;
        const Vector4f texture_rect = GetTextureDisplayRect();
        const f32 width = static_cast<f32>(_texture->Width());
        const f32 height = static_cast<f32>(_texture->Height());
        const f32 start_x = std::max(0.0f, (rect.x - texture_rect.x) / _zoom);
        const f32 start_y = std::max(0.0f, (rect.y - texture_rect.y) / _zoom);
        const f32 end_x = std::min(width, (rect.x + rect.z - texture_rect.x) / _zoom);
        const f32 end_y = std::min(height, (rect.y + rect.w - texture_rect.y) / _zoom);
        for (f32 x = std::floor(start_x); x <= end_x; x += 1.0f)
        {
            const f32 screen_x = TexturePixelToScreen(Vector2f(x, 0.0f)).x;
            renderer.DrawLine(Vector2f(screen_x, rect.y), Vector2f(screen_x, rect.y + rect.w), 0.5f,
                              kPixelGridColor, 0.8f);
        }
        for (f32 y = std::floor(start_y); y <= end_y; y += 1.0f)
        {
            const f32 screen_y = TexturePixelToScreen(Vector2f(0.0f, y)).y;
            renderer.DrawLine(Vector2f(rect.x, screen_y), Vector2f(rect.x + rect.z, screen_y), 0.5f,
                              kPixelGridColor, 0.8f);
        }
    }

    void TexturePreviewWidget::HandleMouseDown(UI::UIEvent &event)
    {
        if (!_navigation_enabled || _texture == nullptr || event._key_code != EKey::kLBUTTON)
            return;
        _is_dragging = true;
        _last_mouse_position = event._mouse_position;
        event._is_handled = true;
    }

    void TexturePreviewWidget::HandleMouseUp(UI::UIEvent &event)
    {
        if (!_navigation_enabled || !_is_dragging || event._key_code != EKey::kLBUTTON)
            return;
        _is_dragging = false;
        event._is_handled = true;
    }

    void TexturePreviewWidget::HandleMouseMove(UI::UIEvent &event)
    {
        if (!_navigation_enabled || !_is_dragging)
            return;
        _pan += event._mouse_position - _last_mouse_position;
        _last_mouse_position = event._mouse_position;
        InvalidatePaint();
        event._is_handled = true;
    }

    void TexturePreviewWidget::HandleMouseScroll(UI::UIEvent &event)
    {
        if (!_navigation_enabled || _texture == nullptr)
            return;
        if (event._scroll_delta > 0)
            ZoomAt(event._mouse_position, kZoomStep);
        else if (event._scroll_delta < 0)
            ZoomAt(event._mouse_position, 1.0f / kZoomStep);
        event._is_handled = true;
    }
}
