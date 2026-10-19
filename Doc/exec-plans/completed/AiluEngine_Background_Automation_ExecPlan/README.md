# AiluEngine 后台运行与自动化验证 — ExecPlan

> 适用基线：用户提供的 `AiluEngine_Source.zip`（2026-10-19）；只包含 Engine / Editor 源码快照，**未包含完整工程生成、构建、测试或样例项目文件**。所有设计均为**待实现方案**，不能把示例 CLI 当作现有功能。
>
> 执行方式：交给 Codex / Claude 在完整 AiluEngine 仓库内按里程碑实施。**当前里程碑未通过验收，不进入下一阶段。**

## Purpose

让 AiluEngine 能由本地命令行、CI 或 Codex / Claude **无人值守启动、执行验证、产生结构化报告、可靠退出**；在后续阶段支持真正的 GPU 离屏渲染及无 GPU 的数据/算法测试。借鉴 Unity Batch Mode 的 CLI 使用习惯和 UE Commandlet / Automation 的职责分离，但复用现有 Editor Automation 基础设施。

## Scope

- Batch 一次性运行入口、退出码、测试发现与运行、异步/跨帧测试驱动、超时、JSON 报告及证据输出。
- 运行能力分级：`windowed`、`hidden-compat`、`offscreen`、`none`；四种名称是**稳定、互不混淆的语义**。
- 基于现有 `EditorAutomationService` / `EditorAutomationRegistry` / Named Pipe / Scene / Asset / Capture 服务，不重复发明另一套 RPC。
- 正常 Editor 启动完全兼容；Batch 默认只读，写入测试通过工作副本与授权策略隔离。
- 建立能被 Agent 解析、复测、诊断的通用结果契约。

## Non-goals

- 本次不实现通用 MCP Server、分布式调度平台、远程渲染农场或可视化 CI 控制台。
- 不在 M1 引入全新 RHI、重写 Render Graph、ResourceMgr 或窗口系统；不承诺 Windows Session 0 / 无登录桌面环境一定支持 D3D12。
- 不在 M1 完成真正无 SwapChain 的 Offscreen；也不把隐藏窗口等同于无窗口。
- 不默认迁移原有 `--profile-capture`、`--plugin-smoke`、`--exit-after-frames` 行为；只预留后续向 Runner 合并的兼容路径。

## Architecture Constraints

1. **能力与生命周期正交**：`--batch` 表示无人值守执行并退出；`--graphics` 决定渲染能力。`hidden-compat` 是隐藏 HWND、保留现有 D3D12 + SwapChain/Present 的过渡能力；`offscreen` 是无窗口依赖的 GPU 绘制、无 SwapChain Present；`none` 是不创建设备的 CPU 路径。
2. **唯一执行核心**：一套 EngineRuntime/Editor Automation 逻辑，一套 Test Runner；测试及 RPC 都由已授权的主线程上下文执行。不要将“自动化 Tick”绑死在 `EditorLayer`。
3. **阶段性支持**：M1 仅实现 `windowed` 与 `hidden-compat`；请求尚未实现的 `offscreen` / `none` 应以参数或能力错误退出，绝不可悄悄回退到有窗口模式。
4. **默认无修改**：Batch 默认拒绝 SafeWrite / DestructiveWrite / ExternalEffect；只有显式使用测试工作副本并经 Runner 授权的写操作可放行，真实工程不能作为默认写入目标。
5. **无 UI 阻塞**：Batch 禁止 MessageBox/文件对话框/需要人工按键的提示；初始化异常必须返回错误、日志及有条件的 JSON 结果。
6. **可取消并受限**：整体与每个测试均有 deadline；异常时尽最大努力顺序关闭（测试→服务→插件→Runtime），若卡死由**外部 watchdog** 终止。避免在渲染线程同步等待自身。
7. **报告是协议**：版本化 `result.json` + 事件流 `events.jsonl` + 进程退出码 + 独立日志目录；路径是规范化绝对路径；报告落盘使用临时文件+同目录原子替换。
8. **约束编码风格**：变量 `lower_snake_case`；类/结构体成员变量 `_lower_snake_case`；静态变量 `s...`；常量 `kCamelCase`；类/结构体和函数 `CamelCase`；C++ 每行尽量不超过 160 列；避免过度抽象。
9. **兼容构建差异**：当前快照未包含构建系统。在完整仓库先查清项目构建方式，不猜测 `.sln`、CMake target 名；验证命令应记录实际执行的完整命令与环境。

## Baseline

已在快照中核对：

