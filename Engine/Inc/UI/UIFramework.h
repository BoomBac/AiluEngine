//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIFRAMEWORK_H
#define AILU_UIFRAMEWORK_H

#include "GlobalMarco.h"

namespace Ailu
{
    class Window;
    namespace UI
    {
        class Widget;
        class UILayer;
        class UIRenderer;
        class UIElement;
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
            // 新增：设置 / 清除 / 获取 当前焦点元素
            void SetFocus(UIElement *element);
            void ClearFocus(UIElement *element = nullptr);
            UIElement *GetFocusedElement() const { return _focus_target; }
            //弹出一个popup widget,位置基于当前窗口左上角，root则会被添加到popup widget的root(canvas)进行显示
            void ShowPopupAt(f32 x, f32 y, Ref<UIElement> root, std::function<void()> on_close = nullptr, Window *win = nullptr);
            void HidePopup();
            Widget *GetPopupWidget() const { return _popup_widget; }
            void Destroy(Ref<UIElement> element);
            void OnElementDestroying(UIElement *element);
        public:
            UIElement *_capture_target = nullptr;//记录按下时的目标,全局共享，element销毁时检查这个值
            UIElement *_focus_target = nullptr;  //记录按下时的目标,全局共享，element销毁时检查这个值
            UIElement *_hover_target = nullptr;
            Widget *_pre_hover_widget = nullptr; //记录上一帧鼠标停留的控件,全局共享，widget销毁时检查这个值
            //sort order大的在后面
            Vector<Ref<Widget>> _widgets;

        private:
            void ApplyFocusChange(UIElement *old_f, UIElement *new_f);

        private:
            UILayer *_ui_layer;
            UIRenderer *_renderer;
            Widget *_popup_widget;
            std::function<void()> _on_popup_close;
            Vector<Ref<UIElement>> _pending_destroy;
        };
    }// namespace UI

}// namespace Ailu

#endif//AILU_UIFRAMEWORK_H
