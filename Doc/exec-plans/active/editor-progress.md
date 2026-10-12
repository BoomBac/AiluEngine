# AiluEngine Editor Progress System — ExecPlan

> 状态：M1 及追加样式均经用户验收；M2 代码/Debug 构建/队列回归测试通过，等待用户自行交互验收 | 规模：小～中型、单文件 ExecPlan | 目标：轻量、可并发、线程安全的编辑器进度提示 | 输入基线：`AiluEngine_Source(20261010-033933).zip`，实施以当前 checkout 源码为准
>
> **执行规则**：从 M1 开始；每个 Milestone 必须独立通过编译及验收才允许进入下一阶段。优先复用现有 UI、JobSystem 和编辑器生命周期；不要为了展示进度重构资源系统。

## Purpose

为编辑器中的资源导入、Shader 编译、批量处理等耗时操作提供统一进度报告。运行任务显示在窗口右下角、状态栏上方；支持多个任务并发、确定/不确定进度、完成、失败与协作取消。业务方只更新任务状态，不直接操作 UI。

## Scope

- `EditorProgressService`：创建任务、线程安全更新、快照读取、完成态保留与清理。
- `EditorProgressHandle`：供同步命令/JobSystem 使用的轻量任务句柄。
- `EditorProgressPanel`：基于现有自研 UI 的右下角浮动任务面板；同时在现有底部状态栏展示摘要。
- 一个开发用的模拟任务入口，用于验证进度条和并发/取消/失败。
- 第二阶段接入 `AssetImportController` 的文件队列进度（遵守现有弹窗/主线程约束）。

## Non-goals

- 不实现任务执行框架、线程池、JobSystem 改造、命令继承体系、通用 DAG / 加权子任务树。
- 不实现全局任务历史持久化、ETA 估计、操作系统级进度、运行时游戏加载页。
- 不把 `ResourceMgr::ImportResource()` 或场景 / GPU API 在未经审查时转移到工作线程。
- 不承诺单个同步耗时函数执行期间编辑器仍能刷新；仅报告其调用前后状态无法得到内部实时进度。
- 第一版不要求新增可序列化的 `Engine::UI::ProgressBar` 类型；先用现有 UI 组件实现，必要时再抽成通用控件。

## Architecture Constraints

1. **职责隔离**：任务只是状态，不承担调度；ProgressService 不依赖 Widget/ImGui，Panel 不执行任务。
2. **线程模型**：业务线程只调用 Handle API；仅编辑器主线程创建/更新/销毁 UI。读写任务集合使用 mutex；计数/状态/取消标记优先 atomic；文本使用短临界区锁。
3. **生命周期**：Service 随 EditorApp 初始化/关闭，Panel 随 EditorLayer OnAttach/OnDetach；关闭时禁止创建新任务，保证不留悬空服务/Widget 指针。后台任务的停止与等待遵循现有 JobSystem 生命周期，不能靠进度条取消强行杀线程。
4. **成本**：避免每次 `Advance()` 分配内存；尽量不在工作线程频繁格式化字符串；UI 仅变更实际发生变化的文案/进度，不每帧重建 Widget 树。注册表锁禁止包住 UI 调用或任意业务回调。
5. **真实进度**：`total == 0` 表示不确定进度；未知工作量不可伪造百分比。已知工作量按 `completed / total` 计算，展示时 clamp 到 `[0, 1]`。
6. **状态一致性**：运行中 → 取消请求中 → 已取消，或运行中 → 完成/失败。终态不可被后续更新覆盖；重复 `Complete/Fail/Cancel` 幂等；请求取消 != 已取消。
7. **代码风格**：成员变量 `_snake_case`，普通变量 `snake_case`，函数/类型 PascalCase，常量 `kCamelCase`；尽可能控制在 160 列。避免新增不必要抽象和虚函数。

## Baseline

已核对上传快照中的以下现状：

