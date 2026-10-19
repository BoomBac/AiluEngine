# M1 — Batch Runner + 隐藏窗口兼容模式

**状态**：Not Started。**前置**：读取 `README.md` / `progress.md`，完成完整仓库基线调查。**本阶段独立交付**：通过一次 CLI 调用实现静默启动→选择测试→运行→写 JSON/日志→关闭进程；GPU 仍可使用现有 D3D12 + SwapChain。

## Purpose

以**最小侵入**建立第一套可由 Codex / Claude 自动启动和判读的稳定 Batch 能力，为下一阶段真 Offscreen 预留清晰的能力契约。

## Scope

- `--batch --graphics hidden-compat` 的稳定启动和关闭，不出现可见窗口、弹窗、人工交互；保留兼容窗口/SwapChain/Present。
- 解析参数、合法性验证、通配符枚举/选择测试、同步和跨帧测试生命周期、整体/单测试 deadline、结构化报告、退出码。
- 复用现有 AutomationService/Registry 和 Scene/Asset/Reflection/Write/Capture 现有命令，不复制处理逻辑。
- 将 Automation 请求的执行泵从 EditorLayer 的 UI 更新中解绑，保证常规 Editor 功能不退化。
- 默认只读，自动化测试数据与用户工程、编辑器配置、Session 文件隔离。

## Non-goals

- 不是“真正 offscreen”：内部存在 HWND + SwapChain，不能声明在没有图形桌面的 Windows Service 环境可用。
- 暂不实现 `--graphics offscreen`、`--graphics none`、常驻 `--listen`、CPU-only ResourceMgr。
- 暂不重构所有资源系统或引入测试发现的复杂插件框架。

## Architecture Constraints

1. **CLI 前向兼容**：不改变原有项目位置参数和现有三个特别开关。组合冲突必须明确报错（不能静默忽略）。
2. **Batch 用 Window 与 Normal Editor 共用代码路径**，仅增加显示策略，不 fork `WinWindow` 实现；`hidden-compat` 不调用 `ShowWindow(SW_SHOW)`。
3. **Batch 默认禁用写入**：禁止保存全局 EngineConfig/EditorConfig、迁移正式工程资源、覆盖 `.ailu/editor_session.json`；可以写日志和 TestResults 隔离目录。
4. **Test Runner 不能吞错**：0 条匹配/测试异常/报告写入失败/超时均必须失败。Fail 不因跳过其它测试而转为成功。
5. **受信任的权限上下文**：权限由 CLI 选择的本地执行策略与写入沙盒得出。Pipe 内请求上下文只是请求，不是授权；修复时保持正常 Editor 的既有安全策略和客户端兼容。
6. **生命周期单一入口**：在主线程执行对象/资源/场景操作；`Tick` 既有请求队列与测试状态机在稳定帧边界运行。不得由 Pipe 工作线程直接操作 SceneMgr/ResourceMgr。
7. **单线程先行**：如构建启用 `SEPARATE_LOGIC_THREAD` 或多线程渲染而主线程归属不一致，M1 Batch 先显式选择经过验证的串行执行路径；保留正常 Editor 的多线程行为。
8. 类型和函数 CamelCase、变量 snake_case、成员 `_snake_case`，每行尽量 <160 列；新增文件尽量少且职责明确。

## Baseline

- `EditorMain.cpp` 使用 `WinMain` / `CommandLineToArgvW`；项目路径从 `context._arguments[1]` 取，缺项目会 `MessageBoxW`。
- `EditorMain.cpp` 支持 profile/smoke/exit-after-frames，通过 `_before_update` / `_after_update` 委托触发动作，最后 `app.Tick()` 持续运行。
- `Application.cpp` 初始化 `WindowFactory::Create`、Input、Runtime；循环有分段方法，但默认都执行；`Finalize()` 持久化引擎配置。
- `WinWindow.cpp` 创建 HWND 后 `ShowWindow(_hwnd, SW_SHOW)`。
- `EditorApp.cpp` 自动加载 Plugin、UI、场景、资源迁移；创建 `EditorAutomationService` 和 `AutomationPipeServer`，写入 AutomationSession；Finalize 保存 EditorConfig。
- `EditorLayer.cpp::OnUpdate()` 驱动 `automation->Tick()`。
- `AutomationService.cpp` 已有 `Submit()` → pending queue → `DrainRequests()` → 私有 `Execute()`，权限含 ReadOnly/SafeWrite/DestructiveWrite/ExternalEffect。

