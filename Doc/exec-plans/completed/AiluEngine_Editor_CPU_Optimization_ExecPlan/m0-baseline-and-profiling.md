# M0 — Baseline & Diagnostics（当前 Milestone）

> **目的**：先证明空闲 Editor CPU 时间花在哪里，建立任何后续改动都可复验的基线。此阶段只允许轻量统计/测试与确定性诊断修正，**禁止顺手做 Dock 逻辑和 RT 缓存重构**。

## Purpose

建立精确到 UI 子系统阶段、重点 DockWindow/Widget 的 Profile 数据，区分 Editor UI 的 CPU 计算、Draw Submission、渲染线程/Present 等待；为 M1/M2/M3 定义性能阈值与优先级。

## Scope

- 采集 Window/Dock/Widget 数量、各类 Update 次数、布局数量、Paint/Texture Cache 统计；P50/P95 CPU 耗时。
- 扩充现有 `UIRenderStats`，只为确实缺失的关键阶段加数据；复用 Tracy/VS Profiler/现有 UI Reflector，避免每节点耗时都上锁或分配。
- 覆盖静止空闲、Tab、Dock/Popup、SceneView 动态、ImGui Debug、双窗口；保证固定采样设置、可比较。
- 记录工具版本、CPU/GPU、应用线程/帧率策略、是否 RenderGraph/并行 UI Record。

## Non-goals

- 不为统计创建后台网络服务、复杂任务系统或长生命周期高开销 Profile 记录。
- 不建立“UI Cache Hit 就是零 CPU 成本”的错误判定。
- 不测试尚未实现的 RT 缓存收益；不要填虚构数值。

## Architecture Constraints

- 采样时钟与统计字段必须符合现有线程模型：`Prepare()` 与可能并行的 `Record()` 写入不同统计快照或通过已有受控合并时机统一更新，**禁止跨线程普通字段数据竞争**。
- 如果 `GetFrameStats()` 当前读取尚在录制中的值，在编辑器 UI 面板中显示已完成帧快照，而不是无锁读取当前帧可变字段。
- 热路径不得每节点 `std::format`、字符串拼接、动态分配、日志输出；可将详细每 Widget Profile 设为可关闭的诊断选项。
- 统计开关的关/开性能差异要记录，避免用高开销统计污染 M1 对比。

## Baseline

已知：

1. `Application::Tick()` 顺序为 UpdateLayers → UpdateScenes → RenderFrame → RenderEditor → PresentFrame；不要把 RenderEditor（ImGui）等同于自研 UI 的唯一开销。
2. `RenderPipeline::Render()` 执行 `UIRenderer::Prepare()`，`Record()` 可能在线程池中执行；`RenderGraph` 开关会影响录制时序。
3. `UIRenderer::Prepare()` 当前负责可见 Widget 的 `PreUpdate`/`Update`、Paint Cache Build/Hit；`Record()` 负责以 DrawNode 提交 UI。
4. UI Reflector 已显示 RenderImpl/Layout/Paint/Upload/Submit/CacheHit 等有限指标，不需要再造完整调试 UI。

## Target Outcome

- 在同一台 Windows/DX12 机器得到稳定的真实性能基线与重复性说明。
- 能独立观察：Dock 业务 Update 调用次数，UI PreUpdate/Update CPU 耗时，Paint Cache Miss 源，DrawNode Submission CPU 耗时。
- 明确后续 **哪一部分值得优先优化**，不能只看整个进程任务管理器占用率。

## Relevant Files

- `Engine/Src/Render/RenderPipeline.cpp`
- `Engine/Inc/UI/UIRenderer.h`, `Engine/Src/UI/UIRenderer.cpp`
- `Engine/Inc/UI/Widget.h`, `Engine/Src/UI/Widget.cpp`
- `Editor/Src/Widgets/EditorLayer.cpp`
- `Editor/Src/Dock/DockManager.cpp`, `Editor/Src/Dock/DockWindow.cpp`
- `Test/` 下现有 Editor 自动化与 smoke 入口

## Implementation Steps

### 0.1 核对快照与运行环境

1. `git status --short`、`git rev-parse HEAD`、`git branch --show-current`，记录分支/commit/未提交变更。
2. 核对上述代码调用链是否与本计划一致；不同则先更新 `progress.md` 的 Baseline/Decision Log。
3. 固定 Scene、Inspector 选中实体、Dock 布局、窗口尺寸和 Renderer/RenderGraph 模式；记录 VSync、帧率目标和 GPU Driver。
4. 确定当前 Engine/Editor Debug 或 RelWithDebInfo 的 Profile 构建方式；优先一致使用同一构建配置，必要时记录二者。

### 0.2 最小化分段观测