| 位置 | 当前行为 | 对应改造点 |
| --- | --- | --- |
| `Editor/Src/Widgets/EditorLayer.cpp` | `BuildEditorChrome()` 创建工具栏和底部状态栏；`UpdateEditorChrome()` 调整位置并写 `_status_left_text`、`_status_right_text` | 进度面板创建、位置更新和状态栏摘要 |
| `Editor/Inc/Widgets/EditorLayer.h` | 持有 `_status_bar_widget` 等控件 | 新增面板/摘要控件引用并在 OnDetach 清理 |
| `Engine/Inc/UI/UIFramework.h` | `UIManager::RegisterWidget / UnRegisterWidget / BringToFrontSilently` | 独立 Widget 的层级、注册和注销 |
| `Engine/Inc/UI/Basic.h`、`Container.h`、`UISlot.h` | 已有 `Border`、`Text`、`Button`、`Canvas`、`VerticalBox`、`CanvasSlot` | 优先组合出进度条和浮动面板 |
| `Engine/Src/UI/Table/TableRenderer.cpp` | 已有 `TableRenderer::DrawProgressBar()` 的矩形填充逻辑 | 可参考画法；无需强行让 TableRenderer 管理编辑器任务 |
| `Engine/Inc/Framework/Common/JobSystem.h` | 已有 `Dispatch`、`WaitHandle`、`JobGroup` | 后台任务可持有 Handle 报告进度；不改调度层 |
| `Editor/Src/Widgets/AssetImportController.cpp` | `QueueFiles()` → `ProcessNext()`；直接导入及导入弹窗回调同步调用 `ResourceMgr::ImportResource()` | M2 只做真实队列/文件级进度；不可假定导入线程安全 |
| `Editor/Src/Widgets/AssetBrowser.cpp` | `AssetBrowser::Update()` 存在，`_import_controller` 为成员 | 如需分帧处理队列，可在此调用 Controller::Tick |
| `Editor/Src/Automation/AutomationService.cpp` | `Tick()` 通过 `DrainRequests()` 在主线程直接执行请求 | 本次不自动包装所有请求，以免错误宣称主线程长命令可实时刷新 |

## Target Outcome

- 任意编辑器模块能用少量代码创建、更新和结束进度任务。
- 右下角只在有活动任务/待展示终态时显示，多个任务互不干扰；不覆盖工具栏、状态栏和模态弹窗。
- 状态栏保留已有 Scene/Selection/FPS/Frame 信息，并能显示运行任务摘要。
- 后台任务报告进度时 UI 可正常刷新；单线程同步导入的界限在代码及文档中明确，不伪装为异步。
- 完成后自动隐藏；失败保留信息可手动关闭；点击取消只是发出请求，实际退出由业务代码负责。

## Relevant Files

预计新增（具体位置按工程现有包含路径及编译规则调整）：

```text
Editor/Inc/Common/EditorProgressService.h
Editor/Src/Common/EditorProgressService.cpp
Editor/Inc/Widgets/EditorProgressPanel.h
Editor/Src/Widgets/EditorProgressPanel.cpp
```

预计修改：

```text
Editor/Inc/EditorApp.h
Editor/Src/EditorApp.cpp
Editor/Inc/Widgets/EditorLayer.h
Editor/Src/Widgets/EditorLayer.cpp
Editor/Inc/Widgets/AssetImportController.h       # M2
Editor/Src/Widgets/AssetImportController.cpp     # M2
Editor/Src/Widgets/AssetBrowser.cpp              # M2，只有采用分帧队列时
```

如果现有构建脚本需要手动列举 `.cpp`，同步更新；禁止无关的 UI、资源系统大规模重构。

## Milestones

### M1 — 进度服务 + 右下角 UI + 模拟任务

**实施细节**

1. 定义公开数据类型：

   ```cpp
   enum class EProgressState : u8 { kRunning, kCancelling, kCompleted, kFailed, kCancelled };

   struct ProgressSnapshot
   {
       u64 _id = 0;
       String _title;
       String _message;
       u64 _completed = 0;
       u64 _total = 0;
       EProgressState _state = EProgressState::kRunning;
       bool _can_cancel = false;
   };
   ```

