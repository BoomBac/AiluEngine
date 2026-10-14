# M2 — Demand-Driven UI Update

> 先决 Gate：M1 已通过，Dock 业务更新与 Layout 调度无重复执行。此阶段专注 **CPU UI 树更新与数据同步**，不改最终渲染目标为 Texture Cache。

## Purpose

将可以保持静态的 Retained UI 从“每帧遍历所有可见节点”改为“变化发生才遍历必要子树”，同时明确哪些控件/面板需要持续 Tick 或即使隐藏仍要推进后台工作。

## Scope

- 在现有 `Widget` / `UIElement` 上引入**渐进、可审计、默认保守**的按需更新策略。
- 用 Dirty/Active-Tick 信号跳过静态子树的 `PreUpdate` / `Update`，利用现有 `InvalidateLayout/Transform/Style/Hierarchy/Paint`。
- 减少 Inspector、Hierarchy、AssetBrowser、EditorChrome 的无变化数据刷新与字符串处理。
- 处理 Focus、Hovered、Pressed、输入法/文本光标、动画、轮询资源到位等定时唤醒场景。
- 使 Debug Reflector 能显示 Skip/Executed、Active Tick 原因，保证优化可解释。

## Non-goals

- 不要求一次性把所有控件都改成事件驱动；不能安全证明的窗口维持 `EveryFrame`。
- 不引入通用 Observer Framework 或重构 ECS 组件数据模型；已有 Scene/Selection Revision 优先复用。
- 不依赖“Paint 没有 Dirty”直接停掉所有 Update——**Update 本身可能产生 Paint Dirty**。
- 不通过冻结动态 RenderTexture、暂停 SceneView 相机或延迟输入事件换取虚假收益。

## Architecture Constraints

1. **默认保守**：未知/第三方/插件自定义 `UIElement::Update` 和 `PreUpdate` 的虚函数副作用无法静态证明时，默认 `kEveryFrame`；只将审计过的静态控件/树显式 opt-in `kOnDemand`。
2. **不同 Dirty 语义**：Paint Dirty 只要求重建绘制；Layout/Transform/Style/Hierarchy Dirty 可能需要 Update/Measure/Arrange；事件或外部数据变化可以主动 `RequestUpdate`。不得把两者粗暴合并成单一 `bool dirty`。
3. **活动计时器**：输入框插入符闪烁、滚动惯性、Slider 拖动、动画/过渡、动态 Preview 要么保留 EveryFrame，要么有安全的定时/事件唤醒；第一版可选择 EveryFrame，**不必做复杂 Scheduler**。
4. **传播正确**：子节点 `RequestUpdate` / `Invalidate*` 可唤醒祖先/Widget；子树没有活动 Tick 且无 Update Dirty 时才能跳过。避免每帧扫描子树只为判断是否能跳过。
5. **生命周期**：添加/删除 Child、Widget 注册/销毁、Popup 创建/关闭、Focus 转移、主题/DPI/尺寸变更必须在同帧或下一合法渲染帧触发更新；无悬空 active Tick 指针。
6. **线程归属**：UI 树变更仍在 Edit/UI 主执行路径完成；异步资源/后台任务只通过现有跨线程队列在正确线程触发 invalidation，不直接并发改 UI 节点。

## Baseline

- `UIRenderer::Prepare()` 当前依次处理所有 visible Widget 的 `PreUpdate()` 和 `Update()`；二次扫描处理本帧新增 Widget。
- `UIElement::Update()` 仅在 `_is_layout_dirty` 时 Measure/Arrange，但通常仍递归所有可见子节点，并每帧 `EnsureStyleResolved()`。
- `InputBlock::Update()` 用 `dt` 推进光标闪烁，焦点内会间歇 `InvalidatePaint()`。
- `WorldOutline` 已用 `Scene::StructureRevision/EditRevision` 和 `Selection::Revision()` 判变。
- `AssetBrowser` 有 `_content_dirty` / `_layout_dirty`，但还存在异步预览 Revision 和后台导入 Tick。
- `ObjectDetail::Update()` 枚举 Selection，之后遍历 Component Editor 的 `NeedsRebuild/Refresh`；不保证每类属性变化都有可靠 Revision 通知。
- `SceneView::Update()` 包括 Preview、相机、Gizmo、工具栏及 Drag 状态；不能当作纯静态控件。

## Target Outcome

