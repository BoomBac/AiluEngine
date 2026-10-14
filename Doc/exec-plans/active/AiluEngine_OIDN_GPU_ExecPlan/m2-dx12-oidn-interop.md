# M2 — 独立 DX12 / OIDN CUDA GPU Interop 技术验证

## Purpose

先在**不改 PathTracer 和完整 RenderGraph**的最小测试中证明：DX12 Buffer 可共享给同卡 OIDN GPU、跨 API GPU Fence 顺序可靠，滤波输入输出均留在显存。M2 是风险最高的技术 Gate。

## Scope

- 对 OIDN 2.5.1 `NVIDIA Turing+ GPU / CUDA` 做运行时设备枚举 + LUID/UUID/PCI 匹配，禁止默认选 CPU/其他物理 GPU。
- 增加专用 DX12 Shared Buffer 创建/回收路径，验证 `CreateCommittedResource` + `CreateSharedHandle` + OIDN Import。一个 Input Buffer + 一个 Output Buffer，均为 GPU resident RGBA32F。
- 新建两个可导出共享 Fence（推荐 `dx_ready_fence` 和 `oidn_done_fence`），验证设备支持外部 D3D12 Fence/NT Handle、timeline values、跨 API 传递。
- 最小自测：DX12 Compute 写入已知 HDR → DX12 GPU Fence Signal → OIDN GPU Wait → `RT` Filter Async → OIDN GPU Fence Signal → DX12 Wait/完成查询 → DX12 GPU 读取并写回测试 RT。
- 用 RenderDoc/PIX 或 DX12 GPU 输出校验（不是 CPU Readback）检查颜色变化/画面正确性；可以利用编辑器中已有 Texture 显示路径或一张纯 GPU 校验纹理，**不依赖 ImageCapture**。
- OIDN device/filter 仅创建一次，固定 resolution 的测试复用，配置更改才重建。

## Non-goals

- 不改全局 DX12 Buffer 分配策略、不提供 CPU readback fallback、不适配 Intel/AMD、不将 OIDN 直接放入 RayTraceGI。
- 不把性能测量等同于 SDK 中 `oidnExecuteFilterAsync` 线程返回耗时。

## Architecture Constraints

### 资源与格式

- 推荐对 `D3DBuffer` 增加严格限定的 shared allocation 选项，**仅 DX12 默认堆 committed resource** 可被创建为 shareable；不允许自动扩大到常规动态/上传/Readback/Counter Buffer。
- 可选择 `BufferDesc::_is_cross_api_shareable`（显式 bool）或专用 `D3DSharedGpuBuffer`。优先先写隔离样例，在证明需求后再决定通用抽象，不污染默认缓冲创建性能。
- 对 `DX12 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` + `D3D12_HEAP_FLAG_SHARED` 的组合检查实际设备返回。共享使用 `ID3D12Device::CreateSharedHandle` 的 NT HANDLE，权限和 Handle 生命周期独立跟踪。
- 对 OIDN `externalMemoryTypes` 同时验证 `D3D12_RESOURCE` / `OPAQUE_WIN32` 及 `DEDICATED` 修饰，与实际导出对象一致；处理导入失败而不偷换 `D3D12_HEAP` 类型。
- **以 OIDN filter 的实际图像语义验证 `Float3`/`Float4` stride。** MVP 用 RGBA32F 16-byte pixel stride，读取 RGB；输出仍采用 16-byte pixel stride 并由 GPU Unpack 设置 alpha=1。尾部像素和奇数宽度必须测试。
- Shared Buffer 容量：`u64 width*height*16` 计算含溢出检查；若采用 RDG GPUBuffer 当前 `_size` 为 `u32` 的约束，需限制尺寸并在错误信息中明确提示。
- 外部访问资源的 D3D12 state 须以 `COMMON`/明确的 handoff state 进行验证；包围 Pack/Unpack 的 UAV Barrier 和 state transition 由 DX12 RHI 跟踪，不能让 RG 仍误认为 buffer 处于 UAV 状态。

### 设备与运行时

- 枚举 `oidnGetNumPhysicalDevices` 并按 `luid`（含 nodeMask）选择与 `IDXGIAdapter` 一致的设备；不可盲选 `oidnNewDevice(OIDN_DEVICE_TYPE_DEFAULT)`。
- LUID 缺失/不一致时可显式评估 UUID/PCI 验证同一物理设备，无法证明匹配则返回 Unsupported。
- `oidnGetDeviceUInt(device, "externalMemoryTypes")` 与 `oidnGetDeviceUInt(device,"externalSemaphoreTypes")` 必须包含当前采用的 flag。
- 仅接受 `OIDN_DEVICE_TYPE_CUDA`，拒绝 CPU fallback；初始化超时/异常时回退 `kNone`，而不是在渲染主线程重试。
- OIDN Windows DLL（包括 CUDA backend 及其运行时依赖）必须明确部署；建议可选独立 adapter DLL 或经验证的延迟加载，缺库时不崩进程。
- CUDA/OIDN 模块卸载前确保 GPU work 完成；不能在资源引用存活期间先释放 DLL、device、HANDLE、fence 或 DX12 resource。