2. 实现紧凑的外部接口（可微调签名匹配工程容器/字符串类型，但不要改变语义）：

   ```cpp
   class EditorProgressHandle
   {
   public:
       void SetTotal(u64 total);
       void SetCompleted(u64 completed);
       void Advance(u64 count = 1);
       void SetMessage(StringView message);
       bool IsCancellationRequested() const;
       void Complete();
       void Fail(StringView reason);
       void Cancel();
       u64 Id() const;
   };

   class EditorProgressService
   {
   public:
       EditorProgressHandle BeginTask(StringView title, u64 total = 0, bool can_cancel = false);
       void RequestCancel(u64 task_id);
       void GetSnapshots(Vector<ProgressSnapshot>& out_snapshots) const;
       void Tick(f32 delta_time);
       void Shutdown();
   };
   ```

3. **任务数据与并发**：任务注册表存放 `shared_ptr<ProgressTask>`；Handle 也持有相同任务对象，确保工作线程使用时任务不会被清理释放。`_completed/_total/_state/_cancel_requested` 用 atomic；标题只初始化一次、消息使用 mutex。快照先在短注册表锁下复制任务指针，再逐项读取，避免长时间持锁。终态转换使用 CAS 或等效同步措施。`Advance()` 对零总量合法，对已结束任务无效；不发生无符号溢出。
4. **句柄契约**：Handle 可复制以便 JobSystem 捕获；显式调用 `Complete/Fail/Cancel`，确保每条退出路径落入终态，异常捕获后 `Fail()` 并记录日志。不在析构时偷偷 `Complete()`。不要在持有注册表锁时触发任务回调。
5. **保留/清理**：运行任务持续可见；完成/已取消显示约 2 秒后移除；失败保留直到用户关闭（可添加 `DismissTask(id)`）；提供对失败任务的有限数量上限以免无限增长。不需要数据库或任务历史。
6. **右下角面板**：使用单独 `UI::Widget`、`Border + VerticalBox + Text + Button`，进度条可由 Canvas 背景条和填充条组成。确定进度显示百分比，不确定进度用无百分比的动画/文字。默认展示最多 3 项，其余显示“+N tasks”。宽度可取约 320 px，距窗口右缘 12 px，底部位于状态栏上方 12 px；窗口缩放时重新计算并做边界裁剪。
7. **交互与层级**：Panel 有内容时注册/显示，无内容时隐藏或注销；不使用覆盖全屏的可交互 Widget；Cancel 按钮只调用 `RequestCancel(id)`，此后显示 Cancelling，业务正式退出后再显示 Cancelled；失败项可 Dismiss。模态弹窗必须高于进度面板。注意目前 `_status_bar_widget->_is_receive_event = false`，如果需要点击状态栏，应另外创建可交互控件，不要让整条状态栏拦截事件。
8. **状态栏**：在 `BuildEditorChrome/UpdateEditorChrome` 添加运行中任务数和最主要任务的短摘要；不要覆盖 `_status_right_text` 原有 FPS 信息；只有文案变化才更新文本。
9. **调试入口**：添加仅开发/Debug 可触发的模拟任务（例如 Editor 菜单项），使用 JobSystem 驱动 3 个不同节奏的任务，覆盖：正常完成、失败、可取消、不确定进度；模拟用分步休眠仅限测试任务，不能在主线程 Sleep/Wait。

**M1 Acceptance Criteria**

- [ ] 可成功编译并启动编辑器；无 UI 反射代码生成、链接或注册失败。
- [ ] 可同时显示 3 个任务：百分比彼此独立，消息与状态正确，拖动/缩放编辑器窗口不造成布局错误。
- [ ] 未知总量显示不确定进度；`total == 0` 不会除零，`completed > total` 不会越界显示。
- [ ] 点击取消时显示 Cancelling；工作任务在安全点退出并报告 Cancelled；没有强制结束线程。
- [ ] 某任务失败后保留原因，可关闭；成功/取消任务短暂保留后自动消失。
- [ ] 快速连续开始/结束任务，以及后台线程更新与关闭编辑器同时发生时，无数据竞争、崩溃、UAF 或滞留的 Widget 注册项。
- [ ] 进度数值更新不导致整个 Widget 树每帧重建；空闲时无不必要的高频分配/刷 UI 文本。
- [ ] 记录本阶段实际验证的构建目标、命令、结果；未运行的测试不得标为通过。

