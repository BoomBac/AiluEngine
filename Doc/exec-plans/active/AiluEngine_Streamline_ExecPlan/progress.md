# Streamline ExecPlan · Progress

> 这是跨 context 的唯一进度事实来源。只有在测试真正发生之后才改为 Pass。

## Current Milestone

**M2 · DLSS SR 画面闭环（用户明确要求开始；M1 验收仍 In progress，M0 完整矩阵仍 Blocked）**。入口：`m2-dlss-sr.md`。

## Progress

| 项目 | 状态 | 证据 |
|---|---|---|
| 源码基线识别 | Done（静态阅读上传快照） | `AiluEngine_Source(20261012-111424).zip` |
| 官方 SDK 版本核对 | Done（网页确认） | Streamline SDK v2.14.1 |
| SDK 安装 / 编译配置 | Done（SDK-on build 通过） | 可选 `AILU_WITH_STREAMLINE`；SDK 2.14.1 位于 `F:/AiluBuild/deps/Streamline/2.14.1`；最终 `build-debug` 成功 |
| M0 初始化、hook 与能力查询 | Runtime 部分通过 | RTX 5070 上 SDK / `sl.dlss` 加载、DLSS SR capability、Device/Factory/SwapChain proxy 初始化成功；本次观察到 proxy Present |
| M0 验收 | 本机修复复验通过；完整矩阵仍 Blocked | `m0-repair-20261012.md`：COM ownership/native 路由已修复；生产 SDK 606/606 hook 配对（含 Resize/恢复）；最终三次 30/30 正常启停且 live-object=0；非 RTX 与完整视觉矩阵未完成 |
| M1 验收 | In progress | `CommandBuffer::RecordNativeCallback` 已加入；GPU worker 执行后的状态 cache 失效已接入；其余 M1 验收未执行 |
| M2 实现 | In progress | Camera 有效模式/尺寸决策、Streamline SR RenderGraph pass、fallback 输出与按实际输入尺寸运行后处理已实现；修复显式分配 frame 0 与帧级 constants/tags 不匹配导致的 result=25，改为 evaluate 自动初始化；Debug build 和桥接/Camera smoke 通过；RTX 图像和 D3D12 state 验收未执行 |
| M2 验收 | Pending | `build-debug` 通过；Evaluate 成功帧、深度/MV convention、Debug Layer/PIX 与图像质量仍待实机验证 |
| M2 叠加偏移修复 | 源码/编译通过；视觉 Pending | 修复全尺寸 SR color 与低尺寸 DSV 导致的 DX12 viewport clamp；独立最终深度和输出尺寸 Editor 叠加；`build-debug` 退出码 0、depth PS DXC 编译通过、CameraTemporalHistorySmoke 通过；无 live Editor，未抓帧 |
| DLSS Off 天空回归 | 具体根因定位并修复 | 首帧未初始化 SR/TAA handle 错选 4x4 输出，新增深度 v0 被 PickBuffer 读取导致整图编译失败，丢失一次性天空/IBL 生成；初始化图句柄并传递深度已写入版本，旧 SkyView dirty 逻辑恢复；首帧完整 execute 日志和天空/FinalBlit 各 8 连续帧通过；确定性内存 regression 前失败后通过；证据 `SkyRootCause_20261013_001`；临时日志/配置已恢复 |
| M3 验收 | Pending | |

### 环境与复现信息（由执行者填写）