### 同步协议（单次 request）

```text
DX12 GPU pack(input_buffer, historyHDR)
DX12 transition / UAV barrier / release 到 external handoff state
DX12 Queue Signal(dx_ready_fence, value_a)    <-- 与 pack 命令列表同一序列且提交有严格先后
OIDN WaitSemaphoresAsync(dx_ready, value_a)
OIDN ExecuteFilterAsync(filter)
OIDN SignalSemaphoresAsync(oidn_done, value_b)
DX12 不阻塞 CPU，查询 oidn_done_fence.GetCompletedValue() >= value_b
下一次可提交的 DX12 Unpack pass（必要时在 Queue 上 Wait 已完成的 fence，绝不提前消费）
DX12 Queue 提交/完成后，可重用共享 buffer / 将 GPU Preview Texture 标记 Ready
```

- 同步/等待与滤波必须严格以 **同一个 OIDN device/stream 的提交顺序**成立；在 M2 首先打印 API 错误并验证顺序。
- 独立 Fence value 分配器由 RHI 同步、单调增长。禁止同一 Fence value 重用、禁止 host Signal 冒充滤波完成。
- 应证明 `dx_ready` **一定在 Pack 所属 CommandList 真正提交后**才 signal；不得仅在 `RenderGraph::Execute()` 的 CPU pass lambda 里 signal。
- `oidn_done` Signal 只有在 OIDN filter GPU work 之后被排队，CPU 不等滤波完成。
- 如果无法导入 external semaphore：**Unsupported + kNone**（严格 GPU-only、非阻塞目标），不要启用 `oidnSyncDevice`/主线程 EventWait 作为默认通路。

### 专用共享资源生命周期

```text
Created → DX12 Ready → ExportedHandle → OIDN Imported → Filtering → Done → Unpack/Consumed → Retired
```

- `CloseHandle` 与 `OIDNBufferRef`/`OIDNSemaphoreRef` 必须各自释放；Win32 handle 导入后所有权仍属调用方。
- 未完成 Fence 对应的资源必须由独立 keep-alive 记录持有；仅靠共享原始指针或当前帧 RG Lease 不够。设备或 Filter 的最终销毁在安全关闭阶段须按官方 `oidnSyncDevice` 要求保证 OIDN 工作已完成；严禁把同步等待放进正常逐帧预览路径。
- resize/device lost/disable 时标记旧 Job `retiring`，停止新请求，不等待渲染主线程；实际资源由 Fence 确认安全后释放。进程关闭阶段可在非交互销毁时完成受控同步和释放。

## Baseline

- `D3DBuffer.cpp:120-155` 当前 committed resource 缺 `D3D12_HEAP_FLAG_SHARED`。
- `D3DContext.cpp:1149-1228` 对 command list 执行、queue signal 和 post-submit callback 的锁/顺序有现成管理；外部 signal 需要沿用正确边界。
- `D3DContext.h` 提供 `GetDevice()`、锁保护的 `SetExternalState()`，但尚无明确的外部 Fence 业务 API。
- 源码当前未出现 `CreateSharedHandle`/`OIDN` 实现；GPU interop 是新能力，不能重用旧 ImageCapture 的 Readback 服务当作“共享”。

## Target Outcome

可以在适配的 NVIDIA Turing+ GPU 上独立展示 DX12→OIDN GPU→DX12 的一张 HDR 降噪测试图，输入输出均为 device-local Buffer，且无 CPU `Map`、Readback Heap、系统内存 HDR 拷贝。

## Relevant Files

- `Engine/Src/RHI/DX12/D3DBuffer.cpp` / `Engine/Inc/RHI/DX12/D3DBuffer.h`
- `Engine/Src/RHI/DX12/D3DContext.cpp` / `Engine/Inc/RHI/DX12/D3DContext.h`
- `Engine/Inc/Render/Buffer.h`（仅确有必要时加共享 flag）
- 新建 `Engine/Src/RHI/DX12/OidnGpuInteropD3D12.cpp` / private header
- 外部 SDK 版本锁定配置，DX12 独立测试程序/可选 Editor debug command。

