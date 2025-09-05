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
            // ----------------- 窗口 -----------------
            APROPERTY()
            Color _window_bg_color;// 窗口背景色

            APROPERTY()
            Color _window_title_bar_color;// 标题栏背景色

            APROPERTY()
            Color _window_title_text_color;// 标题文字颜色

            APROPERTY()
            Color _window_border_color;// 窗口边框颜色


            // ----------------- Tab 栏 -----------------
            APROPERTY()
            Color _tab_bg_color;// Tab 背景色（未选中）

            APROPERTY()
            Color _tab_active_bg_color;// Tab 背景色（选中）

            APROPERTY()
            Color _tab_hover_bg_color;// Tab 背景色（鼠标悬浮）

            APROPERTY()
            Color _tab_text_color;// Tab 文字颜色（未选中）

            APROPERTY()
            Color _tab_active_text_color;// Tab 文字颜色（选中）

            APROPERTY()
            Color _tab_hover_text_color;// Tab 文字颜色（鼠标悬浮）

            APROPERTY()
            Color _tab_border_color;// Tab 边框颜色


            // ----------------- 分隔条 / 拖拽区域 -----------------
            APROPERTY()
            Color _splitter_color;// 分隔条颜色

            APROPERTY()
            Color _splitter_hover_color;// 分隔条 hover 时颜色

            APROPERTY()
            f32 _splitter_thickness = 2.0f;// 分隔条厚度


            // ----------------- Dock 提示区域 -----------------
            APROPERTY()
            Color _dock_hint_color;// Dock 提示区域填充色

            APROPERTY()
            Color _dock_hint_border_color;// Dock 提示区域边框色

            APROPERTY()
            f32 _dock_hint_border_thickness = 2.0f;
        };
        static EditorStyle DefaultDark()
        {
            EditorStyle s{};
            // 窗口
            s._window_bg_color = Color(0.12f, 0.12f, 0.12f, 1.0f);
            s._window_title_bar_color = Color(0.18f, 0.18f, 0.19f, 1.0f);
            s._window_title_text_color = Color(0.86f, 0.86f, 0.86f, 1.0f);
            s._window_border_color = Color(0.24f, 0.24f, 0.24f, 1.0f);

            // Tab
            s._tab_bg_color = Color(0.20f, 0.20f, 0.22f, 1.0f);
            s._tab_active_bg_color = Color(0.28f, 0.28f, 0.29f, 1.0f);
            s._tab_hover_bg_color = Color(0.25f, 0.25f, 0.27f, 1.0f);
            s._tab_text_color = Color(0.78f, 0.78f, 0.78f, 1.0f);
            s._tab_active_text_color = Color(1.0f, 1.0f, 1.0f, 1.0f);
            s._tab_hover_text_color = Color(0.90f, 0.90f, 0.90f, 1.0f);
            s._tab_border_color = Color(0.31f, 0.31f, 0.31f, 1.0f);

            // 分隔条
            s._splitter_color = Color(0.31f, 0.31f, 0.33f, 1.0f);
            s._splitter_hover_color = Color(0.47f, 0.47f, 0.49f, 1.0f);
            s._splitter_thickness = 3.0f;

            // Dock 提示
            s._dock_hint_color = Color(0.31f, 0.55f, 1.0f, 0.3f);
            s._dock_hint_border_color = Color(0.31f, 0.55f, 1.0f, 0.8f);
            s._dock_hint_border_thickness = 2.0f;

            return s;
        }

        static EditorStyle DefaultLight()
        {
            EditorStyle s{};
            // 窗口
            s._window_bg_color = Color(0.94f, 0.94f, 0.94f, 1.0f);
            s._window_title_bar_color = Color(0.88f, 0.88f, 0.88f, 1.0f);
            s._window_title_text_color = Color(0.12f, 0.12f, 0.12f, 1.0f);
            s._window_border_color = Color(0.71f, 0.71f, 0.71f, 1.0f);

            // Tab
            s._tab_bg_color = Color(0.92f, 0.92f, 0.92f, 1.0f);
            s._tab_active_bg_color = Color(1.0f, 1.0f, 1.0f, 1.0f);
            s._tab_hover_bg_color = Color(0.96f, 0.96f, 0.96f, 1.0f);
            s._tab_text_color = Color(0.20f, 0.20f, 0.20f, 1.0f);
            s._tab_active_text_color = Color(0.0f, 0.0f, 0.0f, 1.0f);
            s._tab_hover_text_color = Color(0.08f, 0.08f, 0.08f, 1.0f);
            s._tab_border_color = Color(0.78f, 0.78f, 0.78f, 1.0f);

            // 分隔条
            s._splitter_color = Color(0.78f, 0.78f, 0.78f, 1.0f);
            s._splitter_hover_color = Color(0.59f, 0.59f, 0.59f, 1.0f);
            s._splitter_thickness = 3.0f;

            // Dock 提示
            s._dock_hint_color = Color(0.31f, 0.55f, 1.0f, 0.25f);
            s._dock_hint_border_color = Color(0.31f, 0.55f, 1.0f, 0.7f);
            s._dock_hint_border_thickness = 2.0f;

            return s;
        }
        
        extern EditorStyle g_editor_style;
    }// namespace Editor
}// namespace Ailu

#endif// !__EDITOR_STYLE_H__
