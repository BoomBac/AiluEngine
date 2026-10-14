# Editor CPU Optimization — Progress

> **唯一动态进度真相**。每次工作结束更新本文件；性能数值只填写真实测量，禁止推算或写“预计 PASS”。

## Purpose

让下一个实现会话可以准确恢复当前状态、验证项和决策，而不必重新加载所有 Milestone 文档。

## Scope

M0 基线 → M1 Dock 与 Cache 修复 → M2 按需 Update → M3 可选 RT Cache。

## Non-goals

不在此重复各 Milestone 全量设计；不将未实现或未经验证内容标记为 COMPLETE。

## Architecture Constraints

执行 `README.md` 的全部架构约束，尤其：每阶段独立验收、不丢后台任务、保留现有 CPU Paint Cache、RT 默认关闭。

## Baseline

- 计划参考 SHA：`f49b6f1d0f4da2e802f9ea33bf9681af1a9b23cb`。本次工作树：`master` / `c49a7e0fa9578e535c9ec432fba3bba66d3df702`；开始时 `git status --short` 为空。
- 本地 Debug：Visual Studio 18 2026 x64，Tracy OFF，Engine Profiler ON，Lua ON；输出位于当前 checkout 的 `.artifacts/build-debug` 与 `.artifacts/out`。
- CPU：Intel Core i7-8700，6 核 / 12 线程。WMI 仅枚举 OrayIddDriver 与 MuMu Virtual Display Adapter；实体 GPU/驱动、分辨率、VSync 与帧率目标未确认。
- Debug Editor 已构建；隐藏启动的 Editor 通过引擎内置 `Profiler::GetFrameData()` 自动导出 300 个采样帧、301 个缓存帧及 162252 个 Profile Block 事件，无需连接 Tracy 或操作编辑器。

## Target Outcome

空闲 CPU 负担降低，动态/交互正常，阶段性能/功能证据可追溯，RT 缓存有客观 ROI。

## Relevant Files

`README.md`、`m0-baseline-and-profiling.md`、`m1-dock-and-cache-fixes.md`、`m2-demand-driven-ui-update.md`、`m3-optional-widget-texture-cache.md`。

## Milestones

| Milestone | 状态 | 验收 | 证据 |
| --- | --- | --- | --- |
| M0 Baseline & Diagnostics | IN PROGRESS | PARTIAL | Tracy OFF / Engine Profiler ON; automated idle capture and UI cache/draw/upload counters PASS; A–F scenarios remain |
| M1 Dock & Cache Quick Wins | IMPLEMENTATION PARTIAL / VALIDATION PARTIAL | NOT PASSED | M0 Gate incomplete; call-count and interaction cases pending |
| M2 Demand-Driven UI Update | IMPLEMENTATION PARTIAL / VALIDATION PARTIAL | NOT PASSED | M1 Gate unmet; early implementation per user request |
| M3 Optional Widget Texture Cache | IMPLEMENTATION PARTIAL / VALIDATION PARTIAL | NOT PASSED | User-requested early implementation; M2 Gate unmet; visual and full-frame ROI Gate incomplete |

## Current Milestone

**M3 — Optional Widget Texture Cache**（用户要求提前实现；M0–M2 Gate 仍未通过，M3 不得标记 PASS）。

## Progress

- [x] 核对工作树 commit、分支、初始 dirty 状态；记录 CPU 与 Debug/Tracy 配置
- [ ] 补齐实体 GPU/驱动、分辨率、VSync、帧率及项目/Scene 固定信息
- [x] 空闲场景自动采集 300 帧 Profile Block 与 UI cache/draw/upload 计数；A–F 其余场景待补
- [x] 当前 checkout 的 Debug Editor 编译/链接 PASS；`git diff --check` PASS
- [x] Editor 可隐藏启动；Engine Profiler 逐事件 CSV、逐 Block P50/P95 汇总自动导出
- [ ] Editor 稳定帧循环、自动化/手工回归与 M0 Gate PASS
- [x] M1 Dock 重复 Update、隐藏 Tab 业务 Tick、同值 Visibility、Arrange 快速路径、空缓存、EditorChrome quick wins、SceneView 隐藏相机预览/Spawn 后台 Tick 已实现；Debug Editor build 通过
- [ ] M1 每帧活动 Tab Update 次数断言/计数、空 Widget 异步资源/增删子项用例、Dock/Popup/隐藏 AssetBrowser 导入及 SceneView/GameView 多窗口交互验收；M1 Gate PASS 待 M0 完成后复验
- [x] M2 首批按需 Update 门控、UI 计数/Reflector 与采样统计接入；Debug Editor build 和 300 帧空闲采样通过
- [ ] M2 静态 Widget 交互唤醒、Dock/Popup/输入/主题/尺寸/后台任务回归及 Gate PASS
- [x] M3 完成 DrawNode ROI 审计；实现默认 Geometry、显式可开关的 EditorToolbar 不透明 RT cache 与缓存指标
- [ ] M3 完成图像/交互/DX12 资源状态验收和整帧 CPU/GPU ROI Gate；证据不足时保持关闭

