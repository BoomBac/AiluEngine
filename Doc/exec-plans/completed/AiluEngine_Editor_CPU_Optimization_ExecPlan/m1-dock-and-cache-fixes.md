# M1 — Dock & Cache Quick Wins

> 先决 Gate：M0 已通过并且 `progress.md` 中存在可复验基线。阶段目标是消除明确的无效工作，**不引入新的按需 Tick 框架或 RT 资源**。

## Purpose

修正重复 Dock 更新、无效布局计算、隐藏 Tab 重型刷新、冗余 Paint Dirty 和空 Widget 重建，让原有 Retained UI 缓存先稳定、高效运转。

## Scope

- 正规化 Dock `UpdateLayout → Update` 的调用关系，让一帧的布局/业务更新执行次数可预期。
- 阻止非活动 Tab 执行昂贵 UI 业务更新，同时保留必要后台任务。
- 所有可判定的重复 Visibility Dirty 加同值检查。
- 避免无变化 `Arrange()` 的矩阵/角点重新计算，保证首次布局与失效后仍计算。
- 明确空 Paint Cache 是合法有效缓存状态。
- 微调 EditorChrome 无变化布局调用、FPS 文案按低频刷新（可在 M2 改由调度器驱动）。

## Non-goals

- 不在 M1 给 UIElement 引入全新的 Node Tick Policy、Subtree Dirty 框架。
- 不将每个 Dock 对象放入独立 Job，保持现有线程语义。
- 不停用 SceneView/GameView、Asset Import、Automation、Plugin 状态推进等后台工作。

## Architecture Constraints

1. 仅在**已确认不需要**后才跳过业务 `Update`；隐藏内容可能有自定义副作用，先梳理更新类及后台任务。
2. 调整 `UpdateLayout` 逻辑后，一帧内 Popup 创建、Dock 树修改和 Tab 激活后必须能正确走 `SetRect`、Layout 和可见 Widget 更新。
3. `UIElement::Arrange` 的快速路径必须检查 `_is_layout_dirty`、`_is_transf_dirty`、新旧矩形与相关样式状态，不允许挡住恢复布局/父级 Transform 的结果。
4. `SetVisible/SetTabActive` 同值操作不产生新 Dirty；真变化仍保证 Paint Cache 和输入焦点正确失效。
5. 空 Widget 的“缓存有效”独立于有无 `_nodes`；资源未 ready 的 `_cache_build_pending_resource` 状态不能误缓存成永久空画面。

## Baseline

M0 所确认的关键调用路径：

```text
DockManager::Update
  └─ DockNode::UpdateLayout
       └─ DockTab::Update              // 当前 Tab 路径存在业务 Update
            └─ all tab items: SetRect + Update
  └─ DockNode::Update
       └─ DockTab::Update              // 当前又执行一次
            └─ DockNodeTabItem::Update
                 └─ nested UpdateLayout + Update
```

`DockTab::SetVisible()`、`DockWindow::SetTitleBarVisibility()`、`SetContentVisibility()` 会无条件 `InvalidatePaint`；`ResizeWidgetRoot()` 始终调用 `Root()->Arrange`；`UIElement::Arrange` 当前始终执行矩阵求逆和坐标变换。

## Target Outcome

- 无 Dock 改动的一帧中，各实际可见 Dock 内容业务更新次数为一次（或按明确的特殊调度契约执行），不因嵌套 Tab 被隐式放大。
- 非活动 Tab 不进行重复的 UI 布局/业务刷新；后台资产导入与任务仍可推进。
- 无变化 Widget 不会因为反复可见性设定和 Layout 调用持续产生 Paint Miss。
- 有效空 Widget 在已构建且无资源待就绪时命中缓存；真实 Dirty/资源变更仍会重建。

## Relevant Files

- `Editor/Src/Dock/DockManager.cpp`, `Editor/Src/Dock/DockWindow.cpp` 与各自 Header
- `Engine/Src/UI/UIElement.cpp`, `Widget.cpp`, `UIRenderer.cpp`
- `Editor/Src/Widgets/EditorLayer.cpp`, `AssetBrowser.cpp`, `ObjectDetail.cpp`, `WorldOutline.cpp`, `RenderView.cpp`
- `Editor/Src/Widgets/EditorProgressPanel.cpp`（如后台进度调度需要）

## Implementation Steps

### 1.1 统一 Dock Layout/Update 语义

- 整理 `DockNode::UpdateLayout`：**只负责位置/尺寸传播、布局和必要 Layout Dirty**，不隐式执行业务 `DockTab::Update`。
- 整理 `DockNode::Update`：每帧至多对其活动子树执行业务 Update 一次，不在不同层级无条件重复调 Layout。
- `DockNodeTabItem::SetRect` 采用真正的“矩形变更”判断；只有变化才传播子树布局（或标记延后）；`Update` 只执行已确定的当前帧业务逻辑，消除冗余 `UpdateLayout`。
- 检查 `DockTab::Update()` 的 `_tabs` 迭代：隐藏 Item 通常**不更新内容**；可见/选中 Item 正常 Tick；Tab 标题栏仍独立更新其必要状态。
- 注意 `DockWindow` 标题栏与内容 Widget 是独立对象，Tab 内容隐藏时不可造成标题栏状态误更新/交互穿透。
- 添加一帧调用次数断言或调试计数：连续静止帧、嵌套 Dock、切换 Tab、Resize，每个活动 Item 的 Update 恰好符合预期；Layout 可以因事件在帧内触发，但不应由循环结构无条件重复。

