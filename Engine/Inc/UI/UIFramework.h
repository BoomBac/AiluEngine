//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIFRAMEWORK_H
#define AILU_UIFRAMEWORK_H

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "InteractionZone.h"
#include <span>

namespace Ailu
{
    class Window;
    namespace UI
    {
        class Widget;
        class UILayer;
        class UIRenderer;
        class UIElement;
        class UITheme;
        class AILU_API UIManager
        {
        public:
            static void Init();
            static void Shutdown();
            static UIManager *Get();

        public:
            UIManager();
            ~UIManager();
            void Update(f32 dt);
            void RegisterWidget(Ref<Widget> w);
            void UnRegisterWidget(Widget *w);
            void BringToFront(Widget *w);
            void BringToFrontSilently(Widget *w);
            void SendToBack(Widget *w);
            void EnsurePopupWidgetsOnTop();
            // 新增：设置 / 清除 / 获取 当前焦点元素
            void SetFocus(UIElement *element);
            void ClearFocus(UIElement *element = nullptr);
            UIElement *GetFocusedElement() const { return _focus_target; }
            void SetDebugHighlightTarget(UIElement *element) { _debug_highlight_target = element; }
            UIElement *GetDebugHighlightTarget() const { return _debug_highlight_target; }
            void SetDebugReflectorVisible(bool visible) { _is_debug_reflector_visible = visible; }
            bool IsDebugReflectorVisible() const { return _is_debug_reflector_visible; }
            //弹出一个popup widget,位置基于当前窗口左上角，root则会被添加到popup widget的root(canvas)进行显示
            void ShowPopupAt(f32 x, f32 y, Ref<UIElement> root, std::function<void()> on_close = nullptr, Window *win = nullptr);
            void HidePopup();
            Widget *GetPopupWidget() const;
            UITheme *GetTheme() const { return _theme; }
            void SetTheme(UITheme *theme);
            void Destroy(Ref<UIElement> element);
            void OnElementDestroying(UIElement *element);
            [[nodiscard]] std::span<const InteractionZone> GetInteractionZones() const noexcept { return _interaction_zones; };
            ZoneHandle RegisterInteractionZone(Vector4f rect);
            void UnRegisterInteractionZone(ZoneHandle handle);
            void UpdateInteractionZone(ZoneHandle handle, Vector4f rect);
        public:
            UIElement *_capture_target = nullptr;//记录按下时的目标,全局共享，element销毁时检查这个值
            UIElement *_focus_target = nullptr;  //记录按下时的目标,全局共享，element销毁时检查这个值
            UIElement *_hover_target = nullptr;
            UIElement *_debug_highlight_target = nullptr;
            bool _is_debug_reflector_visible = false;
            Widget *_pre_hover_widget = nullptr; //记录上一帧鼠标停留的控件,全局共享，widget销毁时检查这个值
            //sort order大的在后面
            Vector<Ref<Widget>> _widgets;

        private:
            void ApplyFocusChange(UIElement *old_f, UIElement *new_f);
            bool IsElementInWidget(UIElement *element, Widget *widget) const;
            void CleanupWidgetState(Widget *widget);

        private:
            UILayer *_ui_layer;
            UIRenderer *_renderer;
            UITheme *_theme = nullptr;
            struct PopupEntry
            {
                Ref<Widget> _widget;
                std::function<void()> _on_close;
            };
            Vector<PopupEntry> _popup_stack;
            Vector<Ref<Widget>> _pending_popup_destroy;
            Vector<Ref<UIElement>> _pending_destroy;

            Vector<InteractionZone> _interaction_zones;
            Vector<u32> _free_indices;

        };
    }// namespace UI

}// namespace Ailu

#endif//AILU_UIFRAMEWORK_H