优先 Tracy Zone，补充已有 UI 统计。建议事件名称（可以由实现者适配现有命名）：

| 位置 | 新增采样/计数 | 为什么 |
| --- | --- | --- |
| `EditorLayer::OnUpdate()` | `EditorChrome`、`DockManagerUpdate` | 定位总体编辑器更新成本 |
| `DockNode::UpdateLayout`、`DockNode::Update`、`DockTab::Update` | 调用次数、可选总耗时 | 验证重复调用比例 |
| `UIRenderer::Prepare()` | `VisibleWidgetCollect`、`WidgetPreUpdate`、`WidgetUpdate`、`PaintBuild`、`WidgetSync`、总耗时 | 分离树更新与构建/同步 |
| `UIRenderer::Record()`、`SubmitBlock()` | `UIRecordTotal`、DrawNode Count、总 `SubmitBlock` CPU 时间 | 区分缓存命中后的录制开销 |
| `UIElement::Update` / `Arrange` | 访问次数、Layout Dirty 数、Unchanged Arrange 次数（粗粒度计数） | 验证无效遍历/矩阵变换量 |
| `Widget::InvalidatePaint` | 每 Widget invalidation Count/Reasons、最后来源已有字段 | 找出 Cache Miss 是否由每帧重复标记导致 |

不要给每一个 `UIElement` 动态分配 Profile 名称；详细归因优先到 DockWindow / Widget，采样时使用固定字符串或独立的采集开关。

### 0.3 分离 CPU 热点与等待

- 将 `Application::UpdateScenes`、`RenderPipeline::Render`、`ImGui`、`Present`、`WaitForRender` 分别观察；记录 Main/Render/Worker 线程。
- 若 CPU 总利用率高但 UI 耗时很低，明确标注 Editor UI 优化对**整个进程**可能收益有限。
- 在 RenderGraph 并行录制下，额外关注 job queue 等待/上下文提交；不能单纯将各线程 Zone 求和当作主线程帧耗时。

### 0.4 固定案例采样

每组在稳定加载/暖机后运行至少 **300 帧**（或足够取得稳定 P50/P95 的等价采样），记录 P50/P95、CPU 时间占比和采样波动：

- A：Editor 打开复杂 Scene，鼠标静止、不操作；记录 SceneView 实时渲染存在这一事实。
- B：Inspector 不同组件、WorldOutline、AssetBrowser 多次切换 Tab；观察隐藏页还在 Update 多少次。
- C：拖动/拆分/合并 Dock、改变尺寸、弹出 Popup、失焦/聚焦、拖拽操作。
- D：SceneView 相机持续移动/Gizmo、GameView 实时纹理；确保静态 UI 与动态内容同时存在。
- E：ImGui Debug 打开/关闭，各自采样；避免归咎于 UI Tree。
- F：第二窗口、多 Viewport（若功能可用）；对不可用环境标记 SKIPPED 原因。

记录数据到本地可持久路径（建议 `artifacts/diagnostics/editor-ui-cpu/`；若仓库忽略产物，则保留可引用的日志路径），给 `progress.md` 一个摘要表，而不是粘贴海量 Raw Trace。

## Validation

- [ ] 构建通过（用本工作树确实存在的 preset/命令，例如 `cmake --build --preset build-debug`），`git diff --check` 通过。
- [ ] A–E 场景有清晰的 CPU P50/P95、命中率/缺失率、Update/Record/Draw Node 和上传量；F 若不可测说明原因。
- [ ] 可以观察同一 Dock 的重复 Update 现象有/无，而不是仅据源码主观断言严重。
- [ ] Profile 不跨线程 data race；采样开/关对 CPU 的额外开销被记录。
- [ ] 初始 Editor 能打开、切换 Tab、Popup、SceneView、输入无明显功能回归。
- [ ] `progress.md` 写入实际证据、性能基线与 M1 优先修复点。**全部通过才进入 M1**。

## Milestones

该文件仅执行 M0；后继 M1/M2/M3 由 `README.md` 控制，不得越级。

## Current Milestone

M0，未执行。

## Progress

由 `progress.md` 维护；本文件不跟踪动态状态。

## Decision Log

- 2026-10-14：优先在已有 Stats/Tracy 上轻量补充，而非创建独立 Profiling 子系统。

## Surprises & Discoveries

无实测发现；执行后仅记在 `progress.md`。

## Known Follow-ups

如果 UI 命令提交占比很低，M3 可在通过 M2 后记录 `NOT JUSTIFIED` 而不做 RT 大工程；先审查其他 CPU 热点。

## Outcomes

待验收。

## Resume Protocol

读取 `README.md` / `progress.md`，只执行本文件；阶段通过后更新 M1 Current Milestone 和下一步。