### 测量表（执行者填写真实值，必须注明时间与配置）

| Case | Commit / Config | Frame CPU P50/P95 ms | UI Update P50/P95 | UI Prepare/Record P50/P95 | Paint Hit/Miss | UI Draw Calls | Uploaded KB | 备注/证据路径 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Idle Scene | `c49a7e0` + M0 instrumentation / Debug, Tracy OFF | 16.691/19.906 (`Application::LogicLoop`) | 3.610/5.586 (`UIElement::Update`) | Prepare 1.499/2.206; Record 1.627/2.206 | 9 hit / 2 miss per frame | 101 | 42.89 KB/frame | 300 UI stats frames, all stable; 301 cached Profile frames / 161977 events；`.artifacts/diagnostics/editor-ui-cpu/baseline-idle-engine-profiler-with-ui-stats/` |
| Idle Scene after M1 | `c49a7e0` + M1 / Debug, Tracy OFF | 14.508/21.160 (`Application::LogicLoop`) | 3.363/5.231 (`UIElement::Update`) | Prepare 1.309/2.016; Record 1.411/2.102 | mean 9.94 hit / 1.06 miss | 101 | 26.2 KB/frame mean | 300 UI stats / 301 profile frames；`.artifacts/diagnostics/editor-ui-cpu/m1-idle-after/` |
| Idle Scene after M2 early implementation | `c49a7e0` + M1 + M2 / Debug, Tracy OFF | 13.6971/18.2216 (`Application::LogicLoop`) | 2.4917/4.2785 (`UIElement::Update`) | Prepare 0.9297/1.5242; Record 1.4399/2.0022 (SubmitBlock 1.2867/1.8009) | mean 9.95 hit / 1.05 miss | 101 | 25.78 KB/frame mean | 300 UI stats / 301 profile frames; 4 executed / 7 skipped per frame；`.artifacts/diagnostics/editor-ui-cpu/m2-idle-after/`。单次空闲采样，交互验收未完成 |
| Idle Scene M3 trial — Geometry | `c49a7e0` + M1/M2 + M3 Geometry / Debug, Tracy OFF | 13.6573/17.5226 (`Application::LogicLoop`) | 2.5032/3.8142 (`UIElement::Update`) | Prepare 0.9522/1.3714; Record 1.4167/1.9071 | mean 9.94 hit / 1.06 miss | 101 | 25.38 KB/frame mean | 300 UI stats / 301 profile frames; `.artifacts/diagnostics/editor-ui-cpu/m3-final-geometry-after/` |
| Idle Scene M3 trial — EditorToolbar Texture | `c49a7e0` + M1/M2 + M3 Texture / Debug, Tracy OFF | 14.3118/19.3132 (`Application::LogicLoop`) | 2.5465/3.7842 (`UIElement::Update`) | Prepare 0.9837/1.4367; Record 1.3176/1.7262 | mean 9.94 hit / 1.06 miss | 88 | 25.59 KB/frame mean | 300 UI stats / 301 profile frames; cache 1 hit / 0 dirty per frame, 202776 B resident; `.artifacts/diagnostics/editor-ui-cpu/m3-final-texture/` |
| Tab Switch | TBD | TBD | TBD | TBD | TBD | TBD | TBD | NOT RUN |
| Dock/Popup | TBD | TBD | TBD | TBD | TBD | TBD | TBD | NOT RUN |
| Scene Dynamic | TBD | TBD | TBD | TBD | TBD | TBD | TBD | NOT RUN |

## Decision Log

- 2026-10-14：选择拆分式 ExecPlan；M0 测量先于优化，CPU 高收益点先于可选 RT。
- 2026-10-14：不把 Cache Hit 等同于 Draw Submission CPU=0；M3 将以真实 DrawNode/CPU 指标判定。
- 2026-10-14：采集改用 Engine 内置 CPU Profiler；`CPUProfileBlock` 在 `AILU_ENABLE_ENGINE_PROFILER` 下写入层级事件，`Editor` 在 `--profile-capture` 模式从 `Profiler::GetFrameData()` 写 CSV，整个过程 Tracy OFF 且隐藏运行。
- 2026-10-14：`AILU_ENABLE_ENGINE_PROFILER` 作为 Engine PUBLIC 编译定义，使 Editor TU 中的固定名 `PROFILE_BLOCK_CPU` 在 Tracy 关闭时也启用；层级深度用有符号计数并钳制到零，避免无符号下溢。
- 2026-10-14：UI cache hit/miss、Draw Calls 和 Uploaded Bytes 从现有 `UIRenderStats` 在每帧完成后复制，不在热路径增加锁；独立 CSV 逐帧保存，脚本输出 mean/P50/P95/max。
- 2026-10-14：按用户要求在 M1 Gate 未通过时提前实现 M2 第一批。仅 DockWindow 标题栏、DockTab 标签栏、EditorToolbar、EditorStatusBar 显式 OnDemand；默认及所有内容/插件 Widget 保持 EveryFrame。M2 不改变 M1 Gate 状态，也不启动 M3。
- 2026-10-14：按用户要求提前实现 M3，M0–M2 Gate 保留未通过。3.0 审计在基线 UI 101 DrawCall 中找出 EditorToolbar 的 14 DrawNode；高于它的 PlaceActor/SceneView/AssetBrowser 内容区包含动态内容，不选。仅为 `EditorToolbar` 开放 `kOpaqueTexture`；默认/未知/插件 Widget 均 `kGeometry`，仅接受全局 BackBuffer 且 Reflector 关闭的原点对齐工具栏。
- 2026-10-14：Texture 对照中 UIRenderer::Record P50/P95 1.4167/1.9071 → 1.3176/1.7262 ms，DrawCall 101 → 88；整帧 LogicLoop P50/P95 13.6573/17.5226 → 14.3118/19.3132 ms。单次采样显示 UI submit 有收益、整帧 P95 回归，因此 Geometry 继续默认，M3 Gate 不通过。

