#ifndef AILU_UI_COLOR_PICKER_H
#define AILU_UI_COLOR_PICKER_H

#include "UIElement.h"
#include "UI/Style/UIStyles.h"
#include "Framework/Core/Containers/Array.h"

namespace Ailu
{
    namespace Render
    {
        class Material;
        class Texture2D;
    }
}

#include "generated/ColorPicker.gen.h"

namespace Ailu {
namespace UI {

class InputBlock;

ACLASS()
class AILU_API ColorPicker : public UIElement 
{
    GENERATED_BODY()
    DECLARE_DELEGATE(on_value_changed, Color);
public:
    ColorPicker();
    explicit ColorPicker(const String& name);
    explicit ColorPicker(Color old_color);

    Vector2f MeasureDesiredSize() override;
    void Update(f32 dt) override;
    UIElement *HitTest(Vector2f pos) override;

    Color GetColorRGBA() const { return Color::FromSrgb({_rgba.x, _rgba.y, _rgba.z, _alpha}); }
    void SetColorRGBA(Color rgba);

    Vector4f GetColorHSVA() const { return {_hsv.x, _hsv.y, _hsv.z, _alpha}; }
    void SetColorHSVA(Vector4f hsva);

    auto OnValueChanged() { return _on_value_changed_delegate.GetEventView(); }
    bool IsDragAdjusting() const { return _drag_sv || _drag_hue || _drag_alpha || _drag_hdr; }

    void SetShowAlpha(bool v) { _show_alpha = v; InvalidateLayout(); }
    bool GetShowAlpha() const { return _show_alpha; }
    void SetShowHDR(bool v) { _show_hdr = v; InvalidateLayout(); }
    bool GetShowHDR() const { return _show_hdr; }

private:
    void ResolveStyle(const UIStyleContext &context) override;
    void RenderImpl(UIRenderer& r) override;
    void EnsureStaticTextures();
    void SyncRgbFromState();
    void SyncStateFromRGBA(Color rgba);
    void SyncStateFromHSVA(Vector4f hsva);
    void SyncInputFields();
    void NotifyValueChanged(bool sync_input_fields = true);

    static Vector3f RgbToHsv(const Vector3f& rgb);
    static Vector3f HsvToRgb(const Vector3f& hsv);
    static Vector3f ToneMapPreview(const Vector3f& rgb);
    static f32 HdrIntensityFromNormalized(f32 normalized_value);
    static f32 NormalizedFromHdrIntensity(f32 intensity);

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
    Vector4f _rect_hdr{0,0,0,0};
    Vector4f _rect_preview{0,0,0,0};
    Vector4f _rect_inputs{0,0,0,0};

    Color _old_color = Colors::kWhite;

    // Dragging flags
    bool _drag_sv = false;
    bool _drag_hue = false;
    bool _drag_alpha = false;
    bool _drag_hdr = false;

    // Textures
    Ref<Render::Material> _sv_material; // HSV saturation/value shader material
    inline static Ref<Render::Texture2D> s_tex_hue;    // 256x1 rainbow
    inline static Ref<Render::Texture2D> s_tex_checker; // 8x8 checker
    inline static Ref<Render::Texture2D> s_tex_alpha;

    // Options
    APROPERTY()
    bool _show_alpha = true;
    bool _show_hdr = true;

    f32 _hdr_intensity = 1.0f;
    Array<InputBlock*, 4> _channel_inputs{};

    UIColorPickerStyle _resolved_style;
};

} // namespace UI
} // namespace Ailu

#endif // AILU_UI_COLOR_PICKER_H
