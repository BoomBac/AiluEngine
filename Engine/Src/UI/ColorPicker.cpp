#include "UI/ColorPicker.h"
#include "UI/Basic.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Shader.h"
#include "UI/TextRenderer.h"
#include "UI/UIRenderer.h"
#include "pch.h"

namespace Ailu
{
    namespace UI
    {
        using Render::Texture2D;
        using Render::TextureDesc;

        void ColorPicker::ResolveStyle(const UIStyleContext &context)
        {
            _resolved_style = context._theme ? context._theme->_color_picker_style : UIColorPickerStyle{};
        }

        static constexpr u16 HUE_RES = 256;
        static constexpr u16 CHECK_RES = 8;
        static constexpr f32 kHdrMaxIntensity = 64.0f;

        static String FormatColorChannelText(f32 value)
        {
            return std::format("{:.3f}", value);
        }

        static u8 toByte(float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<u8>(v * 255.0f + 0.5f);
        }

        static UIBrush ColorBrush(const Color &color)
        {
            UIBrush brush;
            brush._type = EUIBrushType::kColor;
            brush._tint = color;
            return brush;
        }

        ColorPicker::ColorPicker() : UIElement("ColorPicker")
        {
            SetWantsMouseEvents(true);
            const Array<String, 4> channel_names = {"R", "G", "B", "A"};
            for (u32 i = 0; i < channel_names.size(); ++i)
            {
                auto *input = AddChild<InputBlock>(channel_names[i]);
                input->Name(std::format("ColorChannel_{}", channel_names[i]));
                _channel_inputs[i] = input;
            }
            _channel_inputs[0]->_on_content_changed += [this](String content)
            {
                if (auto value = StringUtils::ParseFloat(content); value.has_value())
                {
                    auto color = GetColorRGBA();
                    color.x = std::max(0.0f, value.value());
                    SyncStateFromRGBA(color);
                    NotifyValueChanged();
                }
            };
            _channel_inputs[1]->_on_content_changed += [this](String content)
            {
                if (auto value = StringUtils::ParseFloat(content); value.has_value())
                {
                    auto color = GetColorRGBA();
                    color.y = std::max(0.0f, value.value());
                    SyncStateFromRGBA(color);
                    NotifyValueChanged();
                }
            };
            _channel_inputs[2]->_on_content_changed += [this](String content)
            {
                if (auto value = StringUtils::ParseFloat(content); value.has_value())
                {
                    auto color = GetColorRGBA();
                    color.z = std::max(0.0f, value.value());
                    SyncStateFromRGBA(color);
                    NotifyValueChanged();
                }
            };
            _channel_inputs[3]->_on_content_changed += [this](String content)
            {
                if (auto value = StringUtils::ParseFloat(content); value.has_value())
                {
                    auto color = GetColorRGBA();
                    color.w = std::clamp(value.value(), 0.0f, 1.0f);
                    SyncStateFromRGBA(color);
                    NotifyValueChanged();
                }
            };
            SyncInputFields();

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
                else if (_show_hdr && IsPointInside(lp, _rect_hdr))
                {
                    _drag_hdr = true;
                }
                if (_drag_sv || _drag_hue || _drag_alpha || _drag_hdr)
                {
                    _eventmap[UI::UIEvent::EType::kMouseMove].Invoke(e);// update immediately
                    e._is_handled = true;
                }
            };
            OnMouseUp() += [this](UIEvent &e)
            {
                if (e._key_code != EKey::kLBUTTON) return;
                const bool was_dragging = IsDragAdjusting();
                _drag_sv = _drag_hue = _drag_alpha = _drag_hdr = false;
                if (was_dragging)
                {
                    SyncInputFields();
                    InvalidatePaint();
                }
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
                if (_drag_hdr && _show_hdr)
                {
                    f32 normalized = RemapClamped(lp.x, _rect_hdr.x, _rect_hdr.x + _rect_hdr.z);
                    f32 intensity = HdrIntensityFromNormalized(normalized);
                    if (!NearbyEqual(intensity, _hdr_intensity))
                    {
                        _hdr_intensity = intensity;
                        changed = true;
                    }
                }
                if (changed)
                {
                    SyncRgbFromState();
                    NotifyValueChanged(false);
                    e._is_handled = true;
                }
            };
        }

        ColorPicker::ColorPicker(const String &name) : ColorPicker() { _name = name; }

