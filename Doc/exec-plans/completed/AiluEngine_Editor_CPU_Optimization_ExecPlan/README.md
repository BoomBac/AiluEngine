# AiluEngine — Editor CPU Optimization ExecPlan

> 基线：GitHub `BoomBac/AiluEngine`，`master`，tree SHA `f49b6f1d0f4da2e802f9ea33bf9681af1a9b23cb`，核对于 2026-10-14。此计划**未修改源码、未运行本机 DX12/Editor**；执行者必须以当前工作树为准确认差异。
>
> 执行规则：先读本文件和 `progress.md`，之后**只读 Current Milestone 对应的 `mX-*.md`**；阶段验收 PASS 且更新 `progress.md` 才能进入下一阶段。
>
> 建议落盘路径：`Doc/exec-plans/active/AiluEngine_Editor_CPU_Optimization_ExecPlan/`。任务规模：**大型**（Dock → Retained UI Tree → CPU Draw Submission → 可选 RT/跨帧资源），所以采用拆分式计划。

## Purpose

降低 **Editor 空闲及交互状态下的 CPU 消耗**，在不破坏现有 Retained UI、Paint Dirty/DrawerBlock 缓存、DX12 渲染与多窗口 Dock 行为的前提下，让无需变化的窗口减少 UI 逻辑、布局、顶点构建、命令提交。**优先修复 CPU 无效工作，最后才引入可选 Widget RT 缓存**；通过基线数据而不是猜测决定 RT 投入是否有价值。

## Scope

- 添加能区分 `Dock / UI Update / Layout / Paint / Sync / Submit` 的轻量采样数据，并建立可复验基线。
- 修正 Dock 重复更新路径、隐藏 Tab 无意义 Tick、无变化仍布局、重复 Visibility Dirty、空 Widget 重建。
- 对已审计的静态 UI 支持按需更新/子树跳过；动态 UI、后台业务任务和外部 RenderTexture 持续正常运行。
- 保留原有每 Widget CPU DrawerBlock + 多帧 GPU Geometry 缓存；在测量证明有效时，引入 **Widget 级、显式开启** 的持久 Texture Cache。
- 覆盖 SceneView、GameView、Inspector、WorldOutline、AssetBrowser、Dock/Popup、编辑器工具栏与状态栏等典型场景。

## Non-goals

- 不把每个 `UIElement`（Button、Text、Checkbox）分别绑定一个 RT；不替换 UI Framework、DrawerBlock、RenderGraph 或 DX12 RHI。
- 不强制所有 UI 单线程/多线程重排；不重新设计全局帧调度、JobSystem、Scene 渲染管线。
- 不将 **Editor 整体 CPU 占用**的每帧场景更新和 Present 问题误归因于 UI；空闲降帧/失焦功耗策略仅列为 Follow-up。
- 首轮不实现脏矩形增量重绘、RT 图集、通用 Layer Compositor、跨窗口纹理移动的零成本平移；有收益再加。
- 不以刷新率节流掩盖 UI 状态失效、输入丢失或光标停滞；不修改 Debug 工具对真实帧数据的必要读取语义。

## Architecture Constraints