## Surprises & Discoveries

- 初始发现（源码级）：`DockNode::UpdateLayout`/`Update` 均可能调用 Tab Update；`DockTab::Update` 更新非活动 Tab；`UIElement::Arrange` 即使无变化也计算矩阵；空 Widget 有重复 Miss 风险。均**尚未通过运行时 profile 验证其耗时权重**。
- 自动采样（2026-10-14）：临时工程场景含 19 个实体，不适合作为空闲场景；已将仅位于 `.artifacts` 下的临时场景清空。Engine Profiler OFF-Tracy 成功采集 300 帧，真实提供 Dock/UI Profile Block 的 P50/P95。实体 GPU/驱动仍未确认；A–F 交互案例与 UI cache 计数尚未覆盖。
- UI 统计验证（2026-10-14）：短采样发现采集回调引用捕获的局部状态在 `app.Tick()` 前已离开作用域；已移到 Tick 外层作用域。随后隐藏运行 300 帧，UI stats 恰好输出 300 行，计数稳定为每帧 9 hit、2 miss、101 draw calls、43920 uploaded bytes。
- M2 空闲采样（2026-10-14）：稳定 300 帧每帧执行 4 个 Widget、跳过 7 个；`UIElement::Update` P50/P95 为 2.4917/4.2785 ms，相对 M1 本次历史采样 3.363/5.231 ms 下降约 25.9%/18.2%；`Application::LogicLoop` P95 为 18.2216 ms，相对此前 M1 的 21.160 ms 下降约 13.9%。该差异来自单次相邻采样，不能单独证明全部收益由 M2 导致；需同条件复测和交互回归。
- M3 首脏帧 smoke（2026-10-14）：5 帧、0 warmup 中首次 RT repaint 1/5，稳态 hit 1/帧，resident 202776 B；repaint P95 1.3884 ms。首次帧含一次 shader load/PSO 编译和 Editor 初始化，不能与稳态 CPU 对照。DX12 输出无 ASSERT/ERROR，但未取得实际 UI 图像。
- 后续新发现按日期追加，标明复现步骤、因果证据、是否解决。

## Validation

| 阶段 | Build | Test/Smoke | CPU Profile | DX12 Debug | Gate |
| --- | --- | --- | --- | --- | --- |
| M0 | PASS（Debug Editor；Tracy OFF / Engine Profiler ON） | PASS（隐藏 Editor；空场景、60 warmup + 300 sample） | PASS（300 UI stats rows：9/2 cache hit/miss、101 draw calls、43920 uploaded bytes/frame；300 LogicLoop samples） | N/A | NOT RUN（A–F 场景和交互回归未齐） |
| M1 | PASS（当前 checkout Debug Engine/Editor build，关闭增量链接） | NOT RUN（交互回归待做） | PASS（300 UI stats + 301 profile frames；结果见 Idle Scene after M1） | N/A | PARTIAL / BLOCKED（M0 Gate 未通过；整体 LogicLoop P95 +1.254 ms，需在完整场景复验） |
| M2 | PASS（当前 checkout Debug Editor，`/m:1 /p:LinkIncremental=false`） | PASS（隐藏 Editor，60 warmup + 300 sample；静态 widget Update 计数 4/7） | PASS（300 UI stats / 301 profile frames；结果与证据路径见测量表） | N/A | PARTIAL（M1/M0 Gate 前置条件未齐；真实交互回归待做） |
| M3 | PASS（当前 checkout Debug Editor build） | PARTIAL（300 帧 Geometry/Texture 隐藏采样及首次脏帧 smoke；无视觉/鼠标验收） | PASS（300 帧对照；见下表） | NOT VERIFIED（未取得 UI frame capture，未做交互/Debug Layer 验收） | PARTIAL / NOT PASSED（UI Record 降低，但单次 LogicLoop P95 回归；默认 Geometry） |

- 记录格式：`日期/分支/commit`、测试命令、PASS/FAIL、Profile 文件、对照基线的绝对值/百分比、已知异常与证据路径。
- 硬件上无法运行 DX12 时，M3 只能是 `BLOCKED`，**不能用逻辑推断作为 GPU Gate PASS**。

