# M1 · RenderGraph / CommandCustom / Native Resource Bridge

## Purpose

让 Streamline 后续功能能在**正确 GPU 录制顺序**里安全访问原生 DX12 Resource/CommandList，并验证资源状态与生命周期。M1 使用可控的原生命令 smoke test，不真正调用 DLSS。

## Scope

### 1. 用现有 CommandCustom 暴露受控入口

1. `GfxCommand.h` 已定义 `CommandCustom`，`D3DContext::ProcessGpuCommand` 已能回调 `_func(cmd_buffer)`，不新建 `EGpuCommandType`。
2. 给 `CommandBuffer` 增加**一个**显式接口，例如 `RecordNativeCallback(std::function<void(RHICommandBuffer *)> callback)`；禁止空回调。把 callback 构造进现有 CommandPool 分配系统，跟踪析构，防止 `std::function` 在池释放时泄漏。
3. 审阅 `CommandBuffer::TakeCommands()`、`CommandBuffer::Clear()`、`DestroyCommand()`、command group 的跨线程转移。不能通过引用捕获 RG local、栈上的 `sl::ResourceTag`、临时 `RenderingData`、临时 Texture*。
4. native callback 只执行“对**传入的** RHICommandBuffer 操作”，不创建新 list、不手工提前 submit、不绕过 `GpuCommandWorker::RecordCommandGroup`。其他 RHI 可保持不可用/明确报错。
5. 如引擎允许并行录制，需要在 DLSS Pass 设置 serial/ordered 属性，避免 SL 调用与 FrameToken/Viewport 选项在多线程并发；检查 `CompiledRenderPass::_allow_parallel_recording` 与提交序列相关逻辑。

### 2. 原生资源访问与生命周期

1. 从 RG pass 中获取 `RGHandle`；在该 Pass 执行录制时通过 `RenderGraph::Resolve<Texture>()` 得到资源；再通过 DX12 `NativeResource().As<ID3D12Resource>()` 获取原生对象。
2. Frame/Viewport 记录不保存可被下一帧复用的 RG handle/raw pointer；Custom callback 捕获稳定的资源 handle/拥有者，执行时 Resolve，必要时通过 `GpuResourceRegistry` 查找并验证句柄 generation。
3. 保存的 COM 资源强引用、RDG 池 lease 和 command packet 的释放时机必须晚于 GPU 对资源的最后一次使用；沿用现有 `RHICommandBuffer::ReleaseTrackedResources()` / FrameResource 的 fence 流程。
4. 定义 `StreamlineFrameContext`：`engine_frame_id`、对应 SDK `FrameToken`、稳定 `ViewportHandle`、Camera 视口 ID、`render_size`、`output_size`、`history_reset` 与生命期；严格区分“全局一帧”和“一帧中的多个 camera”。
5. `slGetNewFrameToken()` 由明确的 frame begin 执行、每 frame 一次；未来多 Viewport 复用相同 frame token、不同 viewport；不要将 engine 帧计数直接当 SL token 指针。

### 3. 状态和 barrier 合约（不可跳过）

M1 要写下并实现可测试的合约，而非靠 `ResourceState=COMMON` 猜测：

- **SL 入口前**：从 RG `_resource_usages` / `RequireState` 等得到输入 color、depth、mvec 和输出的已知实际 DX12 state；标记的 `sl::Resource` state 必须与调用点一致。
- **SL 期间**：依据对应版本 SDK 文档由 SL 管理其内部转移；避免本引擎“又认为是 UAV 又认为是 SRV”的双重跟踪。
- **SL 结束后**：明确哪些 state 经 SL 恢复、哪些需由引擎重新断言，并让后继 Postprocess Pass 获得真实的 barrier。SDK 无可查询最终 state 时，在桥中加入显式受控转换/状态同步，而不是让 RG 自行猜测。
- **命令状态**：官方 Manual Hooking 不为引擎自动恢复所有 D3D12 CommandList 状态；`ResetGraphicsStateCache()` 仅是缓存失效，要验证下一次 Draw/Dispatch 可正确重设 PSO、RootSignature、Heap、root slots。

**提交到 M1 的方案必须包含状态转换表**，列出资源（input color/depth/mvec/output）、入口真实 state、SDK tag state、退出后的已知 state、下一 Pass 预期 state、责任方。

### 4. RG native smoke Pass

暂时以开发宏实现一个关闭时完全不生效的 RenderGraph 验证 Pass：

1. 用正常 RG API 声明输入只读，输出写入和下一 Pass 读取依赖。不要因为 callback 是 opaque 就绕过 RG dependency。
2. 在同一命令列表中录制一条无害 Native marker / 可验证操作（例如 PIX marker），再安排一条已有 CommandBuffer Draw/Dispatch 用于验证缓存和录制顺序。
3. 用有标记的资源查询 native 描述/状态/尺寸，验证 callback 执行于 GPU worker **录制阶段**而非 RG setup/main thread。
4. 在 Debug Layer/PIX 中确认 RG Pass 之前/之后无非法 resource barrier、UAV hazard、use-after-free、错序执行。
5. 测试 `RenderGraph` 重编译、Camera 重复渲染和 resize 后，回调不会引用上一次图构建的 handle。

