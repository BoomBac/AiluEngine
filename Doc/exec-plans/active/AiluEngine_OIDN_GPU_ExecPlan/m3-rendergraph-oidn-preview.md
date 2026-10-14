# M3 — RenderGraph 集成：Reference → OIDN GPU → HDR Preview（MVP）

## Purpose

复用 M1 的 Host/接口和 M2 的 DX12+OIDN GPU interop，使 OIDN GPU 真正服务于 AiluEngine 的静态渐进式 PathTracer；渲染线程**不因等待 OIDN 过滤而阻塞**。

## Scope

- `RayTraceGI` 的 Reference 累积结果 GPU Pack 到共享 Input Buffer；OIDN CUDA 在同卡过滤并写入共享 Output Buffer。
- DX12 根据已完成的 OIDN Fence，将 Output Buffer GPU Unpack 到独立 HDR RenderTexture，再经 `Debug GI` Blit 显示。
- 按 Reference 有效累积 SPP 节流，不改变追踪 Pass/Temporal Kernel；默认 `4/8/16/32/64` SPP 后再每 +32～64 SPP 触发一次。
- OIDN RT `hdr=true` Color-only，Quality 默认 Balanced；高画质 High 可由 UI 切换，但不能造成每帧 Filter 重建。
- 适配真实异步 CommandBuffer 提交顺序，严格管理外部 Fence / Resource State / 未完成请求。
- 诊断状态：Unsupported、Initializing、Idle、PackPending、Filtering、Unpacking、Ready、Failed、Retiring。

## Non-goals

不引入 ImageCapture/Readback/CPU OIDN，不实现 NRD、不分离 diffuse/specular、不变更 PathTracer 噪声采样算法。

## Architecture Constraints

### 具体 RenderGraph 插入位置

1. `RayTraceGI.cpp:706-765` 保留旧 `Denoise` Temporal pass。**只在 kReference、有有效 history 且此次 RayTrace 真正执行之后**才能 Pack。
2. 在旧 `Denoise` 与 `Debug GI` 之间调用 `PathTracingDenoiserHost::Record`；内部注册 `OIDN Pack HDR`（以及必要时注册 `OIDN Unpack HDR`）RG Compute Pass。
3. `builder.Read(history_curr_handle, EResourceUsage::kReadSRV)` 与 `builder.Write(shared_input_buffer, EResourceUsage::kWriteUAV)` 必须完整声明；Pack shader 用 `RWByteAddressBuffer` 逐像素写 RGBA32F/Float4，x/y 像素寻址 `(y*width+x)*16`，尺寸检查防越界。
4. `Unpack` 在 OIDN done Fence 真正确认完成后注册：`builder.Read(shared_output_buffer, ...)` + `builder.Write(preview_texture, EResourceUsage::kWriteUAV)`；如果后端用 UAV 读取 raw Buffer，则 Read/Write 的物理要求按真实 shader usage 声明和实现，禁止仅为骗过 RG validation 乱标依赖。
5. 共享 Output Buffer 的 DX12 状态必须在 External OIDN 工作期处于已确认的 handoff 状态；恢复/转换为 SRV/UAV 要纳入 queue tracker。明确外部跨 API 写入不被 RG 感知，DX12 读取前进行必要的跨 API 完成同步和 UAV/cache 可见性操作，按 M2 验证结果执行。
6. 在 `Debug GI` setup 和 execute 中使用**同一个已选择的 display handle**；debug target 1～5 继续优先显示原始调试数据。
7. Preview Texture 为独立 GPU `R32G32B32A32_FLOAT`、SRV/UAV 可用，**绝不复用** `_history_texture_a/b` 或 `_raw_path_trace_texture` 作为 OIDN Output。
8. 无 ready preview 时总能显示当前 Reference History；旧 Ready 只有 `generation/width/height` 全匹配时才能继续显示，不要求其 SPP 与最新帧完全相等。
9. RenderGraph 每帧 Compile/变化重编译时，导入的共享 Buffer 和 Preview Texture 是持久真实资源；改变 Resource 指针/版本必须更新 RG Import 关系，不可沿用过期 `RGHandle`。

### 一次 GPU 请求的 3 种 CPU/GPU 时序

```text
Frame N (CPU graph record):
  Reference History Temporal -> Pack(source SPP=N, shared input slot)
Frame N (DX12 actual submit):
  ExecuteCommandLists ... Pack -> GPU queue Signal(dx_ready, job._ready_value)
After submit callback (CPU, nonblocking):
  oidnWaitSemaphoresAsync(dx_ready, job._ready_value)
  oidnExecuteFilterAsync(oidn_filter)
  oidnSignalSemaphoresAsync(oidn_done, job._done_value)
Frame N+K record (CPU; poll only GetCompletedValue):
  if oidn_done fence complete + same generation:
      register Unpack(shared output slot -> persistent HDR preview texture)
Frame N+K DX12 submit/complete:
  Unpack produces new HDR preview; mark Ready only when dependency is valid
Frame N+K+1:
  Debug GI can select same-generation ready preview (or earlier when verified by DX12 queue ordering)
```

