#pragma once
#include "Framework/Common/Path.h"

namespace Ailu
{
    namespace UI
    {
        struct UITheme;
    }
    namespace Editor
    {
        extern UI::UITheme g_editor_ui_theme;

        Path GetDefaultUIThemePath();
        Path GetDefaultEditorStylePath();
        bool LoadUIThemeFromFile(const Path &path);
        bool SaveUIThemeToFile(const Path &path);
        bool LoadEditorStyleFromFile(const Path &path);
        bool SaveEditorStyleToFile(const Path &path);
        void ApplyUIThemeChanges();
        void ShowStyleThemeEditorWindow(bool *is_show);
    }
}
