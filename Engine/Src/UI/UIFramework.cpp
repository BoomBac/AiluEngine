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

    void UIManager::SetCursor(ECursorType type)
    {
#if PLATFORM_WINDOWS
        HCURSOR cursor = nullptr;
        switch (type)
        {
            case ECursorType::kArrow:
                cursor = LoadCursor(NULL, IDC_ARROW);
                break;
            case ECursorType::kSizeNS:
                cursor = LoadCursor(NULL, IDC_SIZENS);
                break;
            case ECursorType::kSizeEW:
                cursor = LoadCursor(NULL, IDC_SIZEWE);
                break;
            case ECursorType::kSizeNWSE:
                cursor = LoadCursor(NULL, IDC_SIZENWSE);
                break;
            case ECursorType::kSizeNESW:
                cursor = LoadCursor(NULL, IDC_SIZENESW);
                break;
            default:
                cursor = LoadCursor(NULL, IDC_ARROW);
                break;
        }
        ::SetCursor(cursor);
#endif// PLATFORM_WINDOWS
    }


}// namespace Ailu::UI
