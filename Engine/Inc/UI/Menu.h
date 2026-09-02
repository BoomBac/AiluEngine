#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "UI/UIElement.h"

#include <functional>

namespace Ailu::UI
{
    class UIElement;

    struct MenuEntry
    {
        String _label;
        std::function<void()> _on_click;
        bool _is_destructive = false;
        Vector<MenuEntry> _children;
        bool _is_enabled = true;
        bool _is_separator = false;
    };

    struct MenuOptions
    {
        f32 _item_width = 180.0f;
        f32 _item_height = 24.0f;
        f32 _max_height = 220.0f;
        f32 _submenu_offset = 0.0f;
    };

    class AILU_API Menu final
    {
    public:
        static void ShowAt(Vector2f position, const Vector<MenuEntry> &entries, const MenuOptions &options = {});
        static void ShowAt(UIElement *anchor, const Vector<MenuEntry> &entries, const MenuOptions &options = {});
        static void Hide();
    };
}
