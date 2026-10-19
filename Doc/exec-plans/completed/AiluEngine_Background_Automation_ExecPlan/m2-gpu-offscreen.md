# M2 — 真正的 GPU Offscreen 运行与渲染验证

**状态**：Blocked by M1。**前置门槛**：M1 全部 Acceptance Criteria Passed/Accepted 且证据已记录。**本阶段独立交付**：不创建窗口/SwapChain，不调用 Present，也能创建 D3D12 设备、渲染到独立 RT、推进帧、读回截图、验证 Render Graph 和清理 GPU 资源。

## Purpose

使 GPU 自动化摆脱 HWND/SwapChain/Present，能够在**有可用 D3D12 设备的受支持 Windows 会话中**稳定执行真实场景渲染与自动截图，为 Blender Link、材质、Render Graph、体积云及后处理建立复现性验收入口。

## Scope

- `--batch --graphics offscreen` 的真离屏初始化与按需子系统裁剪。
- 渲染目标、主摄像机、帧同步及 Ring Buffer 的无 SwapChain 驱动；禁止依赖隐藏 SwapChain 的 Present。
- 复用 Render Pipeline、RenderTexture、ImageCaptureService、`render.capture_pass`、`render.capture_status` 与 FrameDebugger/RenderGraph 调试接口（如可用）。
- 支持确定性帧数、GPU 完成等待、图片/结构化统计与容差比较。
- 报告可区分 GPU 初始化、Shader 编译、资源加载、GPU 同步、捕获写盘、图像内容、设备丢失等错误来源。

## Non-goals

- 不改造渲染算法、不追求不同 GPU/驱动像素 bit-exact；不新建独立 Render Graph。
- 不承诺无硬件 GPU、无完整图形驱动、Session 0/服务或远程桌面任何组合必然可用。
- 不在这一阶段移除 ResourceMgr 的 GPU 依赖或实现 `graphics=none`。
- 不强制实现自动更新 Golden 图、视觉 AI 识别或多 GPU 分布式对比。

## Architecture Constraints

1. `offscreen` 与 `hidden-compat` 是两条**可测试、语义明确**的后端运行策略，不能把 Present 置空后仍偷偷维持 SwapChain。
2. 设备创建与窗口绑定要分离：`GraphicsContext::InitGlobalContext()` 可独立于 `RegisterWindow()`；窗口存在时才执行 Register/Unregister/ResizeSwapChain。
3. 渲染主输出指向独立 `RenderTexture`，不得无条件使用 `RenderTexture::s_backbuffer` 作为测试输出；Renderer、Camera、UI 的访问都要检查真实生命周期。
4. **绝不只删除 `Present()` 调用**：DX12 帧收尾、命令队列提交、Fence、FrameResourceManager、资源回收、ImageCaptureService Tick、调试消息 Drain 目前存在依赖。必须提供非 Present 的完整 Frame Boundary。
5. Offscreen 默认不构建 EditorLayer/DockManager/ImGui/UIRenderer 的可视部分；确需 Editor 服务时按依赖单独初始化，不在 GPU backend 特判 Editor 业务。
6. Frame test 需固定渲染尺寸、相机矩阵、时间步长、随机种子、资源清单等可控条件；错误与缺资源要单独报告，而不是只给一张空白截图。
7. 禁止以长时间 Sleep 替代 Shader/Asset/GPU/Readback 完成信号；等待循环有 deadline、进度和取消。

## Baseline

经源码确认：

- `EngineRuntime.cpp::Initialize(context, Window&, desc)` 先 `InitGlobalContext()`、`RegisterWindow`、`RenderTexture::WindowBackBuffer`、`ResizeSwapChain`。
- `Application.cpp::LogicLoop` 当前无条件 `RenderFrame / RenderEditor / PresentFrame`，后者直接调用 `g_pGfxContext->Present()`。
- `Engine/Src/RHI/DX12/D3DContext.cpp::Present()` 不只调用交换链操作：它触发 `CommandPresent` 的执行，并 `DrainD3DDebugLayerMessages()`。
- `D3DContext.cpp::PresentImpl()` 负责 SwapChain Present、`MoveToNextFrame` 等流程；`GpuCommandWorker` 还在自己的帧循环中处理 `ImageCaptureService::Tick()`、GPU Registry Sync、帧调试开始/结束。
- `D3DContext::CurBackbufIndex()` 等 API 与窗口 SwapChain 当前索引耦合，要清点由谁使用。
- `EditorLayer.cpp` 的主 Widget 默认将输出绑定到 `RenderTexture::s_backbuffer`；`DockManager`/`UIRenderer` 也含窗口 BackBuffer 访问，**不能简单带着完整 EditorLayer 启动真正 Offscreen**。
- `RenderPipeline` 存在 camera/target 管理，`ImageCaptureService` 和 `RenderGraphImageCaptureBridge` 已具备通用 capture 设施。