1. **CPU 优先**：M0 → M1 → M2 必须先完成独立验收；M3 只有性能证据支持时才启用 Texture Cache。M3 实现必须默认关闭，可回退到 kGeometry。
2. **Paint Cache 真相**：`Widget::InvalidatePaint`、`UIElement::Invalidate*`、`_is_paint_cache_frame_valid`、`UIRenderer::_widget_drawer_blocks`、`DrawerBlock::_gpu_dirty` 已存在，不可另造一套互相打架的 Dirty 源。
3. **数据更新 ≠ 绘制更新**：即使 Panel 隐藏，资产导入进度、自动化请求、GPU/文件状态等必要后台任务仍需 Tick；在业务层拆出后台 Tick，而非全盘停止。
4. **双路径可共存**：包含动态纹理（SceneView、GameView、预览 RT）、复杂交叠、Backdrop Blur、非标准效果的 Widget 初期维持 Direct/Geometry；静态 Dock 内容可显式选 Texture。
5. **Draw 顺序与透明度**：Texture Cache 合成必须保持 Widget Z-order、popup、scissor、Alpha blending、圆角、sRGB/Linear 行为；不可把透明 UI 误作为不透明 `CopyTextureRegion` 覆盖。
6. **坐标契约**：当前 DrawNode 顶点位置、裁剪与 UI 绘制都按窗口/Widget 世界位置构建；渲染到局部尺寸 RT 时必须显式解决投影原点与 Scissor 坐标变换，不允许直接 `BindOutput(local_rt)` 盲跑。
7. **时序与资源**：`RenderPipeline::Render()` 在主调度中调用 `UIRenderer::Prepare()`，在 RenderGraph 模式可将 `UIRenderer::Record()` 放到 JobSystem 并行任务。准备资源、缓存切换、销毁和录制快照必须明确线程边界；RT 生命周期遵守 GPU fence。
8. **兼容性**：每阶段必须通过输入、选中、拖拽 Dock、调整尺寸、多个窗口、Popup/Modal、文本输入光标、动态 Preview 等回归；缺设备/无自动化时不能宣称 GPU 验收通过。
9. **低侵入**：优先原有类增加少数状态字段/判断，不为潜在将来引入大型 RenderLayer 虚接口、插件或新的全局管理器。
10. **代码规范**：普通变量 `lower_snake_case`；成员 `_lower_snake_case`；静态变量 `s_...`；常量 `kCamelCase`；类型/函数 `CamelCase`，每行尽量不超过 160 列；尊重存量代码差异，不做无关改名。

## Baseline

以下是 **已通过仓库当前代码确认的事实**，不是已测 CPU 时间：

| 位置 | 现状与风险 | 对应阶段 |
| --- | --- | --- |
| `Engine/Src/UI/UIRenderer.cpp::Prepare` | 每帧遍历可见 Widget，调用 `PreUpdate`、`Update`；Paint dirty 时重建 CPU DrawerBlock，命中则复用 | M0/M2 |
| `Engine/Src/UI/UIRenderer.cpp::Record` / `SubmitBlock` | 即使 Paint Cache Hit 也遍历 DrawNode、切材质/纹理/scissor、`DrawIndexed`；window UI 先合成临时 RT 再 Blit | M0/M3 |
| `Engine/Src/UI/Widget.cpp` | `InvalidatePaint` 会失效全部 frame slot；`ClearPaintInvalidation` 与多帧缓存相关 | M0/M1/M3 |
| `Engine/Src/UI/DrawerBlock.cpp` | `CopyBuildDataFrom` 同步每帧资源并置 GPU dirty；`SubmitVertexData` 在非 GPU dirty 时直接返回 | M0/M3 |
| `Engine/Src/UI/UIRenderer.cpp::Prepare` | 判断 `IsPaintCacheDirty(frame) || is_cpu_blocks_empty`，**合法空 DrawNode 可能每帧重建** | M1 |
| `Editor/Src/Dock/DockManager.cpp::DockNode` | Tab 的 `UpdateLayout` 调 `_tab->Update`，`Update` 又调 `_tab->Update`；嵌套 `DockNodeTabItem` 也有额外 `UpdateLayout` | M1 |
| `Editor/Src/Dock/DockWindow.cpp::DockTab` | `Update` 对所有 `_tabs` 执行 `SetRect` 和 `Update`，非活动 Tab 仍做业务 Update；`SetVisible` 无相同值保护 | M1 |
| `Editor/Src/Dock/DockWindow.cpp::DockWindow` | `SetTitleBarVisibility`、`SetContentVisibility` 即使状态不变也会使 Widget Paint Cache 失效 | M1 |
| `Engine/Src/UI/UIElement.cpp::Arrange` | 矩形与 Dirty 未变时仍构建矩阵、求逆、变换 4 角点 | M1 |
| `Engine/Src/UI/UIElement.cpp::Update` | 遍历可见树，检查样式并逐子树调用 Update；Layout 内部已有 `_is_layout_dirty` | M2 |
| `Editor/Src/Widgets/EditorLayer.cpp::UpdateEditorChrome` | 每帧 `ResizeWidgetRoot`、读取状态并格式化 FPS 文案，造成额外工作/持续变更 | M1/M2 |
| `Editor/Src/Widgets/ObjectDetail.cpp::Update` | 每帧收集 Selection、检查结构、遍历 Component Editor 执行 `NeedsRebuild/Refresh` | M2 |
| `Editor/Src/Widgets/WorldOutline.cpp::Update` | 已使用 Scene `StructureRevision` / `EditRevision` 和 Selection Revision，适合复用 | M2 |
| `Editor/Src/Widgets/AssetBrowser.cpp::Update` | 已维护 `_content_dirty` / `_layout_dirty`；同时负责 `_import_controller.Tick()`，不得因隐藏跳过该工作 | M1/M2 |
| `Editor/Src/Widgets/RenderView.cpp::SceneView::Update` | 实时场景、相机预览、Gizmo、动态 RenderTexture、相机控制持续更新 | M2/M3 |
| `Engine/Src/Render/RenderPipeline.cpp::Render` | UI `Prepare()` 主路径，`Record()` 在启用 RenderGraph 时可并行录制；UI 合成 RT 每帧使用临时目标 | M0/M3 |

