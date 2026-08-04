#pragma once
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"

namespace Ailu
{
    namespace UI
    {
        class UIRenderer;

        /// <summary>
        /// Table 的数据源，Table 本身不持有业务数据。
        /// 支持 Object Reflection / Asset / ECS 等任意数据来源。
        /// </summary>
        class TableDataSource
        {
        public:
            virtual ~TableDataSource() = default;

            virtual i32 GetRowCount() const = 0;

            virtual String GetCellText(i32 row, i32 column) const = 0;

            /// <summary>
            /// 返回 true 时由数据源完全负责单元格绘制，Table 不再叠加默认文本。
            /// </summary>
            virtual bool DrawCell(UIRenderer &r, i32 row, i32 column, Vector4f rect) { return false; }

            virtual void Sort(i32 column, bool ascending) {}
        };
    }// namespace UI
}// namespace Ailu
