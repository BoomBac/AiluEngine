//
// Created by 22292 on 2024/10/28.
//
#include "UI/UIFramework.h"
namespace Ailu::UI
{
    static UIManager* g_pUIManager = nullptr;
    //----------------------------------------------------------------------------------------UIManager-----------------------------------------------------------------------------
    void UIManager::Init()
    {
        g_pUIManager = new UIManager();
    }
    void UIManager::Shutdown()
    {
        DESTORY_PTR(g_pUIManager);
    }
    UIManager *UIManager::Get()
    {
        return g_pUIManager;
    }

    UIManager::UIManager()
    {
    }
    UIManager::~UIManager()
    {
    }
    void UIManager::AddElement(UIElement *element)
    {
    }
    void UIManager::RemoveElement(UIElement *element)
    {
    }
    UIElement *UIManager::FindElement(u32 id) const
    {
        return nullptr;
    }
    UIElement *UIManager::FindElement(String &name) const
    {
        return nullptr;
    }
    void UIManager::DispatchEvent(const Event &event)
    {
    }
    void UIManager::Update(float dt)
    {
    }
    void UIManager::Render()
    {
    }
    void UIManager::SetSkin(const UISkin &skin)
    {
        _skin = skin;
    }
    UISkin& UIManager::GetSkin()
    {
        return _skin;
    }


}// namespace Ailu::UI