**已存在的观测入口**：`UIRenderStats` 记录 Visit、RenderImpl、Layout、Text Layout、Vertices、Uploaded Bytes、Draw Nodes、Draw Calls、Hit/Miss、Paint Build、GPU Upload（CPU 时间）、Submit（CPU 时间）；`EditorLayer::DrawUIReflectorRenderStats()` 可显示。**缺少**统一的 UI Update/PreUpdate、Dock Update 与总 Prepare/Record 耗时分段。

**特别关注**：`UIRenderer::Prepare` 中“可见 Widget 收集与更新”出现二次扫描是为了处理本帧新增 Widget；优化时必须保留新 Popup 等同帧更新能力。

## Target Outcome

- 静态 Editor 空闲时，**Dock 逻辑、树遍历、Paint 构建、顶点更新**减少到有明确原因才执行；仍可正常持续显示 SceneView 的每帧场景结果。
- 隐藏 Tab 不反复执行重型 UI 刷新；显示/切换/重排后恰好触发必要的更新；后台工作不中断。
- 原有 Geometry 缓存不回归；已构建空 Widget 稳定 Cache Hit，不出现持续 Miss。
- 当 Widget RT 缓存启用时，大量静态 DrawNode 替换成少量合成 Draw；当效果不好/内容不兼容时立即回退，不牺牲正确性。
- 结果以 `progress.md` 中的 P50/P95 CPU timing、次数、Draw Calls、上传量、内存开销与验收证据判定；**不承诺未经测量的收益百分比**。

## Relevant Files

**Engine**

- `Engine/Inc/UI/UIElement.h`, `Engine/Src/UI/UIElement.cpp`
- `Engine/Inc/UI/Widget.h`, `Engine/Src/UI/Widget.cpp`
- `Engine/Inc/UI/UIRenderer.h`, `Engine/Src/UI/UIRenderer.cpp`
- `Engine/Inc/UI/DrawerBlock.h`, `Engine/Src/UI/DrawerBlock.cpp`
- `Engine/Src/UI/Basic.cpp`, `Engine/Src/UI/UISlot.cpp`, `Engine/Src/UI/UIFramework.cpp`
- `Engine/Src/Render/RenderPipeline.cpp`
- `Engine/Src/Framework/Common/Application.cpp`, `Engine/Src/Framework/ImGui/ImGUILayer.cpp`

**Editor**

- `Editor/Src/Dock/DockManager.cpp`, `Editor/Src/Dock/DockWindow.cpp`，对应 `Editor/Inc/Dock/`
- `Editor/Src/Widgets/EditorLayer.cpp`
- `Editor/Src/Widgets/ObjectDetail.cpp`, `WorldOutline.cpp`, `AssetBrowser.cpp`, `RenderView.cpp`, `EditorProgressPanel.cpp`
- `Editor/Src/Inspector/ComponentEditors/*.cpp`
- `Test/` 下已有 Editor smoke/acceptance，按实际可用的运行器扩展最小测试

准确增删文件范围由实现者基于当前工作树决定，**不要为了贴合文件列表增加空模块**。

## Milestones