## Target Outcome

```powershell
# 预期命令：无 HWND / SwapChain / Present 的 D3D12 场景测试
AiluEditor.exe "D:\Projects\Fixture.ailuproject" --batch --graphics offscreen `
  --scene "Assets/Scenes/RenderTest.alasset" --run-tests "Rendering.*" `
  --fixed-delta 0.0166666667 --report "D:\Reports\render\result.json" --timeout-seconds 180
```

产物示例：`captures/color.png`、`captures/depth.png`（当测试需要时）、`artifacts/render_stats.json`、`artifacts/diff.png`、统一 `result.json`，以及 GPU debug layer 的错误与警告摘要。

## Relevant Files

- 生命周期与主循环：`Engine/Inc/Framework/Common/EngineRuntime.h`、`Engine/Src/Framework/Common/EngineRuntime.cpp`、`Engine/Src/Framework/Common/Application.cpp`。
- 渲染契约：`Engine/Inc/Render/GraphicsContext.h`、`Engine/Inc/Render/RenderPipeline.h`、`Engine/Src/Render/RenderPipeline.cpp`、`Engine/Inc/Render/Texture.h`（`RenderTexture`）。
- DX12 实现：`Engine/Src/RHI/DX12/D3DContext.cpp`、`Engine/Src/RHI/DX12/D3DSwapchain.cpp`、`Engine/Src/RHI/DX12/D3DCommandBuffer.cpp`；以及 `FrameResourceManager` 的实际源码位置。
- 捕获：`Engine/Inc/Render/ImageCapture/ImageCaptureService.h`、`Engine/Src/Render/ImageCapture/ImageCaptureService.cpp`、`Engine/Src/Render/ImageCapture/Adapters/RenderGraphImageCaptureBridge.cpp`、`Editor/Src/Automation/AutomationCaptureService.cpp`。
- UI 耦合：`Editor/Src/EditorApp.cpp`、`Editor/Src/Widgets/EditorLayer.cpp`、`Editor/Src/Dock/DockManager.cpp`、`Engine/Src/UI/UIRenderer.cpp`。

## Implementation

### Step 0 — 映射 Present 对帧推进的所有依赖

- 在完整仓库追踪 `Application::PresentFrame` → `D3DContext::Present` → `CommandPresent` → `GpuCommandWorker::{RunSync,RunAsync,EndFrame}` → `D3DContext::PresentImpl`。
- 对每个动作分类：`submit/flush`、`FrameResourceManager ring`、GPU fence/safe reuse、resource deferred free、debug layer drain、capture progress、ImGui render、SwapChain Present、DXGI latency。
- 查找全部 `RenderTexture::s_backbuffer`、`WindowBackBuffer()`、`CurBackbufIndex()`、`GetFrameCount()`、`_render_windows[0]` 和基于 SwapChain 数量的缓冲访问；列一个有限的依赖列表及处理策略并更新 `progress.md`。
- **验证门槛**：能说明每帧谁发出 GPU 命令、谁增加 frame index、谁回收 resources、谁等待 GPU，而不仅是禁用 Present。

### Step 1 — 引入明确的 GPU Frame Boundary

