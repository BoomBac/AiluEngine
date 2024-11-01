//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIFRAMEWORK_H
#define AILU_UIFRAMEWORK_H

#include "Framework/Events/Event.h"
#include "GlobalMarco.h"
#include "UIElement.h"
namespace Ailu::UI
{
    struct UISkin
    {

    };

    class UIManager
    {
    public:
        static void Init();
        static void Shutdown();
        static UIManager* Get();
    public:
        UIManager();
        ~UIManager();
        void AddElement(UIElement* element);
        void RemoveElement(UIElement* element);
        [[nodiscard]] UIElement* FindElement(u32 id) const;
        [[nodiscard]] UIElement* FindElement(String& name) const;
        void DispatchEvent(const Event& event);
        void Update(f32 dt);
        void Render();
        void SetSkin(const UISkin& skin);
        UISkin& GetSkin();
    private:
        // 根UI容器，所有UI元素的父节点
        UISkin _skin;
    };

}

#endif//AILU_UIFRAMEWORK_H