        ColorPicker::ColorPicker(Color old_color) : ColorPicker("ColorPicker")
        {
            _old_color = old_color;
            SyncStateFromRGBA(old_color);
        }

        Vector2f ColorPicker::MeasureDesiredSize()
        {
            if (auto slot = dynamic_cast<LinearSlot *>(GetSlot().get()); slot != nullptr && slot->_size_policy_h == ESizePolicy::kFixed)
                return slot->_size;
            // default minimum footprint
            return {320.0f, 240.0f};
        }

        void ColorPicker::Update(f32 dt)
        {
            UIElement::Update(dt);

            EnsureStaticTextures();

            // Layout sub-rects within content
            const f32 spacing = 6.0f;
            Vector4f c = _content_rect;// ltwh in local space
            f32 barsH = 16.0f;
            if (_show_alpha)
                barsH += spacing + 16.0f;
            if (_show_hdr)
                barsH += spacing + 16.0f;
            f32 svSize = std::min(c.z, c.w - barsH - spacing);

            // SV square at top-left
            _rect_sv = {c.x, c.y, svSize, svSize};

            f32 curY = c.y + svSize + spacing;
            _rect_hue = {c.x, curY, svSize, 16.0f};

            curY += 16.0f + spacing;
            if (_show_alpha)
            {
                _rect_alpha = {c.x, curY, svSize, 16.0f};
                curY += 16.0f + spacing;
            }
            else
            {
                _rect_alpha = {0, 0, 0, 0};
            }
            if (_show_hdr)
            {
                _rect_hdr = {c.x, curY, svSize, 16.0f};
            }
            else
            {
                _rect_hdr = {0, 0, 0, 0};
            }

            // Preview box to the right if room, else below
            f32 rightW = c.z - svSize - spacing;
            if (rightW > 40.0f)
            {
                _rect_preview = {c.x + svSize + spacing, c.y, rightW, 24.0f};
                _rect_inputs = {c.x + svSize + spacing, c.y + 24.0f + spacing, rightW, 20.0f * 4.0f + spacing * 3.0f};
            }
            else
            {
                _rect_preview = {c.x, curY + (_show_hdr ? 16.0f + spacing : 0.0f), svSize, 24.0f};
                _rect_inputs = {c.x, _rect_preview.y + _rect_preview.w + spacing, svSize, 20.0f * 4.0f + spacing * 3.0f};
            }

            for (u32 i = 0; i < _channel_inputs.size(); ++i)
            {
                if (_channel_inputs[i] == nullptr)
                    continue;
                const f32 row_y = _rect_inputs.y + i * (20.0f + spacing);
                _channel_inputs[i]->Arrange(_rect_inputs.x, row_y, _rect_inputs.z, 20.0f);
            }
            SyncInputFields();

        }

