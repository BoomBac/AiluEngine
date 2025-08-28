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
    enum class ECursorType
    {
        kArrow,
        kSizeNS,  // 上下
        kSizeEW,  // 左右
        kSizeNESW,// ↘↖ 对角
        kSizeNWSE,// ↗↙ 对角
        kHand
    };


    class AILU_API UIManager
    {
    public:
        static void Init();
        static void Shutdown();
        static UIManager* Get();
    public:
        UIManager();
        ~UIManager();
        void SetCursor(ECursorType type);
    private:
    };

}

#endif//AILU_UIFRAMEWORK_H