**通过 M1 后，才执行 M2。**

### M2 — 接入实际资产导入队列

**实施细节**

1. 在 `AssetImportController::QueueFiles()` 为当前导入批次创建任务，`total` 为当前批次待处理文件数；每个文件导入成功后 `Advance(1)`，消息显示当前文件名。新的文件在同一批次追加时应正确更新 `total`，不要重复创建大量任务。
2. 保持 `Texture / Mesh` 导入选项弹窗的现有行为。等待用户选择设置时应显示合适的 Waiting/Preparing 消息，**不可在用户尚未确认时计为完成**；用户取消该文件时，记录为跳过并推进批次处理计数，完成提示应能区分“成功 N 个 / 跳过 M 个 / 失败 K 个”。
3. `ProcessNext()` 当前通过 `while` 连续同步处理所有直接导入文件，会阻塞一帧。改为轻量的逐帧调度（例如 `AssetImportController::Tick()`，在 `AssetBrowser::Update()` 调用，每帧最多启动一个直接导入项），确保多个文件之间 UI 有绘制机会。不要在函数内部调用阻塞等待来“刷新 UI”。
4. **单文件导入限制**：现有 `ResourceMgr::ImportResource()` 仍可能在主线程同步耗时。此阶段只能展示文件级/批次级进度；单文件内部进度和不卡顿不作为通过条件。不得把它直接 Dispatch 到 JobSystem。若有已验证的线程安全拆分点，可以补充，但必须单独给出依据和测试。
5. **取消规则**：对尚未开始的排队文件允许请求停止后续处理；正在执行的同步 ImportResource 不可强制中止。只在确实支持取消队列时启用 `can_cancel`。回调和导入后的 `NotifyImported()` 应保持原语义，不重复、不漏触发。
6. **异常/失败**：检查 `ImportResource()` 的返回约定和异常路径，确保失败不误计为成功。最后一个文件处理后，依据统计调用 `Complete()` 或 `Fail()`，并显示摘要。无论成功、用户取消还是导入失败，都不可让进度永远停留在 Running。

**M2 Acceptance Criteria**

- [ ] 批量拖入文件后显示 1 个导入批次任务，百分比与已处理文件数一致。
- [ ] 直接文件按帧推进，不在单个 `ProcessNext()` 调用中连续吞完全部队列；弹窗设置操作与目录切换行为不回归。
- [ ] Texture / Mesh 弹窗确认、取消均不会跳错队列项、重复导入或遗漏计数；追加文件时总数正确。
- [ ] 确实能取消**尚未开始**的文件；不能取消的同步内部操作不展示虚假的“立即停止”行为。
- [ ] 一次导入失败不会导致状态泄漏、死队列或后续文件无响应；刷新通知次数正确。
- [ ] 明确记录同步单文件 ImportResource 仍可能卡帧的限制，并把进一步优化写入 Known Follow-ups；不得宣称已经异步化。

## Current Milestone

`M2 — 接入实际资产导入队列`（2026-10-10 用户确认 M1 与追加样式均通过，授权继续 M2）。

## Progress

- [x] 核对上传版本中的 `EditorLayer`、UIManager、JobSystem、AssetImportController、AssetBrowser 入口。
- [x] M1 代码实现。
- [x] M1 编译、交互与并发验证（构建/状态测试由代理执行，交互验收由用户确认通过）。
- [x] M2 真实资产导入接入。
- [x] M2 Debug 构建与独立队列回归验证。
- [ ] M2 真实导入与弹窗交互验收（用户自行验证）。

## Decision Log