## Non-goals

不依赖真正 RTX，不调用 `slEvaluateFeature()`，不修改 Motion Vector、TAA、Bloom、ToneMapping，也不尝试封装“所有可能的 SL Feature”。

## Architecture Constraints

- CommandBuffer 添加最多一个通用 callback 录制接口；禁止在 CommandBuffer 公共头里 include SDK。
- 桥接代码全部留在 DX12/Streamline 模块内部；`sl::Resource`、D3D12 state 枚举不泄漏到 Camera/Postprocess。
- command packet 需要以值捕获稳定元数据，不允许长存 RG 执行时裸引用。
- 禁止以 CPU 录制完毕代替 GPU fence 完成。

## Baseline

- `Engine/Inc/Render/GfxCommand.h`：`CommandCustom`。
- `Engine/Src/RHI/DX12/D3DContext.cpp`：`ProcessGpuCommand` 和 `GpuCommandWorker::RecordCommandGroup`。
- `Engine/Src/Render/RenderGraph/RenderGraph.cpp`：已有 resource barrier compiler/validations。
- `Engine/Inc/RHI/DX12/D3DCommandBuffer.h`：NativeCmdList 与 GraphicsStateCache。
- `Engine/Inc/Render/CommandBuffer.h` 尚无公开 Custom 命令入口。

## Target Outcome

引擎先不运行 DLSS，已能安全在指定 RG Pass 中录制 native callback，正确取得当前命令列表/资源并通过后继 Draw 测试；可证明资源和线程/栈对象生命周期安全；该入口可直接供未来 DLSS SR、RR 重用。

## Relevant Files

- `Engine/Inc/Render/CommandBuffer.h`
- `Engine/Src/Render/CommandBuffer.cpp`
- `Engine/Inc/Render/GfxCommand.h`、`Engine/Src/Render/GfxCommand.cpp`
- `Engine/Src/RHI/DX12/D3DContext.cpp`
- `Engine/Inc/RHI/DX12/D3DCommandBuffer.h`
- `Engine/Src/Render/RenderGraph/RenderGraph.cpp`
- `Engine/Inc/Render/RenderingData.h`
- 新增 `StreamlineDx12Bridge.cpp` 的 resource/frame 辅助实现

## Milestones

仅 M1；完成独立验收后才能开始 M2。

## Current Milestone

M1（进入条件：M0 已经验收并写入 progress）。

## Progress

- [ ] CommandCustom 公开受控录制函数 + 回调完整析构测试
- [ ] RG 回调和 GPU 录制线程顺序确认
- [ ] NativeResource/FrameToken/Viewport 的薄接口
- [ ] GPU Fence 资源保活检查
- [ ] RG 与 SL 资源 state/barrier 合约记录并测试
- [ ] 后续 Draw/Dispatch 的 CommandList 状态恢复策略验证
- [ ] native smoke Pass 通过 debug/PIX

## Decision Log

把每一种 native callback 执行约束、资源强引用方式、barrier 规则和 SL SDK 线程同步策略记录到 progress。改 RenderGraph 大范围公共实现前优先评估独立桥接可否满足。

## Surprises & Discoveries

- `ProcessGpuCommand(kCustom)` 已支持 `std::function`，但 Pass 不可直接从 `CommandBuffer` 提交。
- graphics cache invalidation 和真实 D3D12 state restoration 不是一回事。
- RenderGraph 的声明式读写/barrier 不能因 SDK 会自己转换资源就省略。

## Validation

1. 不打开 SL Runtime 的普通场景画面/帧顺序保持不变。
2. Callback 执行恰好一次、命令序正确，不使用临时栈上的 capture。
3. CommandPool Reset/TakeCommands 后 `std::function` 析构恰好一次，没有双重释放。
4. 多次 Resize/切换 Camera、最少 100 帧无句柄失效和 RG 资源错误。
5. Callback 前后 Draw/Dispatch 结果与无 callback 基线一致。
6. Debug Layer/PIX 无新增 state/barrier/descriptor 错误；写明真正测试所用设备和场景。

### Acceptance Criteria

- [ ] M1 1～6 通过且记录链接/日志。
- [ ] 未增加专用 GfxCommand enum、独立 command queue、全局 RHI 对 SL 的依赖。
- [ ] 完成可审查的 RG↔SL state table。

## Known Follow-ups

M2 的 DLSS tags/evaluation 基于 M1 桥接；若官方 SDK 在实际机器要求额外 barriers，在 M2 仅小幅扩展 table 与桥，不修改已验证执行顺序。

## Outcomes

待填写：改动文件、bridge API、线程模型、状态表、Debug/PIX 结果。

## Resume Protocol

只读取 README、progress、M1 文件；确认 M0 Passed；执行剩余勾选项；失败时留在 M1 并记录；全部通过后更新 M2 为 Current Milestone。
