#pragma once
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

namespace Ailu
{
    namespace UI
    {
        enum class ETableColumnAlignment : u8
        {
            kLeft,
            kCenter,
            kRight
        };

        struct TableColumn
        {
            String _name;
            f32 _width = 100.0f;
            bool _sortable = false;
            bool _resizable = true;
            bool _stretch = false;// 参与伸缩分配剩余宽度，最后一个伸缩列吸收余量
            ETableColumnAlignment _alignment = ETableColumnAlignment::kLeft;
        };
    }// namespace UI
}// namespace Ailu