## Target Outcome

```powershell
# 下列均为本里程碑完工后预期可执行的命令
AiluEditor.exe "D:\Projects\Fixture.ailuproject" --batch --graphics hidden-compat --list-tests --report "D:\Reports\list\result.json"
AiluEditor.exe "D:\Projects\Fixture.ailuproject" --batch --graphics hidden-compat --run-tests "Automation.*" --report "D:\Reports\automation\result.json" --timeout-seconds 120
```

单次调用的客观成功条件：无可见 UI / 无输入依赖；测试报告有效且语义准确；进程有限时间内退出；Windows 外部调用方能获取真实 Exit Code；用户正式项目和配置文件无非预期修改。

## Relevant Files

**修改优先**：

- `Editor/Src/EditorMain.cpp`（CLI、Batch 入口、退出码）；`Editor/Src/EditorApp.cpp`、`Editor/Inc/EditorApp.h`（Batch 初始化及清理隔离）。
- `Engine/Inc/Framework/Common/Application.h`、`Engine/Src/Framework/Common/Application.cpp`（运行选项、受控步骤与退出机制）。
- `Engine/Src/Platform/WinWindow.cpp`、`Engine/Inc/Framework/Common/Window.h`（隐藏窗口能力）。
- `Editor/Src/Widgets/EditorLayer.cpp`（移除服务 Tick 的唯一依赖）；`Editor/Src/Automation/AutomationService.cpp`、`AutomationTypes.h`（复用执行/授权）。
- `Editor/Src/Automation/AutomationSession.cpp`（只在正常/经允许的交互模式写会话）。

**建议新增最少的设施**（根据真实项目目录决定，避免固定假路径）：`AutomationTestRegistry`、`AutomationTestRunner`、`AutomationTestReport`，必要时单独 `EditorBatchOptions`。

## Implementation

### Step 0 — 环境与兼容基线（先完成）

- 在完整仓库记录 commit、编译工具链、配置、Editor 构建命令、测试工程和是否开启 `DEAR_IMGUI`、`SEPARATE_LOGIC_THREAD`、多线程渲染。
- 运行正常 Editor 启动/退出；如工程支持，运行旧 `--exit-after-frames 8` 和一个已有 smoke 用例并记录 log、退出码、文件副作用。
- 使用哈希或清单记录测试工程的 `Assets` / `.ailu` / EditorConfig / EngineConfig 文件状态；确认应用是否修改资源和偏好文件。先找到副作用再确定隔离方式。
- 观察 `ResourceMgr::MigrateLegacyAssetDocuments()`、插件 autoload 及 `AutomationSession::Save` 是否在测试时写入。必须能解释 M1 的默认写入行为。

### Step 1 — 清晰的命令行契约

- 增加 `BatchOptions`（或者合并至适当的 `ApplicationInitContext`）：`_batch`、`_graphics_mode`、`_test_filter`、`_list_tests`、`_report_path`、`_timeout_seconds`、`_run_id`、`_artifact_dir`。
- 命令解析**先于**需要 Window/UI 的初始化；缺参数、未知选项、非法数字、冲突组合、无工程、无测试筛选器应打印可机器读取的错误并返回 `2`，Batch 不弹 `MessageBoxW`。
- `--graphics hidden-compat` 仅合法于有 D3D12 的路径；`offscreen`/`none` 在 M1 返回明确 `unsupported_graphics_mode`，不可 fallback 到 `windowed`。
- `--list-tests` 与 `--run-tests` 不能同时执行；`--batch` 没提供执行目标时显示 usage 并返回错误（避免运行无限循环）。保留旧选项意义及冲突处理。
- 使用 `std::filesystem::absolute(...).lexically_normal()` 规整工程和报告路径；覆盖既有报告应有显式策略，推荐默认分配 `run_id` 独立文件夹。
- 非交互日志写入独立 `run_dir/engine.log`，包含 Info 级进度；可选附加 stdout/console sink，不能以窗口日志作为唯一信息源。

