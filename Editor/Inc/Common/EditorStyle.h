#pragma once
#ifndef __EDITOR_STYLE_H__
#define __EDITOR_STYLE_H__
#include "Framework/Math/ALMath.hpp"
#include "Objects/Type.h"
#include "generated/EditorStyle.gen.h"
namespace Ailu
{
    namespace Editor
    {
        ASTRUCT()
        struct EditorStyle
        {
            GENERATED_BODY()
            APROPERTY()
            Color _window_bg_color;
            APROPERTY()
            Color _window_title_bar_color;
            APROPERTY()
            Color _window_border_color;
        };
        extern EditorStyle g_editor_style;
    }// namespace Editor
}// namespace Ailu

#endif// !__EDITOR_STYLE_H__