| 决定 | 原因 |
| --- | --- |
| 任务进度与执行解耦 | 复用现有 JobSystem，也支持同步命令；不增加另一套调度器。 |
| 单独 Editor Service + 自研 UI Widget | 不污染 Engine 的通用任务层，不依赖 ImGui。 |
| 第一期不实现子任务树/ETA | 控制功能边界、复杂度与代码量。 |
| 已知进度用文件/工作单位，未知用不确定进度 | 避免“进度条只是在动，但数字不真实”。 |
| 资产导入先接文件级而不是内部阶段级进度 | 现有 ImportResource 同步执行，不能未经审查改线程模型。 |
| 每项先准备一帧，再同步执行；完成后下一帧调度 | 文件名可在阻塞调用前绘制；QueueFiles/弹窗回调不递归吞完整个队列。 |
| EditorPopup 增加可选关闭回调和 popup group id | 外部关闭也记录跳过；只关闭本控制器的弹窗，回调由 weak lifetime token 保护。原有调用保留默认行为。 |
| 修复 ImportResourceImpl 的空返回缺陷 | ret_res 原先声明后一直为空；仅在 CreateAsset 成功时记录首个对象，否则所有成功导入都会被错误计为失败。 |

## Surprises & Discoveries

- M2：控制器显式借用 EditorApp 的进度服务；Browser 销毁早于 UIManager/服务本体销毁。析构先使回调 token 失效，再注销自己的弹窗组并终结批次。
- Texture/Mesh 按钮确认、Cancel、Escape/外部关闭都解除等待；已完成当前项后关闭回调不再计数。取消批次只在主线程安全点清理队列，剩余文件记为 skipped。
- ImportResource 的 null 同时覆盖失败、重复资源和无产出；现有接口没有细分结果或子资产失败信息，M2 对 null 记 failed/no resource produced，不假定成功。
- 独立队列测试接受替代 ImportCallback，不启动 GPU/Editor，不覆盖真实解析器或模态 UI；用于验证逐帧调度、计数、通知与异常/取消状态流。

- 2026-10-10 用户确认“M1验收通过”，新增样式请求“圆角+背景模糊”。复用 `kBackdropBlur` / `ResolveBackdropBrush`，每项卡片使用 10px 圆角和 0.72 alpha 深色 tint；进度条左右各 8px 留白、3px 圆角。不改 UIRenderer 或任务状态契约。

- 2026-10-10 实际源码核对：UIElement 使用 `SetVisible()` 更新层级/绘制失效；进度面板不新增反射类型。Border 颜色需通过 style override 设置，避免主题覆盖。
- 更新、消息和终态由同一任务短锁串行化，保证结束后计数/文案也不再改变；数值与状态保留 atomic。注册表锁不会跨 UI 调用。
- EditorApp 拥有 Service 和模拟任务 JobGroup，Finalize 先禁止创建并请求停止，然后等待模拟 jobs，之后才释放 Editor/Engine。后台 lambda 只捕获任务句柄与整数。
- 本机现有源码是 OneDrive reparse 文件，构建输出在 F:/AiluEngine；沙箱内写入被拒绝，已使用受审查权限完成任务相关修改/构建。
- 仓库 `Doc/exec-plans/TEMPLATE.md` 不存在，使用用户提供的完整 ExecPlan 作为本任务记录。

- `EditorLayer` 目前状态栏是 `UI::Widget`，不是 ImGui 窗口；应复用其布局、主题及注册方式。
- `AssetImportController::ProcessNext()` 既负责队列又负责等待设置弹窗；改分帧时尤其要防止 Tick 重复打开同一个弹窗。
- `AssetBrowser::Update()` 存在可用的分帧入口；不过单文件 `ImportResource()` 本身仍会阻塞执行它的那一帧。
- `TableRenderer::DrawProgressBar()` 已有类似绘制，但不是独立通用控件；无需为本任务搬迁整个 UI Table 系统。

## Validation

实现过程中填写实际结果，不预填“通过”：

