# M0 · SDK Bootstrap / Runtime / 必要 Hook

## Purpose

以可选的、可回滚的方式接入 Streamline SDK 生命周期及能力查询；在**不执行 DLSS Pass**的情况下验证 DX12 Manual Hooking 是否完成。M0 结束时任何 SL 可用性问题都不会影响原始 TAA/Present。

## Scope

### 1. 核对工程基线与 SDK

1. 记录源码实际 commit、SDK `v2.14.1`、构建系统、MSVC、显卡、OS、驱动。若项目仅为快照，先找到实际工程解决方案，不虚构 build command。
2. SDK `include/`、`sl.interposer.lib`、`sl.interposer.dll`、`sl.common`、`sl.dlss` 与需要的依赖来自**同一 Release**；若发行包以不同目录组织，按实际内容调整拷贝，不按猜测文件名引用。
3. 选定一个清晰的可选构建开关：`AILU_WITH_STREAMLINE`。当开关为 0，所有 SL headers/libs/DLLs 不参与依赖；编译源文件条件化或使用 stub；绝不需要 RTX 才能编译基本引擎。
4. 在 SDK 依赖方式上显式选择且记录：若要求缺失 `sl.interposer.dll` 时进程仍可进入 Main 并 fallback，则用**显式动态加载**（自行按 SDK 解析 `slGetFeatureFunction` 等符号）或 **`/DELAYLOAD` + 已验证的失败处理机制**。普通直接链接 `sl.interposer.lib` 会产生启动时 DLL 依赖，缺 DLL 进程可能根本无法启动，不能宣称支持运行时 fallback。Debug 开关下将 SL 日志转接到引擎日志；若 SDK 不可加载、签名不通过、插件缺失、Feature 不支持，输出**有原因的 unavailable 状态**。

### 2. 轻量 Runtime（无 Feature 虚接口）

建议 `StreamlineRuntime`：

- `Init(const StreamlineInitDesc&)`：固定、经审核的插件目录；默认只请求 `sl::kFeatureDLSS`；初始化 SDK；记录版本与错误；不得单纯以 `slInit()` 返回成功代表 SR 受支持。
- `AttachD3D12Device(native_device)`：`slSetD3DDevice()`，对正确的 DX12 adapter 查询 `slIsFeatureSupported()`、`slGetFeatureRequirements()`，按可用性报告。
- `GetFeatureSupport(feature)`：返回“SDK 已加载 / 插件已加载 / 硬件支持 / 已启用”的分层状态，供 UI 判断。
- `AcquireFrameToken(frame_id)`、`GetOrCreateViewport(view_identity)`：定义接口但 M0 只需要能安全创建/销毁框架，不提前搭建完整内部容器；具体 FrameToken 数据管理在 M1。
- `Shutdown()`：保证 GPU 已完成使用 SL 的帧后，再释放 SL Feature/Runtime；无效或重复 Shutdown 不崩溃。运行时状态由调用者拥有，不做全局隐式静态初始化。
- 后续 Feature 使用一个最小 `feature` 列表即可扩展，初始化期决定 `featuresToLoad`；不能假设启用新 Feature 后可无条件热加载所有插件。

### 3. DX12 Manual Hooking 的最小必需点

阅读官方 `ProgrammingGuideManualHooking.md` 对应版本后实施：

1. `sl::Preferences::flags` 设置 `eUseManualHooking`；可同时使用 `eUseFrameBasedResourceTagging`，为后续多帧 tag 做准备。
2. 优先参照官方推荐的 `sl.interposer.lib` 接线方式，但结合上面的运行时 fallback 需求决定**延迟加载还是纯动态解析**；保留原生 DX12/DXGI 接口为主，按文档 `slGetNativeInterface` / `slUpgradeInterface` 获取代理。纯动态解析时 `slDLSSSetOptions` 等 SDK helper 不能无条件静态调用，M2 必须通过实际已解析函数入口访问。
3. 统一拦截**实际使用到的必需 API**：DXGI factory 创建 SwapChain，SwapChain `Present`、`Present1`、`ResizeBuffers`、`ResizeBuffers1`、`GetBuffer`、`GetCurrentBackBufferIndex`、`SetFullscreenState` 和 D3D12 Device `CreateCommandQueue`。不能只改 `D3DContext::PresentImpl` 而忽略其它必需 Hook。
4. 在原有 DX12 `Device / Factory / SwapChain` 的创建封装处集中接线，仅需 proxy 的函数走 proxy，其它操作继续 native；ImGui、PIX、普通 RHI 仍接收 native 句柄。优先完成一条主 swapchain；其它实际使用的窗口和 resize 路径必须被枚举、标注，否则关掉 SL 以免状态不完整。
5. 确认 `presentCommon()` 被**每帧持续调用**：使用日志/断点；Resize 前后依然执行。不能因为 SR 不是 FG 就省略这一验证。只有确认 hooking 全部正确，才开放 Runtime 的“Enabled”状态。
6. 签名验证、安全 DLL 查找路径、Release 日志级别和插件失败的清理都不能遗漏。M0 不需要实现 FG 参数或 `slEvaluateFeature`。

### 4. Capability、失效路径、最小调试入口

