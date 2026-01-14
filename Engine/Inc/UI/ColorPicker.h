#ifndef AILU_UI_COLOR_PICKER_H
#define AILU_UI_COLOR_PICKER_H

#include "UIElement.h"
#include "Render/Texture.h"
#include "generated/ColorPicker.gen.h"

namespace Ailu {
namespace UI {

ACLASS()
class AILU_API ColorPicker : public UIElement 
{
    GENERATED_BODY()
    DECLARE_DELEGATE(on_value_changed, Vector4f);
public:
    ColorPicker();
    explicit ColorPicker(const String& name);
    explicit ColorPicker(Vector4f old_color);

    Vector2f MeasureDesiredSize() override;
    void Update(f32 dt) override;

    Vector4f GetColorRGBA() const { return {_rgba.x, _rgba.y, _rgba.z, _alpha}; }
    void SetColorRGBA(Vector4f rgba);

    Vector4f GetColorHSVA() const { return {_hsv.x, _hsv.y, _hsv.z, _alpha}; }
    void SetColorHSVA(Vector4f hsva);

    auto OnValueChanged() { return _on_value_changed_delegate.GetEventView(); }

    void SetShowAlpha(bool v) { _show_alpha = v; InvalidateLayout(); }
    bool GetShowAlpha() const { return _show_alpha; }

private:
    void RenderImpl(UIRenderer& r) override;
    void RebuildSVTexture();
    void EnsureStaticTextures();

    static Vector3f RgbToHsv(const Vector3f& rgb);
    static Vector3f HsvToRgb(const Vector3f& hsv);

    // Helpers converting mouse -> local and clamping to rect
    Vector2f ToLocal(Vector2f screen) const;
    static f32 RemapClamped(f32 v, f32 a, f32 b);

private:
    // State
    APROPERTY()
    Vector3f _hsv = {0.0f, 1.0f, 1.0f};
    APROPERTY()
    f32 _alpha = 1.0f;

    Vector3f _rgba = {1.0f,1.0f,1.0f};

    // Layout sub-rects (local/content space, ltwh)
    Vector4f _rect_sv{0,0,0,0};
    Vector4f _rect_hue{0,0,0,0};
    Vector4f _rect_alpha{0,0,0,0};
    Vector4f _rect_preview{0,0,0,0};

    Vector4f _old_color = Colors::kWhite;

    // Dragging flags
    bool _drag_sv = false;
    bool _drag_hue = false;
    bool _drag_alpha = false;

    // Textures
    Ref<Render::Texture2D> _tex_sv;     // 256x256, depends on hue
    inline static Ref<Render::Texture2D> s_tex_hue;    // 256x1 rainbow
    inline static Ref<Render::Texture2D> s_tex_checker; // 8x8 checker
    inline static Ref<Render::Texture2D> s_tex_alpha;

    // Options
    APROPERTY()
    bool _show_alpha = true;

    // Cached last hue to rebuild SV
    f32 _last_h_for_sv = -1.0f;
};

} // namespace UI
} // namespace Ailu

#endif // AILU_UI_COLOR_PICKER_H