| 当前文件 / 符号 | 现状与影响 |
| --- | --- |
| `Editor/Src/EditorMain.cpp` | `WinMain` 依靠位置参数指定项目；有 `--profile-capture`、`--plugin-smoke`、`--exit-after-frames`；缺项目时 `MessageBoxW`；`app.Tick(16.6f)` 会进入循环。 |
| `Engine/Inc/Framework/Common/Application.h` | `ApplicationInitContext` 已持有参数和项目路径；有 `BeginFrame / UpdateResources / UpdateScenes / RenderFrame / PresentFrame / EndFrame` 分阶段接口。 |
| `Engine/Src/Framework/Common/Application.cpp` | 初始化强制创建 Window；`LogicLoop` 无条件走 UI、Scene、Render、Present；`Finalize` 写回 `g_engine_config`。 |
| `Engine/Src/Framework/Common/EngineRuntime.cpp` | `Initialize(context, Window&, desc)` 强制 GPU 全局上下文、窗口注册、Backbuffer 和 SwapChain；之后初始化资源、脚本、Audio、Gizmo、UI、Scene。 |
| `Engine/Src/Platform/WinWindow.cpp` | 初始化末尾无条件 `ShowWindow(_hwnd, SW_SHOW)` + `UpdateWindow`。 |
| `Editor/Src/EditorApp.cpp` | 初始化 Editor UI、加载自动插件、打开场景、执行 `ResourceMgr::MigrateLegacyAssetDocuments()`；创建 AutomationService/Named Pipe 并保存会话；Finalize 保存 Editor 配置。 |
| `Editor/Src/Widgets/EditorLayer.cpp` | `EditorLayer::OnUpdate` 调用 `GetAutomationService()->Tick()`，跳过 EditorLayer 会让管道请求无法处理。 |
| `Editor/Src/Automation/AutomationService.cpp` | 现有 `Submit`→队列→`Tick/Execute`，已有权限门禁；`Execute` 私有。 |
| `Editor/Src/Automation/AutomationPipeServer.cpp` | 基于 Named Pipe、future、固定请求超时；RPC payload 中的权限上下文不能被无条件信任。 |
| `Editor/Src/Automation/AutomationCaptureService.cpp` | 已提供 `render.capture_pass` / `render.capture_status`，可复用现有截图能力。 |
| `Engine/Inc/Render/GraphicsContext.h` | `Present`、`RegisterWindow`、`WaitForGpu`、`ResizeSwapChain` 等 API 为 Offscreen 分离的核心检查点。 |
| `Engine/Src/Framework/Common/ResourceMgr.cpp` | `Initialize` 创建默认 `Texture2D` 等 GPU 资源，所以 CPU-only **不能**仅仅绕过 GraphicsContext 初始化。 |

## Target Outcome

支持以下**规划中的 CLI**（不是基线已实现的参数）：

```powershell
# M1：隐藏窗口兼容 Batch（有 GPU + 内部 SwapChain）
AiluEditor.exe "D:\Projects\Demo.ailuproject" --batch --graphics hidden-compat --run-tests "Core.*" --report "D:\Reports\core\result.json" --timeout-seconds 180

# M2：无 Present 的 GPU Offscreen
AiluEditor.exe "D:\Projects\Demo.ailuproject" --batch --graphics offscreen --scene "Assets/Scenes/RenderTest.alasset" --run-tests "Rendering.*" --report "D:\Reports\render\result.json"

# M3：真正 CPU-only
AiluEditor.exe "D:\Projects\Demo.ailuproject" --batch --graphics none --run-tests "Serialization.*" --report "D:\Reports\cpu\result.json"
```

测试输出应明确记录通过/失败/跳过、总耗时、环境能力、错误信息及附属工件位置；无人值守运行时无非预期弹窗、无残留进程、退出码稳定；普通 Editor 启动行为保持一致。

### CLI 契约（按阶段激活）

| 选项 | 语义 | 阶段 |
| --- | --- | --- |
| `<project>` | 与现有位置项目参数兼容；路径必须存在 | M1 |
| `--batch` | 执行命令/测试后自动退出，默认非交互 | M1 |
| `--graphics windowed\|hidden-compat\|offscreen\|none` | 互斥的图形能力；Batch 默认 `hidden-compat`，普通 Editor 默认 `windowed` | M1/M2/M3 |
| `--run-tests <filter>` | 通配符筛选注册测试，**零匹配视为配置失败** | M1 |
| `--list-tests` | 枚举测试与需要的能力；不执行测试 | M1 |
| `--report <path>` | 结果 JSON 文件位置；未指定时 Batch 在隔离输出根目录中分配唯一目录 | M1 |
| `--timeout-seconds <n>` | 整体运行截止时间；测试另设独立期限 | M1 |
| `--fixed-delta <seconds>` | 有明确需要的模拟/回归测试固定步长；不要用这个参数制造 GPU 空转 | M2 |
| `--scene <asset_path>` | 选定测试场景；仅在对应测试要求时加载 | M2 |
| `--render-size <width>x<height>` | 为 Batch offscreen 输出指定独立 RT 尺寸；尺寸必须为 1..65535 | M2 |
| `--listen` | 常驻 RPC 后台服务；**后续增强，不作为本次验收项** | Follow-up |

批量测试可由 Agent 调用一个进程完成，也可以按测试组启动隔离进程；不把自动化外部进程脚本实现硬编码到引擎层。

## Relevant Files

