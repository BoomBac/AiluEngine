#pragma once

#include "Framework/Math/ALMath.hpp"
#include "Objects/Serialize.h"
#include "UI/UISlot.h"
#include "generated/UIStyleBasic.gen.h"

namespace Ailu
{
    namespace Render
    {
        class Texture;
    };
    namespace UI
    {
        AENUM()
        enum class EUIBrushType: i32
        {
            kNone,
            kColor,
            kTexture,
            kNineSlice,
            kBackdropBlur
        };

        ASTRUCT()
        struct UIBrush
        {
            GENERATED_BODY()

            APROPERTY()
            EUIBrushType _type = EUIBrushType::kColor;

            APROPERTY()
            Color _tint = Colors::kWhite;

            APROPERTY()
            String _texture_guid;

            APROPERTY()
            Vector4f _uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);

            APROPERTY()
            Padding _slice_margin;

            Render::Texture *_texture = nullptr;
        };

        ASTRUCT()
        struct UIColorTokens
        {
            GENERATED_BODY()

            APROPERTY()
            Color _text_primary = Color(0.90f, 0.90f, 0.90f, 1.0f);

            APROPERTY()
            Color _text_disabled = Color(0.50f, 0.50f, 0.50f, 1.0f);

            APROPERTY()
            Color _surface = Color(0.12f, 0.12f, 0.12f, 1.0f);

            APROPERTY()
            Color _surface_hovered = Color(0.18f, 0.18f, 0.18f, 1.0f);

            APROPERTY()
            Color _surface_pressed = Color(0.08f, 0.08f, 0.08f, 1.0f);

            APROPERTY()
            Color _border = Color(0.30f, 0.30f, 0.30f, 1.0f);

            APROPERTY()
            Color _accent = Color(0.10f, 0.55f, 0.95f, 1.0f);
        };

        ASTRUCT()
        struct UISpacingTokens
        {
            GENERATED_BODY()

            APROPERTY()
            f32 _small = 2.0f;

            APROPERTY()
            f32 _medium = 4.0f;

            APROPERTY()
            f32 _large = 8.0f;

            APROPERTY()
            Padding _control_padding = Padding(6.0f, 3.0f, 6.0f, 3.0f);
        };

        ASTRUCT()
        struct UITypographyTokens
        {
            GENERATED_BODY()

            APROPERTY()
            f32 _small_font_size = 12.0f;

            APROPERTY()
            f32 _normal_font_size = 14.0f;

            APROPERTY()
            f32 _title_font_size = 18.0f;
        };
    }// namespace UI
}// namespace Ailu
