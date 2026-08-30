#pragma once
#ifndef __TEXTURE_PREVIEW_WIDGET_H__
#define __TEXTURE_PREVIEW_WIDGET_H__

#include "Framework/Math/ALMath.hpp"
#include "UI/UIElement.h"

#include <functional>

namespace Ailu
{
    namespace Render
    {
        class Texture;
    }

    namespace UI
    {
        class UIRenderer;
    }

    namespace Editor
    {
        class TexturePreviewWidget : public UI::UIElement
        {
        public:
            using OverlayCallback = std::function<void(UI::UIRenderer &, const Vector4f &)>;

            TexturePreviewWidget();
            Vector2f MeasureDesiredSize() override;

            void SetTexture(Render::Texture *texture);
            void SetMip(u32 mip);
            void SetChannelMask(const Vector4f &mask);
            void SetShowCheckerboard(bool show);
            void SetShowPixelGrid(bool show);
            void SetOverlayCallback(OverlayCallback callback);
            void SetNavigationEnabled(bool enabled);

            void Fit();
            void ResetZoom();
            void SetZoom(f32 zoom);
            void ZoomAt(const Vector2f &screen_pos, f32 factor);
            void SetPan(const Vector2f &pan) { _pan = pan; }

            Render::Texture *GetTexture() const { return _texture; }
            u32 GetMip() const { return _mip; }
            const Vector4f &GetChannelMask() const { return _channel_mask; }
            f32 GetZoom() const { return _zoom; }
            const Vector2f &GetPan() const { return _pan; }
            Vector4f GetTextureDisplayRect() const;
            Vector2f ScreenToPreview(const Vector2f &screen_pos) const;
            Vector2f PreviewToScreen(const Vector2f &preview_pos) const;
            Vector2f ScreenToTexturePixel(const Vector2f &screen_pos) const;
            Vector2f TexturePixelToScreen(const Vector2f &texture_pixel) const;

        protected:
            void RenderImpl(UI::UIRenderer &renderer) override;
            virtual void DrawOverlay(UI::UIRenderer &renderer, const Vector4f &texture_rect);
            void DrawBackground(UI::UIRenderer &renderer, const Vector4f &rect) const;
            void DrawTexture(UI::UIRenderer &renderer, const Vector4f &rect) const;
            void DrawPixelGrid(UI::UIRenderer &renderer, const Vector4f &rect) const;

        private:
            void HandleMouseDown(UI::UIEvent &event);
            void HandleMouseUp(UI::UIEvent &event);
            void HandleMouseMove(UI::UIEvent &event);
            void HandleMouseScroll(UI::UIEvent &event);

            static constexpr f32 kZoomMin = 0.1f;
            static constexpr f32 kZoomMax = 32.0f;
            static constexpr f32 kZoomStep = 1.1f;
            static constexpr f32 kCheckerTileSize = 16.0f;

            Render::Texture *_texture = nullptr;
            OverlayCallback _overlay_callback;
            Vector4f _channel_mask = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
            Vector2f _pan = Vector2f::kZero;
            f32 _zoom = 1.0f;
            u32 _mip = 0u;
            bool _show_checkerboard = true;
            bool _show_pixel_grid = true;
            bool _navigation_enabled = true;
            bool _is_dragging = false;
            Vector2f _last_mouse_position = Vector2f::kZero;
        };
    }
}

#endif // __TEXTURE_PREVIEW_WIDGET_H__