- 给 `GraphicsContext` 增加最小必要的 Frame 生命周期 API（命名按真实代码调整，例如 `EndFrame()`），将**与窗口无关的**提交、Fence signal、ring slot 推进、待释放资源回收、ImageCapture Tick、debug drain 独立出来。
- Windowed 路径继续执行 SwapChain Present；Offscreen 路径执行无窗口的 EndFrame/Submit 操作；避免同帧重复 EndFrame、重复 increment 或重复 Fence。
- 如果现有 FrameResourceManager 依赖 `_render_windows[0]` / SwapChain backbuffer index，应将 GPU frame slot 改为逻辑 frame-index modulo frame-in-flight-count，仍通过 Fence 防止覆盖 GPU 在用资源。
- 处理 `GpuCommandWorker` 在同步/异步渲染两种模式下的提交与完成语义；先以已验证的同步渲染作为 Offscreen 标准路径，再单独增补异步支持。
- 设备丢失、D3D12 debug layer 严重错误、resource state 校验失败应转换成可观察的 Runner 失败，不能因少了 Present 而跳过 debug drain。

### Step 2 — 无窗口设备初始化和独立 RenderTexture

- 扩展 `EngineRuntime::Initialize` 的运行能力输入，使 `offscreen` 分支能在无 `Window&` 前提下初始化 `GraphicsContext`、ResourceMgr、JobSystem、SceneMgr、ScriptSystem 和所需的 RenderPipeline。
- `RegisterWindow`、`WindowBackBuffer` 和 `ResizeSwapChain` 仅 Windowed/HiddenCompat 调用；在 Offscreen 禁止它们出现。`_window` 允许 nullptr，Shutdown 依赖 `_window` 的操作有条件执行。
- 独立创建指定宽高/格式的 RenderTexture；将测试 Camera TargetTexture 设置为这个 RT（或由可复用的 pipeline output 对象管理），并显式验证 resize / release 生命周期。
- EditorApp 的功能初始化拆成 `CommonEditorServices` / `InteractiveEditorUI` 等**必要且最小**边界：AutomationRegistry、Plugin/Asset/Scene 功能尽量可用，但跳过 DockManager、EditorLayer、ImGui UI、窗口 Input。此边界设计不能使正常 Editor 双份初始化。
- 若某些 Editor 自动化方法要求当前可视 Widget，给方法声明运行能力并返回 `unsupported_capability`；不允许空指针崩溃或假装成功。

### Step 3 — 离屏确定性 Scene Runner

- `--scene` 通过现有资源/场景 API 载入，等待 Asset/Shader 队列真正 ready；记录未加载/失败资源的 Asset GUID/path。
- 支持指定固定分辨率/Camera/step count/`--fixed-delta`，给测试 context 注入稳定时钟和随机种子（只对需确定性的测试），默认关闭非必要编辑器 UI 动画。
- 渲染多帧前检查 `SceneMgr::ActiveScene`、当前 Camera、RT format、所需 features 的开关与初始化状态。资源未 ready 时不计入对比帧数。
- 提供 `RenderFrames(n)` 或等价 API 由 Runner 驱动（必须跨帧异步完成），而非阻塞 GPU 命令线程等待自身。
- 渲染结束调用正确的 GPU Fence 等待，等待 CaptureService 写盘完成，再生成结果。对阻塞设置上界，记录失败阶段。

### Step 4 — 捕获与可重复图片比较

- 优先接入 `ImageCaptureService` / RenderGraph Bridge 已有抓取管道，复用 `render.capture_pass` / `render.capture_status`；确需截取最终 RT 时增加最小的 final-color adapter。
- 保存原始测试画面、比较画面、diff 图、统计（输出尺寸、格式、有效像素、NaN/Inf、均值/方差）及阈值设置。
- 基线图必须有版本与环境标签（场景/Shader/引擎 revision、GPU vendor/driver、色彩空间、曝光设置、分辨率）；动态 TAA / GI / 云噪声先固定 jitter/seed 或标记为非确定性测试。
- 比较指标至少 `MAE`、`RMSE`、最大误差和**差异像素比例**，并可针对 alpha、深度或掩码区域单独设置阈值。
- 图片比较前先校验 **维度、颜色空间、像素格式、线性/伽马**，不能把 tonemapped PNG 直接和 linear HDR 做裸比较。对于不同 GPU，允许明确定义的容差，不能无依据地设像素 100% 相等。
- Baseline 缺失时报告 `baseline_missing`，不要自动生成并通过；批准新的基线属于明确的人工/流程外操作。

### Step 5 — 性能与生命周期回归