| 项目 | 命令 / 操作 | 结果 / 证据 |
| --- | --- | --- |
| 编译 | `cmake --build --preset build-debug` | 通过（exit 0）；Editor、Engine、EditorProgressSmoke 及默认全部目标。首轮 Editor 因误用 `kCollapsed` 失败，改用现有 `SetVisible()` 后通过。AiluHeadTool 正常更新 EditorApp.gen.h 宏行号。 |
| M1 状态/并发测试 | `F:/AiluEngine/artifacts/out/test/bin/x64/debug/EditorProgressSmoke.exe` | 通过（exit 0）；4 线程共 40000 次 Advance；Cancelling/Cancelled；终态后更新无效；零总量及饱和计数；失败上限 16/手动移除；2 秒清理；100 次生产者与 Shutdown 交错；服务销毁后句柄存活。此测试不等价于数据竞争检测器或 UI 验收。 |
| M1 Editor 启动 | Debug Editor.exe + `C:/Users/liusongchen_e/OneDrive/AiluTestProj/rg_acceptance.json` | 第二次测试启动日志显示 `EditorApp::Initialize 0 cost 53027.5ms`，随后进入 MainThread/RenderThread 渲染。不能凭启动日志认定视觉正确。 |
| M1 并发面板/取消/窗口交互 | Debug Menu > Progress Demo；computer-use window activation/screenshot | 未通过验收：初始化后两次窗口截图 `window capture timed out: timed out waiting on channel`；初始化期间也出现 activate_window timeout / FrameArrived timeout。没有可靠截图，未点击任务按钮，未验证窗口缩放、模态遮挡、真实正常关闭。 |
| M1 静态差异检查 | `git diff --check` | 通过；控件树创建一次，slot setters 对相同值短路；空注册表快照不分配；没有修改 AssetImportController/ResourceMgr/JobSystem。 |
| M2 导入回归 | 多文件、弹窗确认/跳过、追加、失败、取消队列 | 未执行、未实现；M1 交互验收未通过，按阶段门禁不进入 M2。 |

后续验收记录（覆盖上表的原始待验收状态）：2026-10-10 用户直接确认“M1验收通过”。同轮请求修改弹出面板为圆角与背景模糊；本次只处理该样式请求。
样式验证：`cmake --build --preset build-debug` exit 0，`git diff --check` exit 0；复用现有 UI blur/shader/资源生命周期，未新建模糊 pass。
本轮未启动 Editor 或捕获新样式图像，视觉效果不标记为已验收。现有用户 dirty 配置/布局保持原样。

### M2 实施验证（2026-10-10）

- 用户随后确认样式“通过”，授权继续 M2。
- `cmake --build --preset build-debug` exit 0，Engine/Editor/AssetImportQueueSmoke 等全部默认目标编译链接成功。初轮测试误用非静态 LogMgr::Initialize，改为 LogMgr::Init 后通过。
- `AssetImportQueueSmoke.exe` exit 0；临时将 Debug Editor 目录加入 PATH，满足测试目录缺少的 Engine 依赖 DLL。测试覆盖准备帧/每 Tick 最多一个导入、同批追加和总数、目标目录不变、null/异常后继续、仅成功项刷新通知、开始前取消、第一项后取消、同步调用期间请求取消但当前项仍完成、控制器销毁和服务拒绝新任务。
- `git diff --check` exit 0（已有用户配置的换行提示与本次功能无关）。
- 隔离空项目 Editor 初始化返回 0，但首帧崩溃在 PostprocessSettings.cpp:14 / Entity.inl:186 的 PostprocessVolumeComponent View；还未执行任何导入，不能认定 M2 运行验收通过。日志/配置备份位于 `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/ImportValidation_20261010/`。

### M2 人工验收步骤

2026-10-10 用户要求“我自行交互验证”。代理停止自行交互验证，结束自己启动的测试 Editor，并恢复本轮启动前备份的六个配置/材质文件；逐个哈希核对一致。以下步骤由用户执行，尚未记为通过。

1. 重启新构建的 Debug Editor，打开现有可正常渲染的项目；拖入多个有效 wav/ogg 等直接导入文件，检查只有一个 Asset import 批次，逐项推进并能看到 Preparing 文件名。
2. 拖入 Texture/FBX，等待设置弹窗时进度不增长；分别确认、Cancel、Escape/关闭，检查成功/跳过统计及后续项继续，不重复导入。
3. 等待弹窗期间追加文件并切换 Browser 目录，检查总数增长、原文件仍使用排队时目标目录、新文件使用追加时目录。
4. 直接导入或弹窗处理项之间点击进度面板 Cancel，检查剩余项不执行且 batch 进入 Cancelled；同步函数内不能强制中止，单文件卡帧仍允许。
5. 混入不支持的文件/无效文件，检查后续有效文件仍能导入，终态 Failed 保留统计与原因；确认成功项各通知刷新一次，跳过/失败不伪计成功。
6. 等待设置时关闭 Browser/Editor，检查没有悬空弹窗回调、滞留 Running 任务或重复通知。