| Milestone | 详细文件 | 独立交付成果 | Gate |
| --- | --- | --- | --- |
| **M0 — Baseline & Diagnostics** | `m0-baseline-and-profiling.md` | 分段性能指标与 4+ 场景采样基线 | 数据完整、现有回归 PASS |
| **M1 — Dock & Cache Quick Wins** | `m1-dock-and-cache-fixes.md` | 每帧重复逻辑/错误脏标记修复 | Dock/Popup/Hidden Job 回归 PASS，采样无显著退化 |
| **M2 — Demand-Driven UI Update** | `m2-demand-driven-ui-update.md` | 可审计的按需 Tick/子树跳过、业务刷新与 UI 解耦 | 静态节点更新下降、输入/动画/数据刷新 PASS |
| **M3 — Optional Widget Texture Cache** | `m3-optional-widget-texture-cache.md` | 可配置、可回退的 Widget RT；静态面板 CPU 提交改善 | DX12 图像/状态/资源与性能 PASS；否则关闭默认并记录 |

## Current Milestone

**M3 — Optional Widget Texture Cache**（已从 Toolbar 试点扩展至显式候选的透明 Widget RT；Debug/300 帧对照通过，新增视觉/交互与完整 ROI 验收仍为 PARTIAL，M0–M2 Gate 保留未通过）。执行者仅加载 `m3-optional-widget-texture-cache.md`。

## Progress

- 初始状态：所有 Milestone 未执行，仓库未因本文档发生修改。
- 逐步 checklist、测量值、commit/文件路径及验收结果统一维护于 `progress.md`。

## Decision Log

- 2026-10-14：基于源码确认 Paint Cache 已存在；不重复建设 CPU 缓存。
- 2026-10-14：CPU 优化先于 RT 化；以 Dock/更新树/Draw Submission 的分段采样作为前置判断。
- 2026-10-14：Texture Cache 以 Widget 为缓存单元，**不是每个 UIElement 独立 RT**。

## Surprises & Discoveries

- 现有绘制缓存不仅是顶点内存：还跟踪各 frame slot revision 和 GPU dirty；新增 RT 层必须叠加，而非替换。
- UI 当前创建 Window Composite RT 并清屏、最终 Blit 到 BackBuffer；因此即使有 RT cache，也**不能推断能消除整个 UI 渲染命令列表**。
- 未知当前代码分支是否有尚未合入的修改；执行前对比 SHA、近期开源分支与工作区差异。

## Validation

必须每阶段执行：`cmake --build --preset build-debug`（如当前工作树有此 preset；若不同，记录等价命令）、`git diff --check`、对应自动化/手工场景、CPU Profiles 和 DX12 Debug Layer（M3）。不得只凭编译成功进入下一阶段。

**统一采样矩阵**：1) 空闲主界面，SceneView 静止；2) 切换复杂 Inspector/Hierarchy/AssetBrowser Tab；3) Dock 拖拽/尺寸变化/弹窗；4) SceneView 实时运行（Gizmo/相机）；5) ImGui Debug UI 开/关；6) 双窗口/多 Viewport（若可用）。分别记录 P50/P95 CPU 帧耗时、UI Update/Prepare/Record/Submit、Cache Miss、Draw Calls、上传量、显存/RT 个数；同样设置和相同窗口尺寸对比。

## Known Follow-ups

- 场景暂停/相机静止时节流 RenderPipeline、编辑器窗口失焦限帧、`Present`/GPU 等待对整个进程 CPU 的影响。
- 仅在 M3 数据支持时考虑子区域缓存、复用平移不重绘、Dirty Rect、纹理图集、额外 GPU 合成优化。
- `ObjectDetail` Component Property 的细粒度 Revision 语义若目前不足，可在后续独立任务完善事件模型；本计划不得为了优化引入过度侵入的 ECS 变更。

## Outcomes

尚无执行结果。只在 `progress.md` 有测试证据后填实际收益与回归状态；不可把上述代码分析当作优化已经完成。

## Resume Protocol

1. 读取 `README.md` → `progress.md` → `Current Milestone` 对应的 `mX-*.md`。不要一次加载所有 mX 文件。
2. 检查工作树 `git status` / branch / commit；比对 Baseline，识别他人改动，不覆盖未知变更。
3. 从 `progress.md` 的 Next Step 继续，完成当前阶段**实现 → 构建 → 性能/功能测试 → 记录**。
4. 若 Gate 未通过：保持当前阶段，修复或记 Blocker；严禁提前实施下一阶段。
5. Gate 通过：更新 `progress.md` 状态/数据/Decision Log 和 `Current Milestone`，必要时同步 README 的 Current Milestone 标记后再继续。
6. 完成全部阶段后，把本计划移至 `Doc/exec-plans/completed/`，保留复验数据索引和 Follow-ups。