### Step 2 — Window 的隐藏兼容模式

- 增加明确的窗口初始显示策略（例如 `EWindowFlags::kWindow_Hidden`，按现有风格实现）；在 `WinWindow.cpp` 仅有条件调用 `ShowWindow` / `UpdateWindow`。
- 确保隐藏不改变 HWND 生命周期；仍完成 `RegisterWindow` / `ResizeSwapChain`，且不会窃取焦点或意外弹到任务栏。
- 此阶段不要在 DX12 backend 伪造无窗口交换链。若驱动/交换链在隐藏 HWND 下不支持某路径，记录原因并调整兼容策略，不能以 Windowed 模式冒名顶替。
- 记录并测试 `WM_CLOSE`、Resize、焦点事件对 Batch 的影响。不要用注入 `WM_CLOSE` 作为测试成功的唯一判断。

### Step 3 — Batch Host 和可控主循环

- 推荐由 EditorMain 持有 `EditorApp` 并调用公共 `BatchRunner::Run(app, options)`；将完整循环拆成**单帧可驱动**的步骤或给循环加入有限执行控制器。
- `Application::LogicLoop` 已含 `BeginFrame/UpdateResources/UpdateLayers/UpdateScenes/RenderFrame/RenderEditor/PresentFrame/EndFrame`，优先复用，不复制一份新循环；M1 仍可以初始化既有 UI/RenderPipeline 以减小冲击。
- 明确帧边界和 Profiler `_before_update` / `_after_update` 对称：异常、退出中途不应造成永久未完成帧状态；保证 `_dispatcher.PumpTasks()` 仍执行。
- Runner 要具备 `RequestStop(reason)`、截止时间判断和安全 `Finalize`；**外部 watchdog** 仍需防硬死锁和 GPU 驱动卡死。
- 即使创建了 UI 系统，Batch 的进度与结果也不能依赖 `EditorLayer` 的绘制或显示状态。

### Step 4 — Automation 接口复用和权限

- 从 `EditorLayer::OnUpdate` 移出唯一的 `EditorAutomationService::Tick`，在主线程明确的应用阶段**每帧恰好调用一次**，普通 Editor/Batch 都可运行；通过 `Application::IsMainThread()` 或明确的线程契约验证。
- `Submit`/Registry 的现有客户端接口保持有效。为 Runner 提供走**同一权限检查与处理器**的进程内调用路径（可同步调用，也可复用队列+帧调度），避免从 Runner 直接绕过 `Execute` 权限门禁。
- 核查 `AutomationJson::ReadRequest` 对 `_allow_write` / `_allow_destructive` 的处理；在传输边界根据可信会话/执行策略限制或覆盖授权，不能让任意客户端单凭 JSON 字段升级权限。
- Batch 中默认不启 Pipe；如果复用 EditorApp 使 Pipe 默认启动，也必须保证独立 session、不过写正常 Editor session，并禁止外部任意连接提权。用户需要的常驻 `--listen` 留待 follow-up。

### Step 5 — 测试 Registry 与跨帧执行器

