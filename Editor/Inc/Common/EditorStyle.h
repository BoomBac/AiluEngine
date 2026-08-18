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
            Color _window_bg_color;// 窗口背景色；运行时线性空间，JSON 使用 sRGB

            APROPERTY()
            Color _window_title_bar_color;// 标题栏背景色

            APROPERTY()
            Color _window_title_text_color;// 标题文字颜色

            APROPERTY()
            Color _window_border_color;// 窗口边框颜色

            APROPERTY()
            Color _window_focus_border_color;// 焦点窗口边框颜色

            APROPERTY()
            f32 _window_corner_radius = 6.0f;

            APROPERTY()
            f32 _dock_panel_gap = 2.0f;


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


            // ----------------- 工具栏 -----------------
            APROPERTY()
            f32 _toolbar_height = 34.0f;

            APROPERTY()
            Color _toolbar_bg_color;// 工具栏背景色

            APROPERTY()
            Color _toolbar_border_color;// 工具栏底部边框色


            // ----------------- 状态栏 -----------------
            APROPERTY()
            f32 _status_bar_height = 24.0f;

            APROPERTY()
            Color _status_bar_bg_color;// 状态栏背景色

            APROPERTY()
            Color _status_bar_play_bg_color;// 运行中状态栏背景色

            APROPERTY()
            Color _status_bar_prefab_bg_color;// 预制体编辑模式状态栏背景色
        };
        static EditorStyle DefaultDark()
        {
            EditorStyle s{};
            // 窗口
            s._window_bg_color = Color(0.145f, 0.155f, 0.175f, 1.0f);
            s._window_title_bar_color = Color(0.118f, 0.126f, 0.145f, 1.0f);
            s._window_title_text_color = Color(0.80f, 0.84f, 0.90f, 1.0f);
            s._window_border_color = Color(0.255f, 0.275f, 0.315f, 1.0f);
            s._window_focus_border_color = Color(0.18f, 0.46f, 0.78f, 1.0f);
            s._window_corner_radius = 6.0f;
            s._dock_panel_gap = 2.0f;

            // Tab
            s._tab_bg_color = Color(0.118f, 0.126f, 0.145f, 1.0f);
            s._tab_active_bg_color = Color(0.145f, 0.155f, 0.175f, 1.0f);
            s._tab_hover_bg_color = Color(0.18f, 0.195f, 0.225f, 1.0f);
            s._tab_text_color = Color(0.68f, 0.72f, 0.78f, 1.0f);
            s._tab_active_text_color = Color(0.92f, 0.94f, 0.97f, 1.0f);
            s._tab_hover_text_color = Color(0.84f, 0.87f, 0.92f, 1.0f);
            s._tab_border_color = Color(0.255f, 0.275f, 0.315f, 1.0f);

            // 分隔条
            s._splitter_color = Color(0.31f, 0.31f, 0.33f, 1.0f);
            s._splitter_hover_color = Color(0.47f, 0.47f, 0.49f, 1.0f);
            s._splitter_thickness = 3.0f;

            // Dock 提示
            s._dock_hint_color = Color(0.31f, 0.55f, 1.0f, 0.3f);
            s._dock_hint_border_color = Color(0.31f, 0.55f, 1.0f, 0.8f);
            s._dock_hint_border_thickness = 2.0f;

            // 工具栏
            s._toolbar_height = 34.0f;
            s._toolbar_bg_color = Color(0.18f, 0.19f, 0.21f, 1.0f);
            s._toolbar_border_color = Color(0.34f, 0.36f, 0.40f, 1.0f);

            // 状态栏
            s._status_bar_height = 24.0f;
            s._status_bar_bg_color = Color(0.16f, 0.17f, 0.18f, 1.0f);
            s._status_bar_play_bg_color = Color(0.85f, 0.45f, 0.10f, 1.0f);
            s._status_bar_prefab_bg_color = Color(0.08f, 0.30f, 0.58f, 1.0f);

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
            s._window_focus_border_color = Color(0.10f, 0.42f, 0.76f, 1.0f);
            s._window_corner_radius = 6.0f;
            s._dock_panel_gap = 2.0f;

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

            // 工具栏
            s._toolbar_height = 34.0f;
            s._toolbar_bg_color = Color(0.78f, 0.80f, 0.82f, 1.0f);
            s._toolbar_border_color = Color(0.60f, 0.62f, 0.64f, 1.0f);

            // 状态栏
            s._status_bar_height = 24.0f;
            s._status_bar_bg_color = Color(0.73f, 0.75f, 0.77f, 1.0f);
            s._status_bar_play_bg_color = Color(0.92f, 0.55f, 0.18f, 1.0f);
            s._status_bar_prefab_bg_color = Color(0.18f, 0.45f, 0.78f, 1.0f);

            return s;
        }
        
        extern EditorStyle g_editor_style;
    }// namespace Editor
}// namespace Ailu

#endif// !__EDITOR_STYLE_H__