### M1 人工验收步骤（解除门禁）

1. 使用 `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe` 打开项目；点击工具栏 Menu > Progress Demo。
2. 检查 3 个任务，两个独立百分比和一个不确定动画；状态栏保留 Scene/Selection/FPS/Frame；面板位于右下角状态栏上方。
3. 立即取消未知总量任务，观察取消请求到业务安全点确认。Cancelling 可能仅持续一个 160ms 工作步；不能把瞬时 Cancelled 当成已观察 Cancelling。
4. 约 6 秒失败任务应保留原因并能 Dismiss；约 8 秒成功任务结束后保留 2 秒；检查没有空面板残留。
5. 连续触发两轮以上，检查最多三行和 +N tasks；缩放/拖动/最大化窗口，打开模态弹窗，确认不遮挡弹窗且不截获面板之外的输入。
6. 活跃任务期间正常关闭 Editor，检查停止和 job join、OnDetach 注销、日志/异常；未实际执行此步骤前不能标记完成。

测试日志和配置备份保存在实际二进制旁 `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/ProgressValidation_20261010/`。

最终验证：补充进度条背景和极小窗口隐藏边界后，再次执行 `cmake --build --preset build-debug`，exit 0。
本次运行改写了 EditorConfig/main_widget/dock_layout；配置按启动前精确备份恢复，dock_layout 按初始 clean checkout 恢复，最终无功能差异。
测试进程已清理；日志包含 Finalize/Allocator 泄漏报告，但没有在活跃模拟任务期间受控正常关闭的证据，仍不标记关闭验收通过。

不能运行编辑器或缺少构建环境时，必须写明未验证内容、阻塞原因和具体人工验收步骤，不得静态分析后声称运行通过。

## Known Follow-ups

- 单文件返回值仅表示至少一个资产创建成功；重复/无产出均为 null，部分子资产失败无法用现有返回值分辨，若需细分统计应单独扩展导入结果契约。

- 如果确需**单个大 FBX/纹理导入时也持续响应**：另立任务，先梳理 CPU 解析、主线程提交、GPU 上传的线程归属，再提供内部阶段进度/后台拆分。
- 需要复杂任务汇总时再考虑带权子阶段；需要多个命令自动上报时再考虑包装 Command/Automation 层。
- 如同样需求延伸至游戏运行时 UI，再提取 `Engine::UI::ProgressBar`，而非提前把 EditorService 下沉到 Engine。

## Outcomes

M1 服务、面板及追加圆角/背景模糊样式均已由用户验收。M2 已接入真实资产导入队列：批次进度、逐帧推进、等待设置、成功/跳过/失败统计、后续队列取消及回调生命周期保护；修复 ResourceMgr 成功导入仍返回 null 的问题。Debug 构建和 AssetImportQueueSmoke 通过，真实导入与弹窗交互由用户自行验收。单文件 ImportResource 仍在主线程同步执行，内部执行期间可能阻塞刷新。M2 尚未完成交互验收，任务整体保持 active。

## Resume Protocol

1. 每次恢复工作先阅读本 ExecPlan 的 `Current Milestone`、`Progress`、`Decision Log`、`Validation`，检查代码现状和未提交修改。
2. 只处理 `Current Milestone`，不要提前实现下一阶段或顺手大改其他系统。
3. 每完成一个可运行/可验证的步骤，更新 `Progress` 与 `Surprises & Discoveries`；出现设计变更写入 `Decision Log`。
4. Milestone 验收通过后在 `Validation` 写入**实际执行**的证据，再更新 `Current Milestone`；不通过则修复，不能跳关。
5. 全部完成后填写 `Outcomes` 和 `Known Follow-ups`，输出简洁的变更总结与残余风险。