## Blockers

- 已补固定名 `PROFILE_BLOCK_CPU` 数据：EditorLayer/Dock、UIRenderer Prepare/Record、Widget PreUpdate/Update、PaintBuild/Sync、UIElement Update/Arrange 与 Widget InvalidatePaint；无动态 Zone 名称或热路径字符串分配。
- Tracy 不参与采样。当前已取得空闲场景及 UI cache/draw/upload 基线；A–F 交互案例与初始交互回归仍待执行，故 M0 Gate 未通过。
- 临时空项目位于 `.artifacts/diagnostics/editor-ui-cpu/baseline-project/EditorCpuBaseline.ailuproject`；采集脚本位于 `Tools/Profiling/CaptureEditorProfileBlocks.ps1`；逐帧 UI 与 Profile 证据位于 `.artifacts/diagnostics/editor-ui-cpu/baseline-idle-engine-profiler-with-ui-stats/`。
- M1 实现：`DockNode::UpdateLayout` 只传播布局；活动 Tab 才 SetRect/Update；`TickBackground` 保留隐藏 AssetBrowser 导入，并在隐藏 SceneView 时推进 pending spawn 与同步相机预览引用；同值可见性不再失效；合法空缓存记为已构建；Arrange 与 EditorChrome 跳过重复工作。
- M2 第一批实现：`Widget` 默认 EveryFrame；OnDemand Widget 由布局/变换/层级/可见性失效、Widget 输入事件或显式 `RequestUpdate()` 唤醒；Debug Reflector 打开时强制更新以保留调试语义。新注册 Widget 的 late pass 保留。内容面板（AssetBrowser、ObjectDetail、WorldOutline、SceneView/GameView）、Popup 与第三方 Widget 暂留 EveryFrame。
- M3 第一批实现：Widget 默认 `kGeometry`；Debug Reflector 可显式切换只支持 `EditorToolbar` 的 `kOpaqueTexture`。Prepare 快照 Cache/RT/Quad，Record 先重绘脏 RT 再按原顺序合成；RT 用局部原点尺寸，白名单仅包含原点、全局 BackBuffer 的不透明工具栏。Paint/尺寸变化使缓存失效；cache-valid 状态由 UIRenderer 持有，Record 不读写 Widget cache flags。Shader 仅首次显式开启时加载；cache metrics 只在 Renderer stats 中记录。

## Next Step

继续 M3 扩大缓存的独立验收（详见本文件末尾的 2026-10-14 扩展记录）。新增范围使用透明 `kTexture`，默认仍为 Geometry；
通过 Debug 构建和同条件自动采样后，在真实 Editor 中切换 Reflector 的实验缓存开关，再关闭 Reflector，验证 Dock 标题/标签、
WorldOutline/Inspector/AssetBrowser 候选的文字、圆角、裁剪、Hover/输入/滚动、移动/Resize/DPI/主题/多窗口和资源释放。
动态 RT、BackdropBlur 与普通图片依赖仍回退 Geometry。完整图像与 DX12/整帧 ROI Gate 尚未通过，M0–M2 缺项仍独立保留。

## Known Follow-ups

- 整个 Editor 空闲降帧与 `Application::UpdateScenes` / `RenderPipeline::Render` / Present 时间分离。
- 如 Inspector 无可靠 Component Revision，不在 M2 强行静态化；记录为后续 ECS 事件化任务。

## Outcomes

M1 核心代码已实现，但每帧 Update 计数断言/调试计数及交互/异步资源回归仍未完成。空闲场景同条件采样显示 DockManager Update P50 1.817→0.253 ms、DockNode UpdateLayout P50 3.950→0.005 ms、Arrange P50 0.0182→0.0042 ms；Cache Miss 2→均值 1.06/frame、上传量 43.9→26.2 KB/frame。LogicLoop P50 下降 2.184 ms，P95 上升 1.254 ms；数据仅为空闲场景，不构成 M1 Gate PASS。
M2 首批按需更新已构建并完成 300 帧隐藏空闲采样，P50/P95 指标见测量表。当前仅静态 Editor Chrome 与 Dock 标签/标题启用 OnDemand；其余 UI 保持 EveryFrame。尚无实时鼠标/键盘、Popup/Dock、主题/Resize、面板后台任务的人工/自动回归证据，因此 M2 Gate 未通过。
M3 已实现仅 EditorToolbar 的显式不透明 RT 试点；默认 Geometry。300 帧对照中 DrawCall 101→88、UIRenderer::Record P50/P95 降低约 7.0%/9.5%，但 Application::LogicLoop P50/P95 从 13.6573/17.5226 升至 14.3118/19.3132 ms；单次样本不足以证明整帧收益，且实际图像/鼠标/DX12 Debug Layer 尚未验收。结论：保留实验实现但不默认启用，M3 Gate NOT PASSED。

## Resume Protocol

