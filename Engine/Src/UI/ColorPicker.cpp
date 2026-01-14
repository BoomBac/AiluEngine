#include "UI/ColorPicker.h"
#include "Framework/Common/Input.h"
#include "UI/TextRenderer.h"
#include "UI/UIRenderer.h"
#include "pch.h"

namespace Ailu
{
    namespace UI
    {
        using Render::Texture2D;
        using Render::TextureDesc;

        static constexpr u16 SV_RES = 256;
        static constexpr u16 HUE_RES = 256;
        static constexpr u16 CHECK_RES = 8;

        static u8 toByte(float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<u8>(v * 255.0f + 0.5f);
        }

        ColorPicker::ColorPicker() : UIElement("ColorPicker")
        {
            _state._wants_mouse_events = true;

            // Mouse handling
            OnMouseDown() += [this](UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON) return;
                auto lp = ToLocal(e._mouse_position);
                if (IsPointInside(lp, _rect_sv))
                {
                    _drag_sv = true;
                }
                else if (IsPointInside(lp, _rect_hue))
                {
                    _drag_hue = true;
                }
                else if (_show_alpha && IsPointInside(lp, _rect_alpha))
                {
                    _drag_alpha = true;
                }
                if (_drag_sv || _drag_hue || _drag_alpha)
                {
                    _eventmap[UI::UIEvent::EType::kMouseMove].Invoke(e);// update immediately
                    e._is_handled = true;
                }
            };
            OnMouseUp() += [this](UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON) return;
                _drag_sv = _drag_hue = _drag_alpha = false;
            };
            OnMouseMove() += [this](UIEvent &e)
            {
                auto lp = ToLocal(e._mouse_position);
                bool changed = false;
                if (_drag_sv)
                {
                    // s: x in [0,1], v: y in [1,0]
                    f32 s = RemapClamped(lp.x, _rect_sv.x, _rect_sv.x + _rect_sv.z);
                    f32 v = 1.0f - RemapClamped(lp.y, _rect_sv.y, _rect_sv.y + _rect_sv.w);
                    s = std::clamp(s, 0.0f, 1.0f);
                    v = std::clamp(v, 0.0f, 1.0f);
                    if (s != _hsv.y || v != _hsv.z)
                    {
                        _hsv.y = s;
                        _hsv.z = v;
                        changed = true;
                    }
                }
                if (_drag_hue)
                {
                    f32 h = RemapClamped(lp.x, _rect_hue.x, _rect_hue.x + _rect_hue.z);
                    h = std::clamp(h, 0.0f, 1.0f);
                    if (h != _hsv.x)
                    {
                        _hsv.x = h;
                        changed = true;
                    }
                }
                if (_drag_alpha && _show_alpha)
                {
                    f32 a = RemapClamped(lp.x, _rect_alpha.x, _rect_alpha.x + _rect_alpha.z);
                    a = std::clamp(a, 0.0f, 1.0f);
                    if (a != _alpha)
                    {
                        _alpha = a;
                        changed = true;
                    }
                }
                if (changed)
                {
                    _rgba = HsvToRgb(_hsv);
                    InvalidateLayout();// ensure redraw
                    _on_value_changed_delegate.Invoke(GetColorRGBA());
                    e._is_handled = true;
                }
            };
        }

        ColorPicker::ColorPicker(const String &name) : ColorPicker() { _name = name; }

        ColorPicker::ColorPicker(Vector4f old_color) : ColorPicker("ColorPicker")
        {
            _old_color = old_color;
        }

        Vector2f ColorPicker::MeasureDesiredSize()
        {
            if (_slot._size_policy_h == ESizePolicy::kFixed) return _slot._size;
            // default minimum footprint
            return {240.0f, 200.0f};
        }

        void ColorPicker::Update(f32 dt)
        {
            UIElement::Update(dt);

            EnsureStaticTextures();

            // Layout sub-rects within content
            const f32 spacing = 6.0f;
            Vector4f c = _content_rect;// ltwh in local space
            f32 barsH = 16.0f + spacing + (_show_alpha ? 16.0f : 0.0f);
            f32 svSize = std::min(c.z, c.w - barsH - spacing);

            // SV square at top-left
            _rect_sv = {c.x, c.y, svSize, svSize};

            f32 curY = c.y + svSize + spacing;
            _rect_hue = {c.x, curY, svSize, 16.0f};

            curY += 16.0f + spacing;
            if (_show_alpha)
            {
                _rect_alpha = {c.x, curY, svSize, 16.0f};
            }
            else
            {
                _rect_alpha = {0, 0, 0, 0};
            }

            // Preview box to the right if room, else below
            f32 rightW = c.z - svSize - spacing;
            if (rightW > 40.0f)
            {
                _rect_preview = {c.x + svSize + spacing, c.y, rightW, 40.0f};
            }
            else
            {
                _rect_preview = {c.x, curY + (_show_alpha ? 16.0f + spacing : 0.0f), svSize, 20.0f};
            }

            // Rebuild SV texture if hue changed significantly
            if (std::abs(_hsv.x - _last_h_for_sv) > 1e-6f || !_tex_sv)
            {
                RebuildSVTexture();
                _last_h_for_sv = _hsv.x;
            }
        }

        void ColorPicker::RenderImpl(UIRenderer &r)
        {
            r.DrawQuad(_arrange_rect, _matrix, Colors::kBlack);
            // SV box
            if (_tex_sv)
            {
                ImageDrawOptions opt;
                opt._transform = _matrix;
                opt._size_override = {SV_RES, SV_RES};
                r.DrawImage(_tex_sv.get(), _rect_sv, opt);
            }
            else
            {
                r.DrawQuad(_rect_sv, _matrix, Colors::kBlack);
            }


            // Hue bar
            if (s_tex_hue)
            {
                ImageDrawOptions opt;
                opt._transform = _matrix;
                opt._size_override = {HUE_RES, 1};
                r.DrawImage(s_tex_hue.get(), _rect_hue, opt);
            }
            // Alpha bar (checker + gradient)
            if (_show_alpha)
            {
                if (s_tex_checker)
                {
                    ImageDrawOptions opt;
                    opt._transform = _matrix;
                    opt._size_override = {CHECK_RES, CHECK_RES};
                    r.DrawImage(s_tex_checker.get(), _rect_alpha, opt);
                }
                // Build a 256x1 horizontal gradient for alpha on the fly (cheap)
                //Vector3f rgb = HsvToRgb(_hsv);
                //auto texA = Texture2D::Create(256, 1, Render::ETextureFormat::kRGBA8UNorm, false, false);
                //Vector<u8> row(256 * 4);
                //for (u32 x = 0; x < 256; ++x)
                //{
                //    float a = x / 255.0f;
                //    row[x * 4 + 0] = toByte(rgb.x);
                //    row[x * 4 + 1] = toByte(rgb.y);
                //    row[x * 4 + 2] = toByte(rgb.z);
                //    row[x * 4 + 3] = toByte(a);
                //}
                //texA->SetPixelData(row.data(), 0, 0);
                //texA->Apply();
                //ImageDrawOptions opt;
                //opt._transform = _matrix;
                //opt._size_override = {256, 1};
                //r.DrawImage(texA.get(), _rect_alpha, opt);
            }

            // Preview: left old (not tracked), right current
            Vector4f pr = _rect_preview;
            if (pr.z > 0 && pr.w > 0)
            {
                // Two halves
                Vector4f left = {pr.x, pr.y, pr.z * 0.5f, pr.w};
                Vector4f right = {pr.x + pr.z * 0.5f, pr.y, pr.z * 0.5f, pr.w};
                r.DrawQuad(left, _matrix, _old_color);
                r.DrawQuad(right, _matrix, {_rgba.x, _rgba.y, _rgba.z, _alpha});
                r.DrawBox(pr.xy, pr.zw, _matrix, 1.0f, Colors::kWhite);
            }

            // Handles
            // SV handle
            if (_rect_sv.z > 0 && _rect_sv.w > 0)
            {
                Vector2f hp = {_rect_sv.x + _hsv.y * _rect_sv.z, _rect_sv.y + (1.0f - _hsv.z) * _rect_sv.w};
                r.DrawBox(hp - Vector2f{4, 4}, {8, 8}, _matrix, 3.f, Colors::kWhite);
            }
            // Hue handle
            if (_rect_hue.z > 0)
            {
                f32 hx = _rect_hue.x + _hsv.x * _rect_hue.z;
                r.DrawLine({hx, _rect_hue.y}, {hx, _rect_hue.y + _rect_hue.w}, _matrix, 4.0f, Colors::kWhite);
            }
            // Alpha handle
            if (_show_alpha && _rect_alpha.z > 0)
            {
                f32 ax = _rect_alpha.x + _alpha * _rect_alpha.z;
                r.DrawLine({ax, _rect_alpha.y}, {ax, _rect_alpha.y + _rect_alpha.w}, _matrix, 4.0f, Colors::kWhite);
            }
        }

        void ColorPicker::SetColorRGBA(Vector4f rgba)
        {
            Vector3f rgb{rgba.x, rgba.y, rgba.z};
            _hsv = RgbToHsv(rgb);
            _rgba = rgb;
            _alpha = rgba.w;
            InvalidateLayout();
            _on_value_changed_delegate.Invoke(GetColorRGBA());
        }

        void ColorPicker::SetColorHSVA(Vector4f hsva)
        {
            _hsv = {hsva.x, hsva.y, hsva.z};
            _alpha = hsva.w;
            _rgba = HsvToRgb(_hsv);
            InvalidateLayout();
            _on_value_changed_delegate.Invoke(GetColorRGBA());
        }

        void ColorPicker::EnsureStaticTextures()
        {
            if (!s_tex_hue)
            {
                s_tex_hue = Texture2D::Create(HUE_RES, 1, Render::ETextureFormat::kRGBA8UNorm, false, false);
                Vector<u8> row(HUE_RES * 4);
                for (u32 x = 0; x < HUE_RES; ++x)
                {
                    float h = x / float(HUE_RES - 1);
                    Vector3f rgb = HsvToRgb({h, 1.0f, 1.0f});
                    row[x * 4 + 0] = toByte(rgb.x);
                    row[x * 4 + 1] = toByte(rgb.y);
                    row[x * 4 + 2] = toByte(rgb.z);
                    row[x * 4 + 3] = 255;
                }
                s_tex_hue->SetPixelData(row.data(), 0, 0);
                s_tex_hue->Apply();
            }
            if (!s_tex_checker)
            {
                s_tex_checker = Texture2D::Create(CHECK_RES, CHECK_RES, Render::ETextureFormat::kRGBA8UNorm, false, false);
                Vector<u8> data(CHECK_RES * CHECK_RES * 4);
                for (u32 y = 0; y < CHECK_RES; ++y)
                {
                    for (u32 x = 0; x < CHECK_RES; ++x)
                    {
                        bool dark = ((x / 2 + y / 2) % 2) == 0;// small tiles
                        u8 v = dark ? 180 : 220;
                        u32 idx = (y * CHECK_RES + x) * 4;
                        data[idx + 0] = v;
                        data[idx + 1] = v;
                        data[idx + 2] = v;
                        data[idx + 3] = 255;
                    }
                }
                s_tex_checker->SetPixelData(data.data(), 0, 0);
                s_tex_checker->Apply();
            }
        }

        void ColorPicker::RebuildSVTexture()
        {
            _tex_sv = Texture2D::Create(SV_RES, SV_RES, Render::ETextureFormat::kRGBA8UNorm, false, false);
            Vector<u8> data(SV_RES * SV_RES * 4);
            for (u32 y = 0; y < SV_RES; ++y)
            {
                float v = 1.0f - (float) y / (SV_RES - 1);
                for (u32 x = 0; x < SV_RES; ++x)
                {
                    float s = (float) x / (SV_RES - 1);
                    Vector3f rgb = HsvToRgb({_hsv.x, s, v});
                    u32 idx = (y * SV_RES + x) * 4;
                    data[idx + 0] = toByte(rgb.x);
                    data[idx + 1] = toByte(rgb.y);
                    data[idx + 2] = toByte(rgb.z);
                    data[idx + 3] = 255;
                }
            }
            _tex_sv->SetPixelData(data.data(), 0, 0);
            _tex_sv->Apply();
        }

        Vector2f ColorPicker::ToLocal(Vector2f screen) const
        {
            return TransformCoord(_inv_matrix, {screen, 0.0f}).xy;
        }

        f32 ColorPicker::RemapClamped(f32 v, f32 a, f32 b)
        {
            if (b == a) return 0.0f;
            return std::clamp((v - a) / (b - a), 0.0f, 1.0f);
        }

        Vector3f ColorPicker::RgbToHsv(const Vector3f &rgb)
        {
            float r = rgb.x, g = rgb.y, b = rgb.z;
            float maxv = std::max({r, g, b});
            float minv = std::min({r, g, b});
            float d = maxv - minv;
            float h = 0.0f;
            if (d > 1e-6f)
            {
                if (maxv == r) h = fmodf(((g - b) / d), 6.0f);
                else if (maxv == g)
                    h = ((b - r) / d) + 2.0f;
                else
                    h = ((r - g) / d) + 4.0f;
                h /= 6.0f;
                if (h < 0.0f) h += 1.0f;
            }
            float s = (maxv == 0.0f) ? 0.0f : (d / maxv);
            float v = maxv;
            return {h, s, v};
        }

        Vector3f ColorPicker::HsvToRgb(const Vector3f &hsv)
        {
            float h = hsv.x, s = hsv.y, v = hsv.z;
            if (s <= 1e-6f) return {v, v, v};
            float f = h * 6.0f;
            int i = (int) floorf(f);
            f -= i;
            float p = v * (1.0f - s);
            float q = v * (1.0f - f * s);
            float t = v * (1.0f - (1.0f - f) * s);
            switch (i % 6)
            {
                case 0:
                    return {v, t, p};
                case 1:
                    return {q, v, p};
                case 2:
                    return {p, v, t};
                case 3:
                    return {p, q, v};
                case 4:
                    return {t, p, v};
                default:
                    return {v, p, q};
            }
        }

    }// namespace UI
}// namespace Ailu