- 必须明确 `Ready` 是指 **OIDN completed + DX12 Unpack 已保证在显示消费之前有序**，不等同 `oidn_done` 单独完成。
- `GIPass` 的 `raw_executed` 是 `shared_ptr<bool>`/Pass execute 时才更新的门控；不要在 RG setup 阶段将尚未执行的帧当成有效 SPP。建议 Pack 的 execute 中再次检测，有效才进行 dispatch/登记 submitted job。
- `Pack` 需要对应的 `dx_ready` Fence Signal 只能在**包含该 Pack 的 DX12 GPU 命令真正提交以后**发出。建议对 `D3DCommandBuffer` 增加 private `ExternalGpuSignalRequest`，由 `D3DContext::ExecuteRHICommandBuffers` 在 `_command_submit_mtx` 下 `ExecuteCommandLists` 后依序执行 `queue->Signal(shared_fence, value)`；不允许在 pass 录制阶段直接访问队列调用 `Signal`。
- 如果当前 `ExecuteRHICommandBuffers` 将多个 CommandList 合批，signal 必须保证 *本 job 的 Pack 之前* 不插入相同 shared slot 下一次写入，也不能让多个 job 的 Fence 值跨批乱序；首版只允许一个 in-flight job。
- OIDN 调用串行且绑定同一 Device/Stream；不允许两个线程同时修改/提交同一 Filter；`executeAsync` 被提交到 OIDN device 后才允许排入 `SignalSemaphoresAsync`。
- DX12 队列在 GPU 输出完成前若不需要使用它，**不要提交会造成无意义队列阻塞的 Wait**；优先等下一帧非阻塞轮询 `oidn_done.GetCompletedValue()` 达标后再发 Unpack。也可使用 Queue Wait + 正确异步调度，但必须证明不会把整个渲染队列卡住。
- 三种资源所有权必须分清：`shared buffers + handles`（interop Job）；`preview texture`（GIPass/Host）；`DX12 command/fence wait/completion`（RHI）。避免 raw `this` 捕获到异步回调。

### 状态、资源、后端错误

- 默认 `max_in_flight=1`；共享 slot 必须等 OIDN 完成、DX12 Unpack 消费并达到安全重用 Fence 后才能准备下次 SPP 请求，禁止按每帧连续申请 GPU Shared Buffer。
- 用不可变 Job ID / generation；提交后的 Job 即使过期也要等待 GPU 释放条件，只能取消**发布**，不能立刻销毁 GPU Buffer / imported OIDN Buffer / semaphore。
- `resize/reset_history/disable/device lost/scene unload`：generation++，立刻显示最新有效 Reference 原图，旧任务 `retiring`，等 fence 完成才退役；绝不可显示上个场景的 OIDN 结果。
- `kNone` 的 branch 不能构造 OIDN Device、分配 Shared Buffer 或添加 Pack/Unpack Pass。
- Shader/Filter 初始化失败、应用缺 DLL、OOM、GPU 不受支持：`kNone` 回退，并提供一次诊断信息，禁止在每帧循环异常尝试。
- 不允许强制 Clamp [0,1]，也不要在 GPU Pack 做 ACES、曝光、Gamma 或 sRGB 转换；对非有限值与负 HDR 颜色按明确的无效样本策略清理并记录统计（OIDN HDR Color 要求非负）；合法 >1 HDR 永不 clamp 到 [0,1]。

## Baseline

- `RayTraceGI.cpp:706-790` 可以在 HistoryCurr 和 Debug GI 之间插入完整路径。
- `RayTraceGI.cpp:403-471` 给出 `reset_history`，用于 generation invalidation。
- `D3DContext.cpp:1149-1228` 控制真实 command list submission/Fence，M2 必须给出可用 queue signal Hook。
- `RenderGraph.cpp:915-~980` 构建 RG command records 再委托 GraphicsContext 异步执行，不能将 Record 当成 GPU 完成。

## Target Outcome

在受支持 GPU 的 AiluEngine 编辑器中，静止视角逐步累积时能够非阻塞看到 OIDN GPU 预览；相机/场景变更、关闭与 resize 安全；M3 完成即达到**GPU-only MVP**。

## Relevant Files

- `Engine/Src/Render/Features/RayTraceGI.cpp`
- `Engine/Inc/Render/Features/RayTraceGI.h`
- `Engine/Src/Render/RenderGraph/RenderGraph.cpp`（优先不改，必要时精确扩展）
- `Engine/Src/RHI/DX12/D3DContext.cpp`、`Engine/Inc/RHI/DX12/D3DContext.h`
- `Engine/Src/Render/CommandBuffer.cpp`、`Engine/Inc/Render/CommandBuffer.h`
- `Engine/Src/Render/Denoiser/OIDN/OidnGpuDenoiser.cpp`（新增）
- `Engine/Res/Shaders/hlsl/ray_trace/oidn_pack.hlsl`（新增）

## Implementation