- 对选定的静态 Widget，在无事件/数据变化的稳定帧中 `UIElement::Update()` 访问次数显著下降，Layout/Style 结果不变。
- Inspector 等不能完全事件化的面板合理回退为每帧或低频更新，**以正确性优先**。
- Input/Focus/Popup/动画/异步资源 ready 后仍正确更新，且不会出现空白/滞后的可见 UI。
- M1 的 Paint Cache Hit 不回退，M0/M1 的 CPU Frame P50/P95 有可量化对照。

## Relevant Files

- `Engine/Inc/UI/UIElement.h`, `Engine/Src/UI/UIElement.cpp`
- `Engine/Inc/UI/Widget.h`, `Engine/Src/UI/Widget.cpp`
- `Engine/Inc/UI/UIRenderer.h`, `Engine/Src/UI/UIRenderer.cpp`
- `Engine/Src/UI/Basic.cpp`, `Engine/Src/UI/UIFramework.cpp`, `Engine/Src/UI/Container.cpp`, `Engine/Src/UI/TreeView.cpp`
- `Editor/Src/Widgets/EditorLayer.cpp`, `ObjectDetail.cpp`, `WorldOutline.cpp`, `AssetBrowser.cpp`, `RenderView.cpp`
- `Editor/Src/Inspector/ComponentEditors/*.cpp`, `Editor/Src/Dock/DockWindow.cpp`

## Implementation Steps

### 2.1 列出所有 UI 生命周期副作用（先扫描，再 opt-in）

在当前工作树检索 `UIElement::PreUpdate/Update/PostUpdate` override、`DockWindow::Update` override 和自定义 `Tick`：

- 分类 `Static`（无时间依赖）、`Interactive`（焦点/输入依赖）、`Animated`（依赖 dt）、`PolledData`（依赖外部 Revision 或轮询）、`BackgroundService`（隐藏时需执行）。
- 不需要对未知脚本/插件系统做静态猜测：未验证的始终 EveryFrame。
- 把审计结果简表记录到 `progress.md`；列出首批 opt-in 的 Widget 名单和留在 EveryFrame 的名单。

### 2.2 添加最小更新门控

建议允许如下**概念状态**，实现时复用现有变量、避免创建过度复杂的类层级：

```cpp
enum class EUIUpdateMode : u8
{
    kEveryFrame,
    kOnDemand
};

// 仅作说明：具体存储可在 Widget/UIElement，视已有层次选择。
// Widget 默认 kEveryFrame；已审计无时间副作用的 Widget 或纯静态子树才 kOnDemand。
// RequestUpdate() 传播祖先脏更新，InvalidatePaint() 保留原有渲染失效职责。
```

最小可交付方案：

1. **先 Widget 粒度**：`Widget::NeedsUpdate()` 在模式 EveryFrame、布局/样式/事件更新 Dirty、活动子节点需要 Tick 时返回 true；仅认证过的静态 Widget opt-in kOnDemand。
2. **再局部子树跳过**：如果 M0/M1 表明 UIElement 递归成本高，则增加 `_subtree_update_dirty` + `_subtree_requires_tick`（或等效引用计数/版本），由子节点向上标记。没有 Dirty/Active Tick 的子树快速跳过；保留 EveryFrame 节点及其祖先可达路径。
3. 提供轻量 `RequestUpdate()`（函数名按项目风格 CamelCase），而不是把每个更新原因都伪装成 `InvalidatePaint()`；任何会影响绘制的外部数据变化仍调用现有 Invalidate API。
4. `UIRenderer::Prepare()` 在 `PreUpdate/Update` 阶段尊重 Gate；本帧新建 Widget 的第二轮更新流程保持，不能因为先前列表快照跳过。
5. 只在完成一轮实际更新后清除对应 Update Dirty；若 Update 中再次请求调度下帧，不能丢标记（必要时用 version 递增或交换待处理标志）。

**特别约束**：对基类 `UIElement` 的 `Update` 递归剪枝，不能在父节点“静态”判断为真时误跳过需要每帧 Tick 的子控件；祖先必须知道活动 Tick 子树存在。若无法低侵入保证，先只做 Widget Gate 并记录后续子树优化，不要冒险全局跳过。

### 2.3 动态控件及输入正确性