        void ColorPicker::RenderImpl(UIRenderer &r)
        {
            r.DrawQuad(_arrange_rect, _matrix, ColorBrush(_resolved_style._background_color));
            // SV box: calculate HSV in the pixel shader so hue changes only update a material parameter.
            if (_sv_material)
            {
                _sv_material->SetFloat("_Hue", _hsv.x);
                ImageDrawOptions opt;
                opt._transform = _matrix;
                r.DrawImage(Render::Texture::s_p_default_white, _rect_sv, opt, _sv_material.get());
            }
            else
            {
                r.DrawQuad(_rect_sv, _matrix, ColorBrush(_resolved_style._background_color));
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

            if (_show_hdr)
            {
                constexpr u32 kHdrBarSegments = 24u;
                Vector3f base_rgb = HsvToRgb(_hsv);
                for (u32 i = 0; i < kHdrBarSegments; ++i)
                {
                    f32 t0 = static_cast<f32>(i) / static_cast<f32>(kHdrBarSegments);
                    f32 t1 = static_cast<f32>(i + 1u) / static_cast<f32>(kHdrBarSegments);
                    f32 intensity = HdrIntensityFromNormalized((t0 + t1) * 0.5f);
                    Vector3f preview_rgb = ToneMapPreview(base_rgb * intensity);
                    r.DrawQuad({_rect_hdr.x + _rect_hdr.z * t0, _rect_hdr.y, _rect_hdr.z * (t1 - t0), _rect_hdr.w}, _matrix,
                               ColorBrush({preview_rgb.x, preview_rgb.y, preview_rgb.z, 1.0f}));
                }
                r.DrawBox(_rect_hdr.xy, _rect_hdr.zw, _matrix, 1.0f, _resolved_style._border_color);
                r.DrawText(std::format("HDR {:.2f}x", _hdr_intensity), {_rect_hdr.x + 4.0f, _rect_hdr.y + 1.0f}, _matrix, 12.0f, _resolved_style._label_color);
            }

            // Preview: left old (not tracked), right current
            Vector4f pr = _rect_preview;
            if (pr.z > 0 && pr.w > 0)
            {
                // Two halves
                Vector4f left = {pr.x, pr.y, pr.z * 0.5f, pr.w};
                Vector4f right = {pr.x + pr.z * 0.5f, pr.y, pr.z * 0.5f, pr.w};
                Color old_srgb = _old_color.ToSrgb();
                Color new_srgb = {_rgba.x, _rgba.y, _rgba.z, _alpha};
                Vector3f old_preview = ToneMapPreview({old_srgb.x, old_srgb.y, old_srgb.z});
                Vector3f new_preview = ToneMapPreview({new_srgb.x, new_srgb.y, new_srgb.z});
                r.DrawQuad(left, _matrix, ColorBrush({old_preview.x, old_preview.y, old_preview.z, std::clamp(_old_color.w, 0.0f, 1.0f)}));
                r.DrawQuad(right, _matrix, ColorBrush({new_preview.x, new_preview.y, new_preview.z, _alpha}));
                r.DrawBox(pr.xy, pr.zw, _matrix, 1.0f, _resolved_style._border_color);
                r.DrawText("Old", {left.x + 4.0f, left.y + 4.0f}, _matrix, 12.0f, _resolved_style._label_color);
                r.DrawText(std::format("New {:.2f}x", _hdr_intensity), {right.x + 4.0f, right.y + 4.0f}, _matrix, 12.0f, _resolved_style._label_color);
            }

            for (u32 i = 0; i < _channel_inputs.size(); ++i)
            {
                static const Array<String, 4> kLabels = {"R", "G", "B", "A"};
                if (_channel_inputs[i] == nullptr)
                    continue;
                r.DrawText(kLabels[i], {_rect_inputs.x - 14.0f, _rect_inputs.y + i * 26.0f + 2.0f}, _matrix, 12.0f, _resolved_style._label_color);
                _channel_inputs[i]->Render(r);
            }

            // Handles
            // SV handle
            if (_rect_sv.z > 0 && _rect_sv.w > 0)
            {
                Vector2f hp = {_rect_sv.x + _hsv.y * _rect_sv.z, _rect_sv.y + (1.0f - _hsv.z) * _rect_sv.w};
                r.DrawBox(hp - Vector2f{4, 4}, {8, 8}, _matrix, 3.f, _resolved_style._handle_color);
            }
            // Hue handle
            if (_rect_hue.z > 0)
            {
                f32 hx = _rect_hue.x + _hsv.x * _rect_hue.z;
                r.DrawLine({hx, _rect_hue.y}, {hx, _rect_hue.y + _rect_hue.w}, _matrix, 4.0f, _resolved_style._handle_color);
            }
            // Alpha handle
            if (_show_alpha && _rect_alpha.z > 0)
            {
                f32 ax = _rect_alpha.x + _alpha * _rect_alpha.z;
                r.DrawLine({ax, _rect_alpha.y}, {ax, _rect_alpha.y + _rect_alpha.w}, _matrix, 4.0f, _resolved_style._handle_color);
            }
            if (_show_hdr && _rect_hdr.z > 0)
            {
                f32 hdr_x = _rect_hdr.x + NormalizedFromHdrIntensity(_hdr_intensity) * _rect_hdr.z;
                r.DrawLine({hdr_x, _rect_hdr.y}, {hdr_x, _rect_hdr.y + _rect_hdr.w}, _matrix, 4.0f, _resolved_style._handle_color);
            }
        }

        void ColorPicker::SetColorRGBA(Color rgba)
        {
            SyncStateFromRGBA(rgba);
            NotifyValueChanged();
        }

        void ColorPicker::SetColorHSVA(Vector4f hsva)
        {
            SyncStateFromHSVA(hsva);
            NotifyValueChanged();
        }

        UIElement *ColorPicker::HitTest(Vector2f pos)
        {
            Vector2f lpos = TransformCoord(_inv_matrix, {pos, 0.0f}).xy;
            if (!IsPointInside(lpos))
                return nullptr;

            for (auto &child: _children)
            {
                if (auto *hit = child->HitTest(pos); hit != nullptr)
                    return hit;
            }
            return this;
        }

        void ColorPicker::SyncRgbFromState()
        {
            _rgba = HsvToRgb(_hsv) * _hdr_intensity;
        }

        void ColorPicker::SyncStateFromRGBA(Color rgba)
        {
            Color srgb = rgba.ToSrgb();
            Vector3f rgb = {std::max(srgb.x, 0.0f), std::max(srgb.y, 0.0f), std::max(srgb.z, 0.0f)};
            _hdr_intensity = std::clamp(std::max({1.0f, rgb.x, rgb.y, rgb.z}), 1.0f, kHdrMaxIntensity);
            Vector3f normalized_rgb = _hdr_intensity > 0.0f ? rgb / _hdr_intensity : Vector3f::kZero;
            _hsv = RgbToHsv(normalized_rgb);
            _alpha = std::clamp(rgba.a, 0.0f, 1.0f);
            SyncRgbFromState();
        }

        void ColorPicker::SyncStateFromHSVA(Vector4f hsva)
        {
            f32 effective_value = std::max(hsva.z, 0.0f);
            _hsv.x = hsva.x - std::floor(hsva.x);
            _hsv.y = std::clamp(hsva.y, 0.0f, 1.0f);
            _hdr_intensity = std::clamp(std::max(1.0f, effective_value), 1.0f, kHdrMaxIntensity);
            _hsv.z = std::clamp(effective_value / _hdr_intensity, 0.0f, 1.0f);
            _alpha = std::clamp(hsva.w, 0.0f, 1.0f);
            SyncRgbFromState();
        }

        void ColorPicker::NotifyValueChanged(bool sync_input_fields)
        {
            InvalidatePaint();
            if (sync_input_fields)
                SyncInputFields();
            _on_value_changed_delegate.Invoke(GetColorRGBA());
        }

        void ColorPicker::SyncInputFields()
        {
            Vector4f color = GetColorRGBA();
            const Array<f32, 4> channel_values = {color.x, color.y, color.z, color.w};
            for (u32 i = 0; i < _channel_inputs.size(); ++i)
            {
                if (_channel_inputs[i] != nullptr && !_channel_inputs[i]->IsEditing())
                    _channel_inputs[i]->SetContent(FormatColorChannelText(channel_values[i]), false);
            }
        }

        void ColorPicker::EnsureStaticTextures()
        {
            if (!_sv_material)
            {
                auto *shader = ResourceMgr::Get().Get<Render::Shader>(L"Shaders/hlsl/color_picker_sv.alasset");
                if (shader != nullptr)
                {
                    _sv_material = MakeRef<Render::Material>(shader, "ColorPicker_SVMaterial");
                    _sv_material->SetFloat("_Hue", _hsv.x);
                }
            }
            if (!s_tex_hue)
            {
                s_tex_hue = Texture2D::Create(HUE_RES, 1, Render::ETextureFormat::kRGBA8UNormSRGB, false, false);
                s_tex_hue->Name("ColorPicker_HueBar");
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
                s_tex_checker = Texture2D::Create(CHECK_RES, CHECK_RES, Render::ETextureFormat::kRGBA8UNormSRGB, false, false);
                s_tex_checker->Name("ColorPicker_Checker");
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

        Vector3f ColorPicker::ToneMapPreview(const Vector3f &rgb)
        {
            return {
                rgb.x / (1.0f + std::max(rgb.x, 0.0f)),
                rgb.y / (1.0f + std::max(rgb.y, 0.0f)),
                rgb.z / (1.0f + std::max(rgb.z, 0.0f))
            };
        }

        f32 ColorPicker::HdrIntensityFromNormalized(f32 normalized_value)
        {
            normalized_value = std::clamp(normalized_value, 0.0f, 1.0f);
            return std::pow(2.0f, normalized_value * std::log2(kHdrMaxIntensity));
        }

        f32 ColorPicker::NormalizedFromHdrIntensity(f32 intensity)
        {
            intensity = std::clamp(intensity, 1.0f, kHdrMaxIntensity);
            return std::log2(intensity) / std::log2(kHdrMaxIntensity);
        }

    }// namespace UI
}// namespace Ailu