- `AutomationTestDesc` 最小字段：`_name`、`_category`、`_required_graphics`、`_timeout_seconds`、`_tags`（必要时）。名字唯一、大小写/通配语义确定，输出排序稳定。
- `AutomationTestContext` 提供：断言（含 expected/actual）、工作目录、报告引用、取消标记、可信调用入口；禁止 test 自行 `exit()`、乱改全局运行模式。
- 适配同步 `Run(context) -> result` 测试；同时支持 `Start/Tick/IsComplete/Finish` 或等价状态机。**不要求每个简单测试都实现跨帧接口**。
- 一个测试只在指定 GPU 能力满足时运行；能力不足应按明确政策 `skipped: unsupported_graphics` 或运行请求配置错误，不允许隐式调用 GPU API。
- 单测试超时设置为失败并给出当前步骤，整体 deadline 触发取消且输出部分结果；待完成作业应在退出前 Drain（设置上限）。
- 首批至少提供 3–5 个真实可复用测试：`Automation.Ping`、`Automation.ListMethods`、`Scene.OpenFixture` / `Asset.QueryFixture`、`Plugin.LifecycleSmoke`（仅可在安全且可控环境执行），另外提供**故意失败**和**故意超时**用例作为 Runner 自测。
- 断言失败必须带测试名、步骤、错误代码、期望值/实际值；失败用例不能污染后续用例状态。避免使用固定 `Sleep(5000)` 作为资源完成条件。

### Step 6 — 报告、退出码与隔离

- 输出 `result.json`、`events.jsonl`、`engine.log`；必要时创建 `artifacts/`。`events.jsonl` 每行一个完整 JSON 对象，包含递增序号、时间、run_id、event 和 test（如果适用）。
- 结果状态：`passed` / `failed` / `skipped` / `error` / `timeout`；Summary 显示各状态数量。JSON 须有 `schema_version=1`、`run_id`、`requested_tests`、`graphics_mode`、`status`、`duration_ms`、`tests[]`、`artifacts`、`error`（失败时）。
- 报告采用 `result.json.tmp` 写入并 flush/close，成功后同目录替换到 `result.json`；原子替换失败需要返回非零码，不能显示测试成功。
- 推荐 Exit Code：`0` 全通过；`1` 断言失败；`2` 参数/工程/初始化错误；`3` 框架内部/报告写入错误；`124` **由外部 watchdog** 认定整体超时。内部软超时要能产出 JSON，进程可返回 `1` 或清晰独立码，但和外部 `124` 必须一致约定。
- 不存在可运行测试时，禁止返回 `0`；`--list-tests` 只要求注册成功、列表可写入（不要求实际执行测试）。
- Batch 运行禁止 `SaveEditorConfig()`、`Application::Finalize` 的默认 EngineConfig 保存；默认跳过 `MigrateLegacyAssetDocuments()` 等可能改写正式资源的初始化迁移。必须保留必要**只读**加载逻辑。
- 写入测试只允许在指定 `work_dir` 的临时项目副本中执行；校验 resolve 后真实路径包含于沙盒根目录，拒绝 `..`、符号链接/junction 越界；插件外部副作用另行显式授权。不要求本阶段覆盖所有任意插件写操作，但必须禁用或隔离不可控测试插件。
- 原有 Editor 仍走正常保存路径；退出时合理卸载 Pipe、Automation、Plugin、Runtime，避免 shutdown 时 Promise 永远 pending。

## Report Contract (schema v1)

以下 JSON 为**设计样例，不是当前实际输出**：

```json
{
  "schema_version": 1,
  "run_id": "20261019-103000-42",
  "requested_tests": "Automation.*",
  "graphics_mode": "hidden-compat",
  "status": "failed",
  "duration_ms": 4216,
  "summary": {"passed": 2, "failed": 1, "skipped": 0, "error": 0, "timeout": 0},
  "tests": [
    {"name": "Automation.Ping", "status": "passed", "duration_ms": 12, "assertions": []},
    {"name": "Automation.DeliberateFailure", "status": "failed", "duration_ms": 8, "error": {"code": "assertion_failed", "message": "unexpected count", "expected": 4, "actual": 3}}
  ],
  "artifacts": [{"type": "engine_log", "path": "engine.log"}]
}
```

`status=passed` 仅当**所请求的测试全部被执行并通过**；若因环境能力跳过，则显式报告 `partial/skipped` 的汇总政策，并在针对整组验收时视为不满足门禁。

## Validation

