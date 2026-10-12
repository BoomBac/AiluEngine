# M3 · 运行稳定性、回退机制与未来 DLSS Feature 扩展点

## Purpose

在 M2 已显示正确 DLSS SR 画面后，补齐生产环境稳定性和跨 Feature 可复用接口，但**不在 M3 实现 RR 或 FG**。把未来接入新 Feature 的新增修改集中到 Streamline 模块、特定 Pass/Coordinator 和最少资源标签。

## Scope

### 1. 生命周期与应力测试

- 确保 `slInit/slShutdown`、Device recreate、Scene unload、Viewport 销毁、Resize、Present、GPU Fence、`slFreeResources()` 生命周期有清晰时序。官方规定 GPU 命令仍有 Evaluate 时不得提前释放资源；释放前等待对应 command list flush/完成。
- 反复做主窗口 Resize、最大化/最小化、设备失活/恢复、应用退出重开、关闭 UI/TAA/SR 模式；记录每次资源占用。检查 plugin 可见的 frame completion/present 逻辑没有暂停累积。
- Scene/Editor 多 Camera：验证独立 Viewport ID、FrameToken、history reset，且 camera 一次 render 结束后不会串用另一个 Camera 的设置。若无法按期完成，多 Camera 明确保持禁用 + fallback，不能暗中共享历史。
- 高 CPU 并行录制/frames in flight：验证 SDK 被调用顺序与 Renderer graph/pass 顺序一致。若 SL 指出 API 非线程安全，使用最小锁/顺序队列控制，并收集实际性能数据。
- Resource pool：GPU 对 color/depth/motion/output 的使用完成前不得复用或者释放；用 Debug Layer/PIX 和多帧资源检查证明。

### 2. 运行时可观测性与错误分级

添加轻量状态诊断（可在现有 Editor Inspector/调试窗口中显示，无需新 UI 框架）：

```text
Streamline runtime: off / failed / loaded
Feature support: SR supported / unsupported (reason)
Requested / Effective mode
Viewport ID / Engine Frame / SL FrameToken state
Render Size / Output Size / Jitter unit
Last DLSS Result / Last Failure / Current Fallback
SDK / Plugin version and selected DX12 adapter
```

- 错误不应按每帧重复刷屏；用已有引擎日志的 rate limiting/去重机制。
- SDK 丢失、GPU 不支持、版本不兼容、resize 失败、evaluate 失败、resource tag/state mismatch 都转化为能诊断的状态。
- 不采纳自动下载 DLL、跨平台自动安装或未知路径 DLL 加载。

### 3. 最小 Feature 扩展合同（保留需求，而不是提前写死）

**共同部分由 runtime/bridge 提供**：

| 共享能力 | 应由谁提供 | 新增 RR/FG 时是否重写 |
|---|---|---|
| `Init`、安全加载、SDK 路径、capability、shutdown | `StreamlineRuntime` | 否 |
| `featuresToLoad` 所需 feature 列表 | `StreamlineRuntime` | 只追加对应 Feature，并处理重新初始化要求 |
| native device/factory/swapchain、必要 hooking | `StreamlineDx12Bridge` | 否；特殊 present 要求另外扩展 |
| FrameToken / Viewport 身份 / 生命周期 | `StreamlineRuntime` | 复用，但按 Feature 区分历史/状态 |
| CommandBuffer native callback、原生资源与 state mapping | `StreamlineDx12Bridge` | 对在 RG 中执行的 Feature 复用 |
| `slSetTagForFrame` 包装与 tag 生命周期 | `StreamlineDx12Bridge` | FG 可复用；按特性扩展 tags |
| Feature-specific options/SDK function 调用 | 各 Feature Pass/Coordinator | **各自实现** |
| Feature-specific render inputs、调度时机、回退 | 各 Feature Pass/Coordinator | **各自实现** |

**禁止错误的统一抽象**：SR/RR 使用命令列表执行 Evaluate，FG 则是 presentation 驱动，不能为了 API 外形统一强行实现 `EvaluateFeature()` 虚接口并在 FG 中伪调用；Reflex 是 marker/latency 管理而非普通 RG 超分 Pass。

### 4. 后续技术切入点（仅留接口文档，不实现）

#### DLAA（SR 变体）

- 可在已有 SR 选项/策略上扩展具体 SDK mode；源尺寸/目标尺寸相等，保留与 TAA 的互斥。
- 不需要新 Runtime、Command Queue 或 RG 外的 Texture Pool。

#### Ray Reconstruction / DLSS-RR

- 新 `StreamlineRrPass`，复用 runtime/FrameToken/Viewport/ResourceTag/NativeCallback。
- 需要**与当前 PathTracer/RTGI 的 noisy diffuse/specular、ray hit distance、正常 GBuffer/Depth/MV 对齐**，替代正确的降噪阶段；先独立检查官方 RR plugin `sl::kFeatureDLSS_RR` / `sl_dlss_d.h`（随 SDK 版本确认）。
- 资源 tag 与 history 不直接沿用 SR；必须复查 RR 输入一致性，不要承诺“加个 mode 即可”。
- 适合后续独立 ExecPlan，可能需要多个 milestone。

#### Frame Generation / MFG

- 新 `StreamlineFgCoordinator`，集中于 swapchain/present/frame scheduling；复用 M0 的必要 hooking，但另行评估 `PresentImpl()`、`_render_windows`、ImGui/HUD-less Color、UI alpha/mask、Depth/MV 持久化 tag、Reflex/frame pacing、Resize/FrameIndex。
- FG 不应直接复用 `StreamlineSrPass` 的 Evaluate slot：官方说明 DLSS-G 由 SwapChain::Present 路径驱动。
- 可能需要通过 `slSetTagForFrame` 对 Depth/MV、HUD-less color、UI 专用资源设置跨 Present 生命周期；根据 SDK 版本验证正式要求。
- FG、MFG 的硬件型号和驱动支持矩阵单独查询；不要将 SR Supported 推导为 FG Supported。