1. 仅读 `README.md`、本文件、当前阶段 `mX-*.md`；不预读未来阶段。
2. 核对 `git status` 与当前 commit；保护用户已有修改。
3. 按 `Next Step` 实现并验证；每次结束在本文件补充已测数据、变更文件、命令、Decision 和 Next Step。
4. 当前 Gate 不通过不能进入后续阶段；通过后修改 Milestones 表、Current Milestone 与 Next Step，再加载下一阶段。

## 2026-10-14 — M3 cache scope expansion

- 用户反馈现有 M3 的 Editor 测试正常，并授权考虑扩大 dirty-only 重绘范围。此反馈对应已有试点，新增范围仍须独立验收。
- 在现有 `kOpaqueTexture` 之外增加 `kTexture` 透明 RT 模式；默认仍为 Geometry。扩大实验开关覆盖 Toolbar/StatusBar、Dock 标题/标签，以及 WorldOutline、Object Detail、Asset Browser 内容候选。
- 使用 DrawNode 实际依赖判断资格：标准 UI/color 和 font 材质可缓存；动态 RT、BackdropBlur、普通图片依赖、未知材质、Popup、depth output 和无效/超限范围回退。AssetBrowser 缩略图/Inspector 图片出现时可能回退整个 Widget，不承诺全部内容已缓存。
- 追加缓存专用 UI/font shader（复用原 shader include），累积 alpha 并以预乘 alpha 合成，保留圆角/边框/文字覆盖率。四因子 Blend 仅在新增 shader 显式使用；既有两因子 shader 保持不变。
- 补齐任意 Widget 原点的正交投影与 signed/clamped scissor、位置/模式/output 改变失效、fallback 后重新重绘、资源尚未 ready 时不宣布 RT valid。业务 Update/Tick 模式不变。
- `AILU_EDITOR_UI_TEXTURE_CACHE=1` 保留原 Toolbar 不透明试点；`=2` 启用扩大范围。Reflector 实验开关可切换现有候选 Widgets，需关闭 Reflector 才使用 RT。采样脚本增加 `-EnableWidgetTextureCache`。
- 最小检查：DXC 7 stage/variant PASS（新增 3 shader VS/PS + font MSDF PS）；10000 个随机 alpha 堆栈验证线性 RGB 与直接合成等价，最大误差 `3.33e-16`（不包含 GPU/sRGB 量化）。
- 首轮完整 Debug build 中途修改 Widget header 后，Test target 的 GENERATED_BODY 行号短暂不匹配；随后正常 AiluHeadTool 流程已重新生成 Widget.gen.h/cpp。后续正常生成与最终完整 `build-debug -- /m:1 /p:LinkIncremental=false` 均 PASS。
- 当前 preset 的实际 build/output 在 `F:/AiluEngine/build/debug` / `F:/AiluEngine/artifacts/out`，CMAKE_HOME_DIRECTORY 已核实为本 checkout；配置 Engine Profiler ON 用于后续真实采样。
- Next Step：完成 build-debug，运行 Geometry/扩大 Texture 的同条件采样与首脏帧 smoke；本次新增图像、交互、多窗口/DPI/Resize、DX12 状态验收仍待执行。M3 Gate 保持 PARTIAL。

### 扩展最终验证与结果

- 最终 Debug Engine/Editor/所有默认 targets 构建 PASS；AiluHeadTool 正常更新 Widget generated header，未手改生成代码；`git diff --check` PASS。
- 修复启动布局反序列化替换 Widget 后丢失 runtime-only cache mode：布局恢复后重应用候选策略，同时兼容旧布局 `ObjectDetail_content` / `AssetBrowser_content` 名称。Debug 开关与启动 env override 共用同一候选判断。
- 分数像素 Dock 矩形采用 floor(origin)/ceil(end) 包围范围，quad 与局部 RT 都按整数像素边界合成；10000 组随机范围的像素中心映射检查 PASS。尚无实际 DPI/Resize 图像验收。
- 日志对照发现并修复本次引入的 DX12 warning：RT desc optimized clear color 改成透明，缓存 shader 显式 `ZWrite: Off`。最终 Geometry / Texture 的 severity=2 warning IDs/counts 完全一致：680/14、974/7、679/12、677/3；新增 820 clear mismatch 已消失，没有本次新增的 state/UAF/assert 日志。三组均存在基线 Streamline `'kFeatureDLSS' context is missing` ERROR，不能宣称全日志无 ERROR。runtime EngineConfig `_enable_d3d12_debug_layer=true`；实际日志确有 Debug Layer 消息。
- 最终三组进程正常退出：Geometry 60 warmup + 300 sample；expanded Texture 同配置 60 + 300；首脏帧 0 + 10。实际 Editor/Engine DLL hash 与本次 build output 一致；配置/布局及既有测试会写的 Material 文件已按启动前 bytes 恢复；仅关闭本次启动进程。
- 配置：当前 checkout / Debug；Engine Profiler ON、Tracy ON（与此前历史 OFF-Tracy 表不可直接横比）。采样时无编译任务；使用既有临时空项目和原始 Editor 布局。

