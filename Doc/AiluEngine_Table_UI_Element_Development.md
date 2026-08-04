# AiluEngine Table UI Element 开发方案

## 1. 功能目标

为 AiluEngine Editor UI 系统增加通用表格控件（Table Element），用于：

-   Inspector 属性列表展示
-   Asset Browser 数据展示
-   Entity / Component 列表
-   Debug 信息面板
-   性能统计数据展示

目标：

-   保持现有 UIElement 架构一致
-   接入已有 Style 系统
-   支持反射系统自动生成数据
-   支持滚动、排序、选择、列调整
-   支持大数据量虚拟化渲染

# 2. 总体设计

Table 不采用大量 UIElement 堆叠：

错误方案：

    Table
     └── Row UIElement
          └── Cell UIElement

问题：

-   数据量大时对象数量爆炸
-   Layout 开销高
-   Editor 数据展示性能差

采用：

    UITableElement

        |
        +-- TableDataSource

        |
        +-- TableRenderer

        |
        +-- UITableStyle

核心原则：

-   数据与 UI 分离
-   样式与控件逻辑分离
-   Cell 不创建独立 UIElement
-   使用虚拟化渲染

# 3. 类结构

## 3.1 UITableElement

``` cpp
class UITableElement : public UIElement
{
public:

    void SetDataSource(TableDataSource* source);

    void AddColumn(const TableColumn& column);

    void SortByColumn(int column_index);

    void SetSelectedRow(int row);

protected:

    void OnLayout() override;

    void OnRender() override;


private:

    Ref<TableDataSource> _data_source;

    Vector<TableColumn> _columns;

    Vector<int> _selected_rows;

    Vector2f _scroll_offset;

    Ref<UITableStyle> _style;
};
```

职责：

-   管理表格布局
-   管理滚动
-   管理选择
-   调用 Renderer 绘制

# 4. TableColumn

``` cpp
struct TableColumn
{
    String _name;

    float _width = 100.0f;

    bool _sortable = false;

    bool _resizable = true;

    ETableColumnAlignment _alignment;
};
```

支持：

  字段        说明
  ----------- ------------------
  name        列标题
  width       列宽
  sortable    是否支持排序
  resizable   是否支持拖动调整
  alignment   文本对齐

# 5. 数据模型

Table 不保存数据，使用 DataSource。

``` cpp
class TableDataSource
{
public:

    virtual int GetRowCount() const = 0;

    virtual String GetCellText(
        int row,
        int column) const = 0;

    virtual void DrawCell(
        int row,
        int column,
        Rect rect);

    virtual void Sort(
        int column,
        bool ascending);
};
```

优势：

-   支持 Object Reflection
-   支持 Asset 数据
-   支持 ECS 数据
-   支持动态刷新

# 6. 虚拟化渲染

只绘制可见行。

例如：

窗口高度：

    600px

行高：

    22px

实际：

    600 / 22 ≈ 27 rows

只渲染：

    visible_start_row
    ~
    visible_end_row

# 7. Renderer 设计

绘制流程：

    UITableElement::OnRender()

        |
        +-- DrawBackground()

        |
        +-- DrawHeader()

        |
        +-- CalculateVisibleRows()

        |
        +-- DrawVisibleRows()

                |
                +-- DrawCell()

Cell 不创建 UIElement：

    TableRenderer

        +-- DrawText
        +-- DrawImage
        +-- DrawCheckbox
        +-- DrawProgressBar

# 8. Style 系统接入

Table 必须接入 AiluEngine UI Style 系统。

不要在代码中保存：

``` cpp
Color _row_color;
Color _header_color;
float _padding;
```

全部进入 Style。

# 9. UITableStyle

``` cpp
struct UITableStyle
{
    UIControlVisual _background;

    UIControlVisual _header;

    UIControlVisual _row;

    UIControlVisual _row_hovered;

    UIControlVisual _row_selected;

    UIControlVisual _cell;


    float _row_height = 22.0f;

    float _header_height = 24.0f;

    float _border_width = 1.0f;
};
```

复用已有：

    UIControlVisual
    ResolveStyle
    UITheme

# 10. Theme 集成

扩展：

``` cpp
class UITheme
{
public:

    UIButtonStyle _button;

    UITableStyle _table;

    UITextStyle _text;
};
```

使用：

``` cpp
table->SetStyle(
    UIManager::GetTheme()->_table);
```

支持：

    Global Theme

          |

    Default Table Style

          |

    Instance Override

# 11. Row 状态

``` cpp
enum class ETableRowState
{
    Normal,
    Hovered,
    Selected,
    SelectedHovered,
    Disabled
};
```

通过 Style Resolve 获取视觉：

``` cpp
auto visual =
    style.ResolveRowVisual(state);
```

# 12. Reflection 集成

利用已有 Reflection 系统。

例如 Object Detail：

    Object

     |

    Reflection

     |

    Property List

     |

    TableDataSource

     |

    UITableElement

展示：

  Property   Type      Value
  ---------- --------- -----------
  Position   Vector3   (0,1,0)
  Visible    bool      true
  Color      Color     (1,1,1,1)

无需新增绑定系统。

# 13. Editor 使用场景

## Object Detail

替换大量手写组件绘制：

    ComponentTable

    Columns:

    Component
    Enabled
    Status

## Asset Browser

    Name
    Type
    Size
    Modified

## Render Debug

    Pass
    GPU Time
    Resource

# 14. 文件结构建议

    Runtime/UI/

        Element/

            UITableElement.h
            UITableElement.cpp


        Table/

            TableColumn.h
            TableDataSource.h
            TableRenderer.h
            UITableStyle.h


    Editor/UI/

        ObjectPropertyTable.h
        AssetTableDataSource.h

# 15. 实现阶段

## Phase 1 基础

-   UITableElement
-   TableColumn
-   TableDataSource
-   Style 接入
-   文本 Cell
-   滚动

## Phase 2 Editor

-   Object Detail
-   Asset Browser

## Phase 3 高级

-   列宽调整
-   排序
-   多选
-   虚拟列表
-   自定义 Cell

# 16. 注意事项

## 不创建大量 UIElement

不要：

    10000 rows
    *
    columns
    *
    UIElement

采用：

    1 UITableElement

    +

    visible cells rendering

## 数据生命周期

    Table

     |

    DataSource

     |

    Object / Asset / ECS

Table 不拥有业务数据。

# 最终架构

                        UITheme

                           |

                     UITableStyle

                           |

                     UITableElement

                  /                    \

        TableDataSource          TableRenderer

                  |                    |

           Reflection Data       Style Resolve

UITableElement 是 AiluEngine Editor
数据展示基础控件，而不是简单表格绘制组件。