- `InputBlock` 聚焦期间每帧 Tick 或有定时唤醒（Blink），失焦后恢复 OnDemand；保持输入框光标、选择区及 IME 行为。
- Hover、Press、Focus、DragDrop、滚轮滚动、滚动惯性、展开折叠、Popup 交互需立刻 RequestUpdate 或适当 Invalidate。
- 样式主题切换、字体替换、DPI / WindowResize 必须全局唤醒对应 Widget；即使缓存命中也不能沿用过期 Style/Layout。
- Gizmo、Viewport、Debug 动态覆层保持 EveryFrame；背景图采用 RenderTexture 指针复用不意味着像素无变化。

### 2.4 Editor 面板的业务刷新解耦

| Panel | 第一批策略 | 数据失效触发 | 注意 |
| --- | --- | --- | --- |
| EditorChrome 工具栏 | 变更驱动 | 窗口尺寸、Scene/Selection、运行模式/配置变化 | FPS 数字单独低频；不要拖慢按钮响应 |
| WorldOutline | `Revision` 驱动 | Scene structure/edit、Selection revision、搜索和展开状态 | 已有机制，优先复用 |
| AssetBrowser | Dirty 驱动 | 文件变化、导航、预览 Revision、尺寸/滚动 | `_import_controller.Tick()` 为后台工作，不与可见 UI gate 绑定 |
| ObjectDetail | **先保守 EveryFrame**，再逐组件试点 | Selection、Scene change、Structure Revision、经验证的 Property Change/Runtime Refresh | 未确认 Transform/Light 动态变更 Revision 完备前，不得全局停止 Refresh |
| SceneView/GameView | EveryFrame | Camera/Gizmo、运行时 RT、输入、动态 Overlay | 仅内部纯静态 Toolbar 可考虑独立 Widget 后缓存 |
| Popup/Tooltip | Event/Active Tick 混合 | 展示、输入、动画、关闭 | 本帧弹出不能落后一帧 |

审计 ComponentEditor `NeedsRebuild` 与 `Refresh` 语义；在属性变更通知完整前只能选择跳过**隐藏页面**刷新或优化局部字符串构建，不可因为 SelectedEntity 未变化就永远不刷新 Inspector。

### 2.5 减少迭代过程本身的成本

- M0 若发现 `UIRenderer::Prepare` 反复 `std::find` / 小 Vector 分配占比高，在现有容器上进行复用与预留；避免为低收益改用复杂 lock-free registry。
- 将多处重复样式 getter、`std::format`、Selection 快照构建局限于确有数据变化路径。
- 关闭高成本的无用 Debug 计数、树追踪，在正式渲染配置下重测；不改变可观测性开关行为。

## Validation

- [ ] 构建、`git diff --check`，静态 UI、嵌套 Dock、Popup、文本输入和场景回归全部 PASS。
- [ ] 指定 OnDemand 静态 Widget 在稳定帧其业务 Tick/子树访问计数趋近零，仍能在真实事件后正确唤醒。
- [ ] InputBlock 光标按原闪烁周期刷新；鼠标 Hover/按压、拖动滑条、滚动惯性和键盘导航不丢更新。
- [ ] Widget 在本帧新创建/注册后正常绘制；Widget 删除、Popup 销毁、主题和尺寸修改没有非法访问/状态残留。
- [ ] AssetBrowser 后台导入、预览异步加载、EditorProgressService、自动化相关后台状态正确；显示恢复后 UI 最新。
- [ ] ObjectDetail 对 Transform 动画与动态属性变更能及时显示；不可靠的 Revision 场景保留 EveryFrame fallback 并在 `progress.md` 明确记录。
- [ ] 与 M1 对照 CPU Update/Prepare P50/P95、Layout/Element Visit 与 Cache Hit/Miss；记录降低百分比和可能退化。
- [ ] Gate PASS 后更新 `progress.md`，**M3 才允许启动**。

## Milestones

本文件仅 M2，不能把 M3 的 GPU RT 引进本阶段。

## Current Milestone

进入本文件时应由 `progress.md` 指向 M2。

## Progress

动态状态仅写 `progress.md`。

## Decision Log

- 默认 `EveryFrame`，只对已审计静态路径 opt-in；降低副作用与未来插件回归风险。

## Surprises & Discoveries

自定义 UI Virtual Update、Editor Component Refresh 的副作用可能多于表面代码；未知来源先维持旧行为并记录。

## Known Follow-ups

更细分的事件总线/组件属性 Revision、计时器唤醒、单元素 Paint Cache 等只有在数据证明值得做时才另立计划。

## Outcomes

待 `progress.md` 实测。

## Resume Protocol

读 README + progress + 本文件；逐小步审计并 Opt-in，验收不过不得进入 M3。