| 最终对照 | Geometry | Expanded Texture |
| --- | --- | --- |
| UI DrawCall P50 / P95 | 101 / 101 | 65 / 67 |
| Texture cache hit P50 / dirty mean | 0 / 0 | 8 / 0.0567 per frame |
| 活动缓存 RGBA8 字节估算 | 0 | 1807584 B（约 1.724 MiB；不含隐藏缓存/驱动分配开销） |
| UI Record P50 / P95 ms | 1.5297 / 2.5338 | 1.1651 / 1.6009 |
| UI SubmitBlock P50 / P95 ms | 1.3665 / 2.2508 | 1.0107 / 1.4217 |
| UI Prepare P50 / P95 ms | 1.0272 / 2.2238 | 1.0319 / 1.6291 |
| Application LogicLoop P50 / P95 ms | 15.3031 / 26.6756 | 13.5161 / 17.6704 |
| 上传 bytes mean | 26424 | 23544 |

- 首脏帧 smoke：起始需要绘制的 RT 最多 8 张，之后稳定 8 hit；10 个样本中 dirty 总数 10；repaint 最大 1.8174 ms。startup Shader/PSO/Editor 工作不与稳态比较。
- 当前可见 11 个 Widget 中 8 个缓存命中；含动态 RenderTexture 的 SceneView、未审计的 Place Actor 和带普通图片依赖的 AssetBrowser 内容继续 Geometry。WorldOutline 已接入候选策略，但当前布局未显示其内容，尚无该面板的独立运行证据。
- 正确日志与 CSV 证据：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/diagnostics/widget-cache-expansion-20261014-verified/{geometry,expanded-texture,first-dirty-smoke}/`。此前 `...-20261014/` 为边编译边采样且策略未覆盖旧布局的探索数据，不能作性能 Gate；`...-final/` 为修复 DX12 warning 前的完整对照。
- 稳态 DrawCall 101→65 是可重复的命令减少；本轮 UI Record P50 下降约 23.8%。不同轮次的整帧 P95 波动明显，因此仍不将单次整帧变化作为完整 ROI Gate。默认 Geometry；新增透明范围的实际视觉、滚动/输入、Dock/多窗口/DPI/Resize、主题/资源释放等手工验收与 GPU timing 未完成，M3 Gate 保持 PARTIAL。
- 手工复验入口：Reflector > Render Stats > `Experimental: eligible editor widget texture caches`，开启后关闭 Reflector（Reflector 可见时强制 Geometry），逐项检查上述交互。也可用 `AILU_EDITOR_UI_TEXTURE_CACHE=2` 启动；`=1` 仍是原 Toolbar 不透明试点。

## 2026-10-14 — 用户报告内容缺失与帧率回归：修复

- 用户截图确认启动后 Inspector/WorldOutline/AssetBrowser 内容出现缺失，并报告 FPS 下降约 20。此前 DrawCall/缓存命中采样不构成内容正确性证据；此前性能表不能作为视觉 Gate。
- 已确认根因：`UIRenderer::PushScissor` 存储的是 `(left, top, right, bottom)`，`D3DConvertUtils::ToD3DRect` 将 `Rect.width/height` 原样当 right/bottom。扩展实现错误地按 extent 计算，又将 `right-left` / `bottom-top` 送到 DX12，导致右侧/底部 scissor 为空或过窄；Geometry 路径同样受影响。
- 修复只保留端点语义：right/bottom 分别直接减 cache origin，裁剪后仍输出 right/bottom。针对右側 Inspector `(1135,79,1480,872)`、底部 AssetBrowser `(248,623,1129,864)` 与局部 cache `(5,3,350,796)` 的回归数值检查 PASS。
- 发现本任务此前把普通 Debug 的 Engine Profiler 从 OFF 改成 ON，并留在交付配置中。CPUProfileBlock 的 Start/End/Hierarchy 每个事件都会触发记录/互斥锁；这是本次额外 CPU 开销的明确来源，但尚未在用户原场景量化它是否解释全部 20 FPS。最终交付恢复 Engine Profiler OFF；原 Tracy ON 保持不变。
- 按 render-frame-validation 工作流实际捕获了最终 UI Composite：1491×902、engine frame 120、Geometry 与扩展 Texture 各 1 张 PNG；两份 `capture.json` 均 completed / 1 frame / 无 capture error。
- 在临时空项目中创建并选中临时实体、切换 WorldOutline Tab，直接审查图像确认 Inspector 的 Transform/Tag 属性、WorldOutline 实体列表、AssetBrowser 目录/文件夹均可见，右侧与底部不再被裁空。两组临时实体 GUID/列表条目数不同（测试退出保存临时场景），不做整帧像素等价结论；临时场景已恢复为空，生产场景未修改。
- ROI 比较：Inspector Transform/Tag `(1140,110)-(1477,327)` RGB RMS 1.5639 / P99 3；AssetBrowser folder `(253,624)-(343,716)` RGB RMS/P99 0。这里只验证本次内容丢失回归；完整颜色/alpha、输入、滚动、DPI/多窗口 Gate 仍待验收。
- 捕获时 Engine Profiler ON，临时 GPU capture hook 和测试实体 hook 已全部移除。最终正常构建 Engine Profiler OFF 已重新配置，完整 build-debug 正在执行，完成后补最终状态。
- 证据：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/diagnostics/ui-scissor-regression-20261014/{geometry,expanded-texture}/images/session_1/`；正确运行日志与采样 CSV 位于对应 mode 目录。配置/布局/ImGui/运行日志/Material bytes 均按测试前备份恢复；未覆盖用户原有 dirty changes。
- M3 Gate 仍为 PARTIAL / NOT PASSED；新的内容缺失修复有实际 GPU 图像证据，用户原场景的交互与 FPS 需用最终 OFF-Profiler 构建复验。