- 在 DX12 初始化完成后输出功能结果，例如 `Streamline: SDK=loaded, DLSS_SR=unsupported(reason)`；如果设备非 RTX，预期状态是 unavailable，继续原始渲染。
- 关闭 SL 时，不挂任何 swapchain proxy 或走明确定义的无 SL native 路径。对于已创建的 proxy，不要在运行中未经安全 teardown 就切换 native/proxy。
- 支持 `StreamlineRuntime::GetStatus()` 用于 Editor 调试展示，但 M0 不要求完整 UI。
- 记录 DLL 文件版本、Steamline SDK 版本和与 OS/显卡有关的 `sl::Result` 值。

## Non-goals

- 不创建 DLSS Output、RenderGraph Pass、CommandCustom API，不修改 Camera Jitter、TAA 或 Postprocess，不处理 SR 调参。
- 不把 FG 的所有 Frame/Pacing/Buffer 标记提前实现；只落实 Manual Hooking 自身的必要性。

## Architecture Constraints

- `sl_*` 不出现在 `Camera.h`、`Renderer.h`、`RenderingData.h` 的公开字段中。
- `slInit` / `slShutdown`、设备绑定与 capability 查询受 Runtime 状态机控制；错误时绝不半初始化继续访问 Feature API。
- 后端只针对 DX12；无 SDK/其它 backend 编译无需 include NVIDIA 头。
- 遵循 README 代码风格、约束、相关文档版本。

## Baseline

- `D3DContext.cpp` 已有 `PresentImpl()`、多个 `_render_windows`、Queue/设备生命周期；具体 Factory/SwapChain 包装文件从实际工作树检索（不能假设文件名）。
- Editor 默认渲染已存在；M0 不允许改变最终画面。

## Target Outcome

SL on/off 两种配置都可编译与启动；已签名 SDK 可完成初始化、能力查询、必要 Hook、present/resize 验证、销毁。完全不支持的机器仍能运行普通渲染。

## Relevant Files

- 优先读取：`Engine/Src/RHI/DX12/D3DContext.cpp` 与实际 Device/SwapChain 封装；启动与结束生命周期所在文件；编译脚本。
- 主要新增：`StreamlineRuntime.{h,cpp}`、`StreamlineDx12Bridge.{h,cpp}`（本阶段桥接只实现 hooks/device，不实现 RG）。

## Milestones

当前只执行 M0；M0 验收后进入 M1。

## Current Milestone

M0。

## Progress

- [ ] 真实源码/SDK/GPU/构建信息记录到 `progress.md`
- [ ] SDK 可选开关 + 同版本 SDK 依赖 / 安全加载
- [ ] Runtime 初始化、Shutdown、capability、日志
- [ ] Manual Hooking 所需 native/proxy 接线完成
- [ ] `presentCommon` 每帧命中（包括 Resize 后）
- [ ] SL off / SL unavailable / SL loaded 三种路径验收

## Decision Log

优先在 `progress.md` 记录：动态加载与静态链接具体取舍、完整 Hook 列表、未执行路径的 fallback 决策。改变 README D001 前需给出与官方指南对应的证据。

## Surprises & Discoveries

- 官方文档明确：Manual Hooking 可能在 `slInit` 前创建 D3D device，但 SDK 仍必须在相关 Hook 执行前初始化；选择稳定的启动顺序。
- 即使首阶段没有 DLSS Evaluate，presentCommon 的每帧验证仍是 Manual Hooking 的硬性前置。

## Validation

**必须留证据**：

1. `AILU_WITH_STREAMLINE=0`：编译/启动、显示场景、TAA、UI/Present/Resize 未回归，且无需 SDK DLL。
2. `AILU_WITH_STREAMLINE=1`，手动移走 DLL/加载失败：启动不崩溃，明确错误，自动走 native；确保错误路径没有半代理对象。
3. SDK 完整但非 RTX：Capability 表示不支持，程序正常使用普通 TAA/Blit。
4. 有 RTX：Capability 检测正确；**无需**显示任何 DLSS 画面。
5. Manual Hooking：正常运行、窗口 Resize、切换前后台可观测 `presentCommon` 正确执行；DX12 Debug Layer 没有新增致命错误；未违反必要 Hook 列表。
6. 退出后重复启动 3 次，无 Runtime 二次释放、COM 引用泄漏、SwapChain proxy 错误。

### Acceptance Criteria

- [ ] 以上 1～6 符合可执行硬件环境；不能执行的项标为 Blocked，不得标为 Passed。
- [ ] 若没有 RTX，允许**M0 限定通过**并注明真实 SR 的 M2 验收被阻塞；但不能跳过 Hook 测试。
- [ ] 变更局限于 build/runtime/DX12 hook 层，TAA/Postprocess 无逻辑改动。

## Known Follow-ups

M1 加入 FrameToken、Viewport、CommandCustom 入口和原生资源生命周期；M2 添加真正 SR 特有逻辑。

## Outcomes

待执行者填写：新增/修改文件、build 日志路径、hook 断点证据、capability 与退出结果。

## Resume Protocol

阅读 README 与 progress；继续未勾选项。M0 Acceptance Criteria 不满足时严禁开始 M1；同步 `progress.md` 的验证/决策和 Next Step。

## References

- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