1. M2 的 interop 代码抽成 `OidnGpuInteropD3D12`（private），负责 DX12 Device 和 OIDN Device 对应、Shared Buffer/Fence 创建与退役、Filter 重用。
2. 在 Host 设备激活时先运行 capability preflight；不支持直接返回 Unsupported，不触发 DX12 ShareHandle 创建；过滤操作按 SPP 节流。
3. 新增 `PackHDR` Compute Kernel：SRV Texture2D<float4> → RWByteAddressBuffer，存 RGB/A 到 linear RGBA32F；对 NaN/Inf 处理须明确且可追踪，正常 >1 HDR 不改。
4. 专用 Shared Input/Output Buffer 导入 RG；**在 DX12 管线中完成 UAV/COMMON 的 release/acquire**；DX12 做好 Pack command 的后提交 Fence Signal。
5. interop 按上述顺序调用 OIDN Filter Async，使用专用已导入 semaphore；job 只保存 immutable request+资源引用和 Fence value；GPU 不要 sync-to-host。
6. 下一帧在安全线程非阻塞 poll `oidn_done_fence`；完成则注册 Unpack pass，从 GPU Shared Output Buffer 写独立 PreviewTexture。若需 GPU Queue Wait，先证明无死锁和渲染排队阻塞。
7. Unpack 和 Debug GI 最终显示依赖必须在 RG 内连通；首次发布必须保证 Unpack 在显示读之前执行，而不是 CPU 标志提前设置。
8. 加入 `quality = Fast/Balanced/High`、SPP 触发间隔、Backend/Status/Last SPP/Last Result Latency/Error / Shared MB 的 Inspector 或现有 Debug 面板属性。
9. 加入 shader/SDK 资源尺寸切换缓存及明确最小复用方案；在尺寸变化/质量变化前确认上一批 GPU job 的销毁次序。
10. 验证 Compute 与 DXR 两种 tracer 都能提供 Reference History；在没有 DXR 的 GPU 上标记 DXR 未测。

## Milestones

本文件 M3，**GPU-only MVP**。M3 通过即可以交付，M4 非必需。

## Current Milestone

M3 实现完成，GPU plumbing 验证通过；完整视觉/性能 acceptance pending。

## Progress

- [x] 真实 submit hook、外部 semaphore 排队。
- [x] RG Pack/Unpack 及无额外 CPU copy。
- [x] GPU OIDN HDR RT 参数、静态资源复用。
- [x] Debug GI/Reference 显示、安全状态切换实现；场景视觉验收仍待执行。
- [ ] GPU Fence/资源生命周期、性能/内存验收。

## Validation / Acceptance Criteria

- [ ] 受支持的 NVIDIA Turing+ GPU、1080p Reference 累积 4/8/16/32 SPP 出现可见降噪 HDR 结果；高亮 >1 不被 clamp，颜色空间一致。
- [ ] 原 Reference 的真实积累 SPP 持续增加，Denoised 仅用于展示；关闭时回到实时更新的 Reference History。
- [ ] **没有** GPU→CPU→GPU HDR 通路，且 GPU 显示路径仍可用；GPU-only 不支持时安全回退。
- [ ] `kNone` 与原管线一致，RenderGraph 无错误依赖/Barrier；Debug GI 1～5 不变。
- [ ] 历史重置、Resize（1080p ↔ 720p）、UI 频繁开关、场景切换、Shader 热载时不会显示旧结果或发生访问冲突。
- [ ] 超过一次触发频率时请求上限 1 生效，不会无限积压 Job 或无界增长 Shared GPU 显存。
- [ ] 正常渲染帧的 CPU 没有 OIDN sync/wait 耗时；有明确的 GPU pack/oidn/unpack latency 以及主渲染帧耗时对比。
- [ ] 使用 DX12 debug layer/PIX/GPU crash validation 排查同步与资源状态，至少 100 次触发/resize/启停压力测试。
- [ ] GPU 不满足能力检查时不可自动调用 `OIDN_DEVICE_TYPE_CPU`，且 Debug/PathTracer 可以正常渲染。

## Decision Log

采用多帧异步完成和旧结果原位展示，避免每次等待外部 CUDA Filter 对整个 DX12 图形队列造成尾部阻塞。

## Surprises & Discoveries

RenderGraph 的 CPU `Record/Execute()` 与 DX12 Queue 实际提交分离；Fence 不能在添加 Pass 的 lambda 内假定提交完成。

## Known Follow-ups

M4 追加 First-Hit Guides 和优化；以后 NRD 按 Diffuse/Specular 重新规划 AOV，而不是把 OIDN 的 GPU interop 用在 NRD 上。

## Outcomes

未执行。

## Resume Protocol

从 `progress.md` 检查 M2 的 DX12 SharedMemory/Fence/Filter 实测通过后开始。每个阶段内子项逐条录视频/日志/截图，所有 Acceptance Criteria 通过才变更当前里程碑。

## Implementation evidence (2026-10-14)

用户要求实施 M3，GPU-only plumbing 在 RTX 5070 上完成 100 次真实 RG/CUDA/RG 发布。操作与证据见 m3-acceptance.md / progress.md；上方全场景 Acceptance Criteria 不因小尺寸 smoke 通过而标为 Pass。