### 2026-10-14 — Reflector observation and dirty visualization separation

- Reflector visibility no longer forces Widget updates or Geometry fallback. Added default-off `Visualize paint dirty state`; only this mode enables dirty snapshots/indicators and the previous diagnostic fallback. Selection highlighting remains independent.
- Closing Reflector disables dirty visualization. The visualization option explicitly warns that normal cache performance requires it off.
- Earlier instructions to close Reflector for cache measurements are superseded: keep Reflector open with dirty visualization OFF. Reflector/ImGui itself still adds diagnostic CPU overhead.
- Validation: scoped Renderer gate/control/reset static checks PASS; git diff --check PASS; build-debug PASS after rebuilding Engine exports (the initial overlapping build linked Editor against the pre-change DLL and failed LNK2019). Disabled dirty visualization also hides stale tree dirty labels/details. Live performance and visual acceptance remain unverified; M3 Gate remains PARTIAL.

### 2026-10-14 — Hidden nested Tab resize regression

- User reproduction: enable texture cache, hide WorldOutline behind another Tab, resize the host window, then reactivate WorldOutline. Its content retains the old panel dimensions.
- Source diagnosis: DockNodeTabItem::SetRect only propagates layout when its outer node rect changes; nested Tab activation does not change that rect. M1 removed active-item SetRect from DockTab::Update, so newly active nested content can retain stale dimensions indefinitely.
- Fix: synchronize only the active item's current Tab rect immediately before its Update. Existing unchanged-rect guards retain cheap steady-state behavior and hidden items still skip business Update. DockWindow updates Widget/root sizes and invalidates geometry/texture paint through the existing path.
- Validation: cmake --build --preset build-debug PASS; git diff --check PASS; source inspection confirms active rect synchronization precedes business/UI size propagation and retains unchanged-rect guards. Live hidden-Tab resize/capture acceptance not run; no running Editor was available in this tool session. M3 Gate remains PARTIAL.

### 2026-10-14 — AssetBrowser image cache and Slider thumb hover

- Source diagnosis: UIRenderer rejected all non-white standard images and all RenderTexture dependencies, so AssetBrowser content with folder icons/previews always fell back. AssetTypeRegistry publishes stable previews under PreviewRevision; AssetBrowser already rebuilds content when that revision changes.
- Added Widget::SetTextureCacheImageRevision as an explicit owner contract for image/static-preview content. AssetBrowser supplies PreviewRevision after BeginFrame. Untracked image widgets/live view RTs, backdrop and unknown materials retain fallback. Texture/font GPU UploadRevision changes also dirty cached pixels after reupload; revisions are runtime-only and not serialized.
- Slider previously selected its thumb hovered brush from the whole track IsHovered state. It now tracks thumb containment in local coordinates separately, invalidates paint on containment changes, and preserves track click/drag behavior.
- Added UISliderHoverSmoke using actual UI events: track/thumb transition, click-to-value, exit, transformed slider and disabled state. Initial event smoke PASS. Extended GPU image cache test is in progress; initial direct-UI harness lacked normal frame setup and was replaced by the established RenderPipeline acceptance path. These exploratory failures are not cache acceptance evidence.
- Debug build PASS after adding the Input keycode include to the new test. Full AssetBrowser/Inspector interactive visual acceptance remains pending; no computer-use tool or user Editor session is exposed in this tool environment. M3 Gate remains PARTIAL.

- Final verification: UISliderHoverSmoke exit code 0; Slider track/thumb hover, click-to-value, exit, transformed coordinates and disabled checks PASS. Real DX12 frames also PASS untracked-image fallback, audited-image steady cache hit, owner-version repaint/hit recovery, texture reupload revision/repaint/hit recovery, static-preview RenderTexture hit and preview-publication repaint/hit recovery.
- Final evidence: F:/AiluEngine/artifacts/out/test/bin/x64/debug/diagnostics/ui-cache-slider-20261014/verified-preview-smoke/{stdout.log,stderr.log,FileAppender_log.txt}. Temp project/config are isolated under .tmp/ui-cache-slider-20261014; user project/config/layout are unchanged. Source status confirms no remaining default-material dirty change from runtime startup.
- The standalone GPU harness now follows normal resource/scene/render/ImGui/Present steps; initial missing-ImGui Present crash was a harness error. Reupload assertions explicitly wait for the GPU upload fence; a not-ready first frame is allowed to fall back to Geometry. Exploratory directories are not acceptance evidence.
- Final cmake --build --preset build-debug PASS; final git diff --check PASS. These are event/GPU-state smoke results, not PNG analysis or complete AssetBrowser/Inspector live visual acceptance. M3 Gate remains PARTIAL.