- 证实 Offscreen 不创建 HWND、不申请 DXGI SwapChain、`Present` 调用计数为 0；可以通过可开关的调试统计/埋点佐证。
- 在固定分辨率连续渲染 100+ 帧，观察 DX12 Debug Layer、GPU resource live object、FrameResource 轮转和显存趋势（允许缓存稳定平台期，不允许单调无限增长）。
- 验证场景多次 Open/Unload、RT resize/recreation、截图 Session Cancel、Shader 编译失败和 GPU 设备不可用时能干净报告并退出。
- 比较 M1 `hidden-compat` 与 M2 `offscreen` 的同一 Camera/Scene 结果：差异在预期容差内、无颠倒或 gamma 问题。把这组对比留为长期回归测试。

## Validation

| ID | 操作 | 预期结果 |
| --- | --- | --- |
| M2-V01 | `--graphics offscreen` 空场景初始化和退出 | HWND/SwapChain 创建次数 0，Present 调用次数 0，无可见 UI |
| M2-V02 | 固定三角形/简单场景渲染 1、8、128 帧 | 输出尺寸/内容正确，Frame Slot 循环安全，Fence 完成 |
| M2-V03 | 图形资源上传→RenderGraph 读取→Readback | 无 Resource State / Barrier 错误；结果不为空且 GPU 完成 |
| M2-V04 | 有意读取尚未完成的 Capture 状态 | Runner 继续推进并有界等待；完成后才宣布通过 |
| M2-V05 | Golden 正确/不正确/缺失/分辨率不符 | 分别 Pass/Fail/Fail/Fail，产生 diff + 原因 |
| M2-V06 | Shader 编译错误 / Asset 缺失 / 设备初始化失败 | 可诊断 JSON，非零退出，不以黑屏为成功 |
| M2-V07 | 100+ 帧 + 多场景/多尺寸 RT | 无资源持续泄漏、Fence 死锁或无界内存增长 |
| M2-V08 | M1 hidden-compat 与 M2 offscreen 同场景比较 | 在记录的容差内一致，允许驱动固有噪声 |
| M2-V09 | 普通 Editor + M1 完整回归 | 无视觉、Input、SwapChain、Plugin 或 CLI 行为回归 |
| M2-V10 | 不支持的无桌面/GPU 会话 | 明确报 `graphics_unavailable`/可诊断初始化错误，不假通过 |

### Acceptance Criteria（全部必须满足）

- [ ] A1：真 Offscreen 的创建过程与 100+ 帧路径经埋点/调试证明无 HWND、无 SwapChain、无 Present。
- [ ] A2：GPU frame slot、command submit、Fence、capture progress、debug drain 在无 Present 路径正确，M2-V02/V03/V07 无关键错误。
- [ ] A3：截图及图像比较可复现；容差、颜色空间与失败工件符合约定，M2-V04/V05/V06 通过。
- [ ] A4：用户原工程及正常 Editor 不受影响，M2-V08/V09 与 M1 回归完整通过。
- [ ] A5：设备不支持时准确失败；写入报告和释放流程可信。
- [ ] A6：`progress.md` 记录 DX12 帧边界决策、实际测试命令、GPU/驱动版本、日志和截图路径。

## Decision Log

- M2-D01：图形设备不等于窗口/SwapChain；真正 Offscreen 必须独立 GPU Frame Boundary。
- M2-D02：复用 `ImageCaptureService`，不另建 GPU 回读工具链。
- M2-D03：先同步、后异步渲染；优先确定性和资源生命周期正确性。

## Surprises & Discoveries

已发现：`D3DContext::PresentImpl` 中还有 `MoveToNextFrame`；`D3DContext::Present` 触发 debug layer drain；CaptureService 的 `Tick` 位于 GPU worker 帧路径内；Editor UI 多处引用 window BackBuffer。这是 M2 不能仅删除 `PresentFrame()` 的直接原因。其余发现实施时填写。

## Known Follow-ups

- 高级图片 perceptual metric / HDR 差异、GUI 截图、跨 GPU 基线管理、长期压力与性能分数回归、异步多 GPU 调度可后续引入。

## Outcomes

尚未实施。若无法安全移除 SwapChain 帧依赖，继续停留在 M2，不以 `hidden-compat` 截图冒充 M2 通过。

## Resume Protocol

读 `README.md` → `progress.md` → 本文件；核对 M1 已 Accepted，优先检查最新 GPU frame boundary 变更和失败证据。当前阶段全验收完成后才能转入 M3。