在完整 Windows 开发环境，使用隔离 fixture 工程，完成以下矩阵并归档每次报告及 Exit Code：

| ID | 操作 | 预期结果 |
| --- | --- | --- |
| M1-V01 | 正常双击/命令行启动 Editor | 界面、工具、场景和 Automation Pipe 与基线一致，无明显崩溃或回归 |
| M1-V02 | `--batch --graphics hidden-compat --list-tests` | 无可见窗口/弹窗；列表可读；`0` 退出 |
| M1-V03 | `--run-tests "Automation.*"` 全通过用例 | `0`、报告全部 pass、无残留进程 |
| M1-V04 | 故意失败断言 | 非零、fail 附 expected/actual；其余测试结果可读 |
| M1-V05 | 未知参数/缺工程/非法 timeout | `2`、记录原因、无弹窗 |
| M1-V06 | 零匹配测试 | 非零、`no_tests_matched`，不能假阳性 |
| M1-V07 | 故意超时（软 timeout） | 终止/取消或安全失败，包含最后步骤；外部 watchdog 能兜底硬卡死 |
| M1-V08 | 运行 3 次 + 两个进程并行（隔离结果目录） | 报告/日志独立，不互相覆盖 session 文件或锁死正常工程 |
| M1-V09 | 前后扫描真实用户工程、编辑器配置、引擎配置 | 非预期写入为 0；隔离目录仅包含已声明工件 |
| M1-V10 | 旧 `--exit-after-frames` / `--plugin-smoke` / `--profile-capture` | 在依赖齐全的配置下保持原语义；不可用项记录环境阻塞原因 |
| M1-V11 | M1 参数误用 `--graphics offscreen/none` | 返回不支持能力错误，不伪装成 `hidden-compat` 成功 |
| M1-V12 | 插件无法加载/资源初始化失败/报告路径不可写 | 明确非零、尽可能完成清理；下次正常 Editor 可重新启动 |

### Acceptance Criteria（全部必须满足）

- [ ] A1：完整项目编译成功；正常 Editor 相关回归通过，未改动原有特殊 CLI 语义。
- [ ] A2：M1-V02/V03 在受支持的 Windows + GPU 会话中稳定通过，用户无需输入、没有可见窗口或弹窗。
- [ ] A3：失败、0 匹配、非法参数、初始化失败、软超时报告和退出码准确（M1-V04–V07、V11、V12）。
- [ ] A4：真实项目/用户配置被保护；跨进程结果文件与 Pipe session 不污染（M1-V08/V09）。
- [ ] A5：一条可复制的 Agent 命令能实现运行测试并读取机器报告，含实际成功/失败示例。
- [ ] A6：`progress.md` 已记录真实构建与测试命令、commit、日志/报告文件路径、遗留限制。

## Decision Log

- M1-D01：新增 `hidden-compat` 是刻意标注过渡语义；只有 M2 才获得正式 `offscreen` 名称。
- M1-D02：测试 Runner 不复刻 Automation Registry 的命令注册、权限判断；Run 和 Test 生命周期独立。
- M1-D03：第一版依靠现有 Editor EXE 及 Runtime；只在遇到无法规避的 UI 初始化耦合时适度拆 Host 阶段。

## Surprises & Discoveries

实施时填写。尤其留意隐藏 HWND 的 Resize/Present、`DEAR_IMGUI` 有无、Plugin 生命周期、Editor 配置文件固定 OneDrive 路径、以及 Windows 后台运行时的焦点事件。

## Known Follow-ups

- 进一步裁掉 Editor UI 的初始化与 Draw 工作（M2）；常驻监听、专用 `AiluCli.exe`、Junit XML 输出与并行 shard 留待后续。

## Outcomes

尚未实施。达成所有 Acceptance 且留存报告后，标为 Accepted 并更新 `progress.md`，方可进入 M2。

## Resume Protocol

恢复时先读 `README.md` / `progress.md` / 本文件，核对当前代码差异与最近测试证据。只继续当前未通过项，不因“代码已写完”就跳过失败场景验收。