### 2026-10-14 — Selected-camera preview on demand (explicit follow-up)

- User scope: keep main SceneView rendering unchanged; refresh selected-camera preview once on selection and thereafter on scene edits.
- RenderPipeline now retains preview output and schedules the extra camera only when selection/target changes or active Scene ID/EditRevision changes. Revision snapshots update after the preview render, not on UI Tick. Repeated edits while unsaved still refresh; scene switching and RT replacement invalidate independently.
- Existing camera property editors call MarkSceneDirty -> Scene::MarkEdited. This is an edit-revision contract: unversioned runtime animation/material/resource changes do not independently trigger preview refresh; temporal preview effects advance only on refresh.
- Extended the existing GPU smoke with preview selection, idle skip, repeated unsaved edits, reselection and RT replacement checks. Build/runtime validation pending. M3 visual/performance Gate remains PARTIAL.

- Final validation: build-debug PASS; git diff --check PASS. UISliderHoverSmoke with real DX12 frames exit 0: initial preview 2 cameras, unchanged 1, each of two unsaved scene edits 2, idle recovery 1, reselection 2, target replacement 2. Existing Slider/image-cache checks also PASS. Evidence: .tmp/preview-on-demand-20261014/{stdout.log,stderr.log}; stderr empty. No interactive Editor visual acceptance or measured frame-time claim; M3 Gate remains PARTIAL.

### 2026-10-14 — Edit-mode GameView camera routing

- Root cause: GameView gated source visibility on Play and the primary camera remained the editor Scene camera in edit mode.
- GameView now resolves the active scene main camera without requiring Play, selects it for the existing primary render output and updates its output size. SceneView restores Camera::sScene on returning; hidden GameView also restores the editor camera. No camera shows the existing message in both modes. Play simulation behavior is unchanged.
- Hidden SceneView unregisters its preview camera, preventing a selected main camera from retaining the small preview RT as GameView output.
- Scope remains the existing shared primary-view output architecture; independent simultaneous SceneView/GameView render targets are not introduced. Final build/static verification pending; no interactive visual acceptance claimed.
- Final validation: cmake --build --preset build-debug PASS (exit 0), including normal reflection generation; git diff --check PASS. Final rerun required access to F: build output after a sandbox write denial. No Editor interaction/capture was run for this change; edit-mode GameView, return-to-SceneView and Play/Stop visual acceptance remain pending.

### 2026-10-14 — Framework hidden-subtree layout invalidation

- Hidden children skip Update and retain local layout dirtiness while ancestors can finish layout. The old InvalidateLayout guard suppressed later propagation, including SetVisible -> InvalidateHierarchy. This allowed first paint at stale origin.
- Removed the local-dirty propagation gate in UIElement::InvalidateLayout. Upward invalidation reaches the root through already-dirty ancestors; explicit downward invalidation visits all descendants. Directions remain exclusive. No SceneView-specific forced layout or delayed visibility was added; normal Update arranges visible subtrees before painting.
- Refinement to proposed early stop: stopping at any already-dirty ancestor is unsafe for nested hidden subtrees, so the implementation traverses the ancestor chain. Cost is proportional to tree depth per invalidation; no extra per-frame tree walk was added.
- Added actual-coordinate smoke assertions for first show after hidden position edits, nested hidden parent with child position/size edits, and reshow after parent resize/move. Validation pending.
- Final validation: build-debug PASS; scoped git diff --check PASS. UISliderHoverSmoke exit 0: first-show coordinates, nested hidden position/size changes and parent resize/move checks PASS; existing Slider, on-demand preview and DX12 image/RT cache checks also PASS. Evidence: .tmp/hidden-layout-20261014/{stdout.log,stderr.log}; stderr empty. Isolated temporary project used; no live Editor preview-window visual acceptance or performance claim. M3 Gate remains PARTIAL.

### 2026-10-15 — Persist experimental Widget cache preference

- Added reflected `EngineConfig::_enable_editor_widget_texture_cache`, default false, and the explicit JSON default in `Editor/EngineConfig.json`. Normal Application load/finalize handles persistence; older files without the field retain the default.
- Reflector edits the config value directly. Toolbar/status, Dock title/tab/content construction and post-layout policy use the same preference. The profiling environment override remains process-local and is not persisted.
- Validation: normal AiluHeadTool generation PASS; static checks confirm JSON default false, reflected bool serialization/deserialization registration and direct Reflector binding. `cmake --build --preset build-debug` PASS (exit 0), including Engine, Editor and test targets; scoped `git diff --check` PASS. No live Editor persistence/interaction/FPS run was performed. This configuration change does not establish FPS recovery or resolve the previous Profiler-OFF shutdown findings; M3 Gate remains PARTIAL.