### 1.2 隐藏 Tab 的后台任务分离

审计至少以下窗口（按当前实际代码逐项确认）：

| 窗口 | 隐藏后是否需要完整 UI Update | 必须保持的行为 |
| --- | --- | --- |
| `AssetBrowser` | 否 | `_import_controller.Tick()` 等导入与完成通知需要可继续处理 |
| `ObjectDetail` | 否 | Selection/组件改变后，恢复显示时必须重新同步；不必隐藏时每帧 Refresh |
| `WorldOutline` | 否 | 恢复显示时从 `StructureRevision`、`EditRevision`、Selection Revision 同步 |
| `SceneView` / `GameView` | **先审计**，默认保持活动/必要 Tick | 相机输出、场景/预览 RT、Editor Gizmo、相机控制与外部预览引用不能被无意停掉 |
| `EditorProgressPanel` 等 | 后台状态继续更新 | 不能因窗口隐藏使任务停止或 Progress 错报 |

最小实现方式：在有隐藏后台工作的 DockWindow 提供轻量 `TickBackground(dt)`（可用明确接口/回调，不必为所有窗体创建新的大基类），由 DockManager 稳定调用；**不要**用“隐藏的所有 Update 永久停掉”破坏业务。按需补激活时 `NeedsRefresh`，先选少量高收益窗口试点。

### 1.3 抑制重复失效

- `DockTab::SetVisible`：同值快速返回，仅状态变化时通知 `_tab_bar` 与 `_tabs[i]->SetTabActive`。
- `DockWindow::SetTitleBarVisibility`：比较 `is_visibility` + 扩展内容策略；同值不再修改 Dirty。
- `DockWindow::SetContentVisibility`：比较当前 `_visibility`，真变化才失效。
- 检查 `OnActiveTabChanged` 不要因焦点重入造成永远反复 Dirty；保持 SetFocus/BringToFront 语义。
- 用已有 `_invalidation_count`、`_last_invalidation_source` 与 UI Reflector 验证静态帧没有无原因失效。

### 1.4 Layout 与 Paint 快速路径

- `UIElement::Arrange`：只有新矩形不同、Layout Dirty 或 Transform Dirty 时才计算世界矩阵、逆矩阵和绝对矩形；保持 `PostArrange` 触发条件与首次 Arrange 行为。
- 确保 `InvalidateTransform` 子树传播、父节点重新布局、DPI/Theme 切换、拖动 Dock 的矩阵和 HitTest 不发生旧坐标残留。
- `EditorLayer::ResizeWidgetRoot`：位置/尺寸与 Root 已布局状态无变化时不再重复 Arrange；初始化和修改后必须一致。
- `EditorLayer::UpdateEditorChrome`：避免每帧重复构建 `Scene/Selection` 相关字符串；FPS/FrameTime 建议 200–250 ms 更新一次，使用立即事件更新标题/选择/状态，不阻塞实际帧率统计。

### 1.5 空缓存有效性修正

- `UIRenderer::Prepare`：用明确的 `cache_built` / `build_revision` / frame-valid 状态区分“从未构建”“成功构建但无节点”“构建依赖还未 ready”。
- 去除以 `_nodes.empty()` 作为**强制每帧重绘的唯一理由**；缓存已经有效且无 Dirty 的空 Widget 要命中。
- 资源待就绪时继续尝试或在 Ready 事件触发时失效，不允许永久显示空白。
- 覆盖清空 Child、添加 Child、隐藏/显示、异步纹理到位、Widget 销毁并创建等测试。

## Validation

**每项功能必须复验，同时附 M0 对照性能数据：**

- [ ] 同配置构建和 `git diff --check` PASS。
- [ ] 对单层与嵌套 Tab 的 Update 调用次数进行断言/计数；静止无变更帧不再因调用链重复触发业务更新。
- [ ] 隐藏 AssetBrowser 时导入任务仍执行并完成；切回时内容正确刷新，不需要重启 Editor。
- [ ] SceneView、GameView、Inspector、WorldOutline 在换 Tab、恢复、切 Scene、Selection 变化后准确显示。
- [ ] Dock 拖拽/浮动/尺寸/焦点/第二窗口/关闭/Popup 的区域与输入命中正确，旧画面不残留。
- [ ] 静态 UI 的 Paint Cache Miss 和 Unchanged Arrange Count 相比 M0 下降；若未改善，说明触发来源。
- [ ] 空 Widget 在暖机后 Miss 为零（除真实 Dirty/资源等待），新增内容又能及时渲染。
- [ ] 关键 CPU P50/P95 与 M0 比较，不出现未经解释的明显退化；记录收益大小，不用理论预测替代。
- [ ] `progress.md` 记录实际变更文件、调用次数、耗时、Gate PASS，方可进入 M2。

## Milestones

仅 M1，独立验收；M2 不得提前混入。

## Current Milestone

进入本文件时应为 M1（由 `progress.md` 更新）。

## Progress

动态状态统一写入 `progress.md`。

## Decision Log

- 预设：优先拆分 Layout/Update；如果存在特殊依赖，改成显式一次性调度而非继续隐式双重调用。

## Surprises & Discoveries

M0 采样如果证明此段非热点，仍修正功能风险，但避免因此无端扩张改动面。

## Known Follow-ups

更细粒度的子树 Tick/Property Revision 由 M2 处理；Draw Submission 的 RT 优化归 M3。

## Outcomes

待执行者填写 `progress.md`。

## Resume Protocol

只读取 README、progress 和本文件；每一步留下构建/回归证据，Gate 不通过不推进 M2。