- 核心启动：`Editor/Src/EditorMain.cpp`、`Editor/Src/EditorApp.cpp`、`Editor/Inc/EditorApp.h`。
- 主循环：`Engine/Inc/Framework/Common/Application.h`、`Engine/Src/Framework/Common/Application.cpp`。
- 引擎生命周期：`Engine/Inc/Framework/Common/EngineRuntime.h`、`Engine/Src/Framework/Common/EngineRuntime.cpp`。
- 窗口与 GPU：`Engine/Src/Platform/WinWindow.cpp`、`Engine/Inc/Framework/Common/Window.h`、`Engine/Inc/Render/GraphicsContext.h`、`Engine/Inc/Render/RenderPipeline.h`。
- Automation：`Editor/Inc/Automation/AutomationTypes.h`、`AutomationRegistry.h`、`AutomationService.h`，对应 `Editor/Src/Automation/*.cpp`。
- 捕获：`Editor/Src/Automation/AutomationCaptureService.cpp`、`Engine/Inc/Render/ImageCapture/ImageCaptureService.h`。
- 资源：`Engine/Src/Framework/Common/ResourceMgr.cpp`、`Engine/Inc/Framework/Common/ResourceMgr.h`。

## Milestones

1. **[M1 — Batch Runner + 隐藏窗口兼容模式](m1-batch-runner.md)**：最高优先级；形成完整无人值守闭环，不承诺真 Offscreen。
2. **[M2 — GPU Offscreen + 渲染验收](m2-gpu-offscreen.md)**：去除 SwapChain/Present 依赖、GPU 截图与渲染回归。
3. **[M3 — CPU-only 无 GPU 模式](m3-cpu-only.md)**：可选初始化资源/脚本/场景，验证无 GPU 路径。

## Current Milestone

当前状态：**Closed by user decision**。计划已完成当前阶段交付并转为完成归档；未完成验收项保留为后续使用中的排查项，不在本计划内继续推进。

## Progress

详见 [`progress.md`](progress.md)。每次实现/测试后更新状态，不要让日志淹没在对话上下文中。

## Decision Log

- D001：使用现有 Editor EXE 做第一版 CLI，暂不新建 `AiluCli.exe`，以避免复制 Host 初始化代码；未来证实拆入口可显著减小依赖时再考虑。
- D002：M1 过渡模式明确命名 `hidden-compat`，避免将隐藏 HWND 冒充 `offscreen`。
- D003：一次性 Batch 优先，常驻 `--listen` 延后；原因是一次性任务更容易保证隔离、可恢复和超时。
- D004：测试 Runner 与 `EditorAutomationRegistry` 分离，但可重用方法注册与调用；测试生命周期不等于单次 RPC。
- D005：权限从**可信的进程模式/隔离工作区**确定，绝不完全采信 Pipe payload 里的 `allow_write` / `allow_destructive`。

## Surprises & Discoveries

- `EditorApp` 的资源迁移、插件自动加载、配置保存存在隐式副作用，需要在 M1 做写入路径审计和隔离。
- `Application::Tick()` 是进入持续循环，不是单帧 Tick；Runner 要么提供 `StepFrame`，要么由循环安全驱动，不能简单调用一次当作执行一帧。
- 现有 `ImageCaptureService` 有状态轮询能力，可复用，不必自己写 GPU 回读通道。
- 快照没有完整构建入口；首次执行应先补充真实 build 命令及一个可复制的验证工程路径到 `progress.md`。

## Validation

每个 Milestone 都需要：编译通过、规定的正向用例通过、规定的失败/超时/重复运行用例通过、与正常 Editor 的差异回归通过，且给出命令、运行目录、退出码、报告链接或文件路径。**缺少运行环境不能写“已通过”。**

## Known Follow-ups

- 独立 CLI EXE、常驻 `--listen`、MCP 工具接入、测试分片与多机执行、覆盖率、自动 golden 图批准、CI 集成、性能指标基线、Windows Service / RDP 环境的 GPU 可用性适配。
- 原有特殊 CLI（plugin smoke、profile capture）可**逐项**迁入统一 Runner；不要求第一版重构完毕。

## Outcomes

本计划已按用户决定关闭并归档。M1/M2/M3 的已实现能力、当前主机证据和未完成验收项均保留在 `progress.md`；后续仅在实际使用中遇到对应问题时单独排查。

## Resume Protocol

1. 首先阅读本 `README.md` 和 `progress.md`，**只打开当前里程碑**的 `mX-*.md`，除非确需核对跨里程碑接口。
2. 在完整仓库核对当前代码、构建命令、测试工程和尚未提交变更，尤其是与 2026-10-19 快照不同的部分；先更新 `progress.md` 的 Baseline delta。
3. 严格遵循 `Current Milestone → Implementation → Validation → Acceptance Criteria`；每完成一个子步骤及时记录决策、风险、测试证据与下一动作。
4. 遇到阻塞或关键设计分歧，停在当前里程碑，记录两三个真实备选方案及权衡；**不擅自推进下一个里程碑**。
5. 本计划已由用户明确决定关闭；不要将剩余未验收项改写为已通过。后续问题应创建新的专项计划或 issue。