## Implementation

1. 验证 DX12 Adapter / OIDN Physical Device 均指向相同 GPU。记录所支持的 external memory/semaphore type bitfield，失败直接 Unsupported。
2. 在无 OIDN 的最简 CUDA/DX12 测试前，先只做 DX12 Shared Resource + Shared Fence 创建/释放的单元测试，检查正确大小、资源状态与 Handle 清理。
3. 验证一对 HDR RGBA32F 共享 GPU Buffer，DX12 pack test pattern：包含 >1 的 HDR 亮度、非 1 alpha、非 256 对齐宽度；负值只用于导入前的纯 Buffer 内存一致性测试，不作为 OIDN RT Filter 的合法颜色输入，确保多行正确。
4. OIDN 创建设备和 Filter；`RT`, `hdr=true`, `quality=balanced`, Image Float3 with interleaved `pixelStride=16`, `rowStride=width*16`；对 output 独立分配共享缓冲，必要时重建 Filter。
5. 验证 DX12 queue signal / OIDN wait / filter async / OIDN signal / DX12 consume；故意延迟工作和快速提交，证明不使用旧数据。
6. 测试用 DX12 shader 读取输出 Buffer 写入 RGBA32F Texture 并在 GPU 直接显示；不允许调用 ReadBack 或编码成磁盘图像作为功能链路。
7. 加入故障测试：无 SDK/无 CUDA/GTX1050/不匹配 LUID/外部 Handle 类型缺失/Import 失败/重建分辨率/快速关闭。
8. 根据验证结果确定用于 M3 的 RHI 对外提交 Hook 的精确位置，形成可供下阶段直接实现的 private 接口与同步拓扑。

## Milestones

本文件 M2 完成后才允许 M3。缺兼容 GPU/驱动则标记 Blocked，**不能以编译通过代替验收**。

## Current Milestone

M2（待 M1 通过后开始）。

## Progress

- [x] CUDA OIDN 设备识别/能力检查实现；实际能力矩阵待用户记录。
- [x] Shareable DX12 HDR Buffers + native handles 实现。
- [x] Shared Fence + GPU wait/signal 协议实现；可靠性实测待验收。
- [x] OIDN HDR filter GPU round-trip 独立程序实现；实际运行待验收。
- [x] 生命周期/资源状态、不可支持设备回退实现；设备验证待验收。

## Validation / Acceptance Criteria

- [ ] 对受支持的 NVIDIA Turing+ GPU，真实 GPU-only OIDN `RT` 滤波结果可以经 DX12 GPU 直接显示，单次和连续 N 次均正确。
- [ ] **没有**调用 `ID3D12Resource::Map` HDR、`ReadbackTexture`、`oidnReadBuffer` / `oidnWriteBuffer`；PIX/运行时日志确认 GPU→CPU HDR bytes=0。
- [ ] `dx_ready_fence` 与 `oidn_done_fence` 完成值单调；故意延时滤波时 DX12 不读到半完成结果。
- [ ] `GPU scratch memory` 受控，1023×513/1920×1080/改变分辨率时无 stride 错误、无越界、无明显颜色变换。
- [ ] 缺 DLL、不支持 GPU、外部类型不匹配、外部导入失败时明确日志并安全返回 `kNone`；不调用 CPU 设备。
- [ ] RenderDoc/PIX/DX12 debug layer 无新增非法状态或未释放资源告警。
- [ ] 记录 GPU 型号、驱动、OIDN 版本、能力 bitfield、GPU timing 或端到端 fence latency、显存、异常测试证据。

## Decision Log

首版限定 CUDA，DX12 Shared Buffer + 共享 Fence 任一能力不可用就不启用，不做额外退化路径以防侵入范围扩张。

## Surprises & Discoveries

`D3DContext` 内的普通 Queue Fence 在未验证可导出前不能直接借用为 OIDN 外部信号量；shared allocation 与 shared semaphore 必须单独验收。

## Known Follow-ups

M3 负责把已验证的最小 interop 路径接入 PathTracer Reference/RenderGraph 和低频调度。

## Outcomes

未执行。

## Resume Protocol

从 `progress.md` 确认 M1 Pass 后进入。硬件不支持则阻塞并记录，禁止写“GPU-only 通过”而实际由 CPU 执行。

## Implementation update (2026-10-13)

代码已实现；具体文件、构建与测试证据见 progress.md。用户要求 M1/M2 一并交付、之后自行验收。上方 Acceptance Criteria 仍作为待执行的视觉/设备验收，不标记 Pass。运行方法见 acceptance.md。