- 工作树路径 / commit：`C:/Users/BoomBac/OneDrive/AiluEngine` / `d428e9b2a1cd3f768ac29bdcac2c58e65724cd2c`
- MSVC / CMake / VS 版本及编译配置：MSBuild 18.11.0、CMake 4.3.2、Visual Studio 18 2026、`build-debug`
- Windows / GPU / RTX 驱动版本：Windows 10 Pro 25H2 (26220) / GeForce RTX 5070 / 610.88
- SDK 路径及发行版本：`F:/AiluBuild/deps/Streamline/2.14.1` / v2.14.1；运行时校验 NVIDIA 签名和 DLL 文件版本 2.14.1.0
- Feature plugins/runtime DLL 路径：构建后复制到 Editor/Player/Test 输出目录的 `Streamline` 子目录；运行实测 `sl.common`、`sl.dlss` 已加载
- 编译命令：`cmake --preset debug -DAILU_WITH_STREAMLINE=ON -DAILU_STREAMLINE_SDK_ROOT=F:/AiluBuild/deps/Streamline/2.14.1`；`cmake --build --preset build-debug`
- 本次证据目录：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/StreamlineM0Acceptance_20261012_2055/`；实际所测 executable 为同目录上一级的 `Editor.exe`。
- 运行命令/测试场景：`Editor.exe <evidence>/clean-project/M0.ailuproject --exit-after-frames 30`；固定 `empty.almap`（全局 PostprocessVolume + directional light）。隔离测试临时关闭 Blender Live Link autoload，退出后恢复；完整 DLL 三次退出码 0、缺 interposer DLL 一次退出码 0。SDK-off 暂移整个 Streamline 目录，TAA/Resize 捕获完成并使用实际 HWND 投递 `WM_CLOSE`，退出码 0。
- 图像：SDK-on/off 分别在 Resize 前及 1280×720、1600×900 后捕获 4 帧 `TemporalAA` PNG（共 24 帧）；每组连续且无黑帧。同尺寸代表帧 RMS 0.299 / 0.757 / 0.541（8-bit RGB）；只证明固定空场景的有界对照，不代表完整 Editor 视觉验收。

## Decision Log

- P001（2026-10-12）：计划分四阶段；M0 仅 SDK + hooking/capability，M1 仅桥接，M2 SR，M3 稳定性。
- P002（2026-10-12）：默认缺失 SDK 时支持无 SL 的编译；SDK 二进制和 plugin 版本必须保持一致。
- P003（2026-10-12）：当前仅产出执行计划，不伪造编译/帧截图/设备运行结果。
- P004（2026-10-12）：以动态安全加载 NVIDIA 已签名 `sl.interposer.dll`；`AILU_WITH_STREAMLINE=OFF` 保留原生路径，SDK/plugin 二进制不提交仓库。
- P005（2026-10-12）：Streamline 2.14.1 的 D3D12 command queue proxy 持有 device proxy 的裸指针；D3DContext 需保留 device proxy 至 swapchain/queue 销毁后，再调用 `slShutdown`。
- P006（2026-10-12）：验收使用隔离项目并临时关闭 Live Link autoload，避免正在运行的 Blender 向测试项目导入场景。原配置与插件 manifest 均按字节备份/恢复；验收发现不自动修改 runtime 源码，也不推进 M1。
- P007（2026-10-12）：用户随后授权修复。通过 SDK 官方 development PDB 确认 wrapper callee，再对生产 common DLL 和引擎 Present 设置进程内临时断点；不修改 SDK 文件。逐次配对计数与图像、退出、live-object 分开验收。
- P008（2026-10-12）：完整 teardown 报告实测发现 timer、RootSignature pool、EnumAdapters 引用残留；分别补虚析构、context 销毁时 Clear 和 ComPtr ownership。报告移到 context/GPU manager 销毁之后，日志可留存。
- P009（2026-10-12）：用户明确要求越过 M1 验收开始 M2 实现；不将 M1 callback/state 恢复缺口标为通过，M2 的 Evaluate/state/图像验收独立记录。

## Surprises & Discoveries

- `CommandCustom` 已存在，但在 CommandBuffer 中没有直接公开录制方法。
- 旧 Postprocess 纹理维度来自 `RenderingData::_width/_height`，不是 Postprocess 实际输入 RG Texture。
- 官方 Manual Hooking 要求每帧正确执行 `presentCommon()`。
- 上传快照不包含足以确认用户真实本机编译过程的全部环境信息。
- 旧 smoke 的首帧/Present 阻塞已被本次隔离场景复测替代：能够观察 proxy Present，三次受控运行正常退出。但仍不能把首次 proxy Present 当成 `presentCommon` 每帧命中的证据。
- 首次未隔离插件的 120 帧测试收到 Live Link 数据，退出时在 `ResourceMgr::ReleaseRuntimeResources -> Asset::~Asset -> shared_ptr` 访问违规；禁用插件的 clean fixture 不再复现，未证明崩溃根因属于 Streamline。
- 构建缓存虽为 `AILU_ENABLE_D3D_DEBUG_LAYER=OFF`，当前 EngineConfig 的 runtime 开关仍实际启用了 DX12 Debug Layer。以运行日志为准，on/off 均记录相同 warning IDs，未观察 severity 0/1。
- 隐藏窗口的 `Process.CloseMainWindow()` 不是可靠关闭驱动；一次 capture run 被强制终止，不能计作正常退出。SDK-off 改用已枚举的真实 HWND 投递 `WM_CLOSE` 后正常退出。
- 一次增量链接尝试曾报 `LNK1201`（`Engine_d.pdb` 写入失败）；后续相同 `build-debug` preset 在最终源码上成功完成。

## Validation

| Milestone | 编译 | 运行 | Debug Layer / PIX | Acceptance Criteria | 状态 |
|---|---|---|---|---|---|
| M0 | 修复后 SDK-on `build-debug` 通过；SDK-off bridge/runtime 编译与 WARP regression 通过；完整 SDK-off 构建属于修复前证据 | 最终三次生产 SDK 正常退出、每次 Present/common=30/30；缺 DLL 回退退出 0；Resize/恢复 common=606/606，16 帧 TAA 有效输出 | 最终三次无 severity 0/1，DXGI live-object hr=0、messages=0；PIX 未执行 | 此前源码/hook/COM 阻塞已解除；完整 SDK-off 复验、非 RTX 真机和完整视觉矩阵未完成 | 完整矩阵 Blocked |
| M1 | `build-debug` 通过 | BridgeSmoke 通过（只覆盖 WARP/null/ownership 路径） | 未执行 | callback 执行/析构、RG Pass、资源 GPU fence、真实 Draw 状态恢复、PIX/Debug Layer 仍待验证 | In progress |
| M2 | SDK-on `build-debug` 通过 | 未执行 | 未执行 | RTX Evaluate、fallback、depth/MV/jitter convention、state/barrier 和图像验收待执行 | Implementation in progress |
| M3 | 未执行 | 未执行 | 未执行 | 未执行 | Pending |

## Blockers

- 原三个源码/hook/COM 阻塞均已修复并留证；旧验收记录保留为历史，最新事实见 `m0-repair-20261012.md`。
- 非 RTX 真机、修复后完整 SDK-off 构建/启动和完整 ImGui/最终后处理视觉矩阵尚未完成；不能以本机 RTX 门禁通过替代这些项。
- 最小化后恢复验证曾因测试驱动使用 non-activating restore 而暂停，需要实际恢复/焦点操作；捕获最终成功，但该场景的恢复延迟不作为性能或正常帧率证据。

### M1 当前记录（2026-10-12）

- 修改：`CommandBuffer::RecordNativeCallback` 将非空 callback 按值移动到现有 `CommandCustom`/`CommandPool`；DX12
  `ProcessGpuCommand` 在同一传入命令列表调用 callback 后使 graphics state cache 失效；`StreamlineDx12Bridge::GetNativeResource`
  返回带 AddRef 的 DX12 resource，并拒绝空/非 DX12 resource。未新增 command enum 或 queue。
- 源码检查：RenderGraph 先对 compiled usages 录制 `RequireState`，DX12 command group 按命令顺序在 GPU worker 录制；
  group 析构时才归还 command packets，state tracker 的资源强引用由 fence 完成路径释放。
- 验证：`cmake --build --preset build-debug` 和目标重编译成功；`StreamlineDx12BridgeSmoke.exe` 退出码 0，覆盖 WARP 下
  DX12 resource AddRef 返回、空/错误 backend 拒绝、32 次失败 proxy upgrade 不改变 device 引用数、GPU timer teardown 释放资源。
- Callback 生命周期测试尝试：独立测试目标无法从 Engine DLL 链接内部 `CommandPool::{Init,Get,Shutdown}` / `DestroyCommand`；未为测试目的
  扩大内部池的导出 API，删除了该试验目标。callback exact-once/析构仍只能通过源码审阅，尚无动态证据。
- 限制：未驱动 RenderGraph callback，也未证明 callback 在 GPU worker 上恰好执行一次；未验证 resize/recompile、DLSS 实际状态/barrier 与随后
  Draw/Dispatch。`ResetGraphicsStateCache()` 只让引擎后续 setter 重新下发状态，不能单独证明任意 native callback 后所有 D3D12 状态都已恢复。
  M1 保持进行中。

### M2 当前记录（2026-10-12）

- 修改：Camera 增加 DLSS requested / available / effective C++ API；`RenderPipeline::Render()` 在 Camera temporal prepare 前查询 SDK optimal render size，保留用户 `RenderScale`；有效 SR jitter 与 TAA pass 互斥。
- 新增 `StreamlineSrPass`：读取 CopyColor HDR、RFloat depth、motion vectors，创建 output-size RGBA16F UAV；先记录 spatial blit fallback，再在同一 RG 输出上执行 evaluate-local tags 的 DLSS Evaluate。RenderGraph resource state 传入 shader-resource 合并状态与 UAV 状态。
- Postprocess：`PostprocessStack` 优先使用 SR output；Bloom mip/output 和 ToneMapping 输出从 RG source `TextureDesc` 派生尺寸。
- SDK frame token 使用 RenderGraph 录制时捕获的 frame index；SL API 调用串行化。Evaluate 失败记录 viewport 请求，下帧切回 Camera 原 TAA/空间路径；失败时保留已生成的空间 fallback。
- Scene View Camera 面板新增 DLSS SR Off/Quality/Balanced/Performance 选择和状态文本；运行时用户选择 Quality 后报告了 896x512 viewport 绘制到 597x341 target 的 D3D12 warning，以及 DLSS Evaluate 失败。
- 诊断修正：用户复测仍出现 896×512 viewport 对 597×341 target 的 #1378，证明单靠 `CommandBuffer::Clear()` 重置不充分；DX12 `SetTarget` 现按当前 RTV/DSV 最小有效尺寸裁剪提交的 viewport/scissor，覆盖颜色与 depth-only 路径。
- DLSS 失败定位：SDK 2.14.1 的返回码 25 是 `eErrorInvalidParameter`。按 SDK 帧资源流程改为先提交 constants 和 `slSetTagForFrame`，再按 output size/mode 调用 `slAllocateResources`，最后用 viewport/frame token 执行 `slEvaluateFeature`；失败日志保留阶段和返回码。
- 崩溃根因修正：用户后续日志中的返回码 24 是 SDK 的 `eErrorExceptionHandler`，同时 `sl.common.dll` 捕获到访问冲突。检查 SDK 2.14.1 `commonEntry.cpp::setTagCommon()` 发现 `numResources` 按连续数组迭代，并为每个数组项继续遍历其扩展链；旧代码将 4 个 ResourceTag 串链后仍传数量 4，导致同一组资源被重复标记。现改为 4 个互不串链的数组项，并显式将未使用的 `cameraPinholeOffset` 初始化为 `{0, 0}`。
- 验证：针对 tag 数组和 constants 修正后再次运行 `cmake --build --preset build-debug` 成功，Engine、Editor、Test binaries 均编译链接。最新用户日志证明上一版调用仍有 SDK 崩溃；本轮修正后尚未重启 Editor 复验异常是否消失、viewport warning 是否消失或 DLSS Evaluate 是否成功。
- 诊断增强：失败日志现在包含 Streamline API 阶段和返回码，便于区分 options、frame token、资源分配、constants 与 Evaluate 失败。
- 限制：修复后的 viewport warning 和 DLSS Evaluate 结果仍待重启 Editor 复测；Depth/MV 矢量约定及实际 DX12 tracker/Streamline state 在 GPU 上尚未验证；M1 native callback 后完整 state restore 仍是前置风险。

## Next Step

继续完成 M2：先审阅并修正 SDK 资源/viewport 生命周期风险，再运行 RTX 固定场景与 Debug Layer/PIX 验证。补 Inspector requested/available/effective 控件。M1 callback 生命周期、RenderGraph 执行和后续 Draw 状态恢复仍未验收；不得将其标记通过。

## Known Follow-ups

- SR 成功后再评估 RR 与 FG 独立接入计划；不在 M0 阶段创建空 Feature 接口体系。

## Outcomes

- [ ] M0 SDK & Hook 通过
- [ ] M1 原生命令/资源桥接通过
- [ ] M2 DLSS SR 图像闭环通过
- [ ] M3 工程稳定性通过

## Resume Protocol

每次新 session 先读取 README、本文件和当前 mX，检查工作树；完成当前阶段前严禁跳下一阶段。重要变化写 Decision Log；构建失败及复现日志写 Validation/Blockers，保留未完成步骤。阶段完成后更新 `Current Milestone`、`Next Step`。