#### Reflex

- 视官方版本要求，在引擎 frame scheduler/input simulation/render submit/present 位置放 marker；与 FG 的低延迟和帧节奏协作。不能作为普通 RenderGraph Effect 来实现。

### 5. 性能与对照

- 定点场景记录 native 1080p、TAA、DLSS Quality/Balanced/Performance 三组的 CPU prepare / RG Compile / GPU SR Execute / Present 占用；不要只看总帧率误判 FG 输出帧率。
- 记录启用 Streamline 时是否增加 DX12 API Hook/临界区/资源拷贝开销，及 Manual Hooking 路径相对原帧的增量。
- 比较 SR 输出在移动和静止时的细节、拖影和 shimmering，确保没有持续降级（例如全屏发虚因错误 MV）。

## Non-goals

- 不实现 RR/FG/Reflex/MFG，不抽象出 vendor-agnostic upscaler 架构，不重做全 RenderGraph / SwapChain。
- 不以“支持所有 NVIDIA 功能”为验收目标；只要求有明确复用边界和后续独立规划。

## Architecture Constraints

- 原始渲染路径仍可通过 `AILU_WITH_STREAMLINE=0` 完全关闭；运行时卸载前正确释放 GPU 资源和 proxy。
- 任何 DLL 与 feature 版本不匹配必须可诊断并优雅 fallback。
- `sl_*` 仅在私有模块内部，Camera/Renderer 不按 Feature ID 直接操作 SDK。
- 主工程 API 变更应继续严格小范围（重要更改需登记并解释为什么 bridge 不够）。

## Baseline

M2 已经实现并正确验证的 SR 画面与 M1 command/resource state 合约，是 M3 的不可破坏基线；若 M2 未达标不能进入 M3。

## Target Outcome

SR 是可以在 Editor 中长期运行、切换、resize、退出和恢复的可选 DX12 特性；日志、profiling、capability 一应俱全。新增 RR/FG 时开发者能明确哪些基础设施复用、哪些需要新 Pass/Coordinator，且无须重构已验证的 SR 主链。

## Relevant Files

- `StreamlineRuntime.{h,cpp}`、`StreamlineDx12Bridge.{h,cpp}`、`StreamlineSrPass.{h,cpp}`
- `Engine/Src/RHI/DX12/D3DContext.cpp`、真实 SwapChain/Device 文件
- `Engine/Src/Render/Renderer.cpp`、`Camera.cpp`、`PostprocessStack.cpp`
- Editor 当前诊断/Inspector；现有 Profiler/FrameDebugger/PIX 支持点

## Milestones

仅 M3；完成后本 ExecPlan 关闭。未来 RR/FG 另开新 ExecPlan。

## Current Milestone

M3（进入条件：M2 Passed，并有 RTX 真实运行证据）。

## Progress

- [ ] 长时运行与帧资源保活
- [ ] Window Resize/模式切换/退出/重复初始化
- [ ] Multi Camera 支持或明确 fallback 的验收
- [ ] 多帧并行及 SDK 非线程安全路径审计
- [ ] Runtime/capability/fallback 调试视图
- [ ] CPU/GPU/VRAM 性能数据与画面对照
- [ ] RR/FG/Reflex 扩展点表审查完成
- [ ] 最终无 SDK / 无 RTX / RTX 三条路径复测

## Decision Log

记录最终模块 API、需要/不需要被未来 Feature 共享的状态、可选与硬性支持范围，以及实际变更位置。任何大范围 RHI/Renderer 改写要求补回归数据。

## Surprises & Discoveries

- 是否出现 plugin 导致的额外 CPU/GPU 同步、postprocess 尺寸导致的内存峰值、Viewport 复用时 history 串用，由执行者写到 progress，不能预先预设结论。

## Validation

1. 显卡支持 SR：连续运行数千帧、Resize 多次、Camera 切换；无设备丢失、未初始化输出、持续 VRAM 增长或强制同步。
2. 反复 Off/TAA/DLSS 切换，截图显示每种有效结果，`requested/effective` 正确；若调试中不支持的 Camera，显示明确 fallback。
3. 完成 DX12 Debug Layer + PIX 检查，RG barrier 和 state cache 无错误，GC/CommandPool/Fence 资源释放按序。
4. Feature unavailable 的机器正常启动，退出无泄漏；编译宏为 0 时旧流程正常。
5. 记录 `presentCommon()` 在主/附加实际使用的窗口 Present/Resize 路径持续调用；无 proxy 误传第三方。
6. 留存性能基线和新增功能接口表。

### Acceptance Criteria

- [ ] 1～6 通过，且所有失败/例外（例如不支持的离屏 Camera）在用户可见/日志上明确。
- [ ] 新增其它 DLSS 的开发切入点明确，不需要重新侵入每一个 Render Pass。
- [ ] progress 最终 Outcomes 和 Validation 完整，已知遗留事项有记录。

## Known Follow-ups

分别建立 `DLSS RR`、`DLSS FG+Reflex` 新 ExecPlan；先梳理新的 Feature 输入和硬件要求，再对 Runtime/Bridge 做小幅拓展；另行评估 FSR/XeSS。

## Outcomes

待执行者填写最终变更汇总、性能对比和下一版任务列表。M3 全通过后标记整个计划 Completed。

## Resume Protocol

按 README → progress → 当前 M3 文件恢复；逐一执行测试与证据采集；验收未过继续留在 M3，不将 RR/FG 工作塞进当前里程碑。

## References

- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_dlss_d.h
