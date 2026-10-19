# AiluEngine — OIDN GPU-only PathTracing Denoiser ExecPlan

> 源码基线：`AiluEngine_Source(20261013-131234).zip`，源码静态审阅；文档创建：2026-10-13。计划基线；M1～M3 当前已实现，状态与证据见 progress.md。
> 依赖目标：Intel Open Image Denoise **2.5.1**，Windows x64 + DirectX 12 + NVIDIA CUDA GPU（首版 NVIDIA Turing 或更新架构，例如 RTX 系列）。
> 执行入口：先读本文件和 `progress.md`，**只加载当前 Milestone 文件**。

## Purpose

以最小侵入方式为 AiluEngine 现有 DX12 PathTracer 的 **Reference（静态渐进累积）**增加 OIDN **GPU-only** HDR 预览。
降噪输入和输出保持在 GPU 显存，使用 DX12 Shared Buffer + OIDN CUDA 和跨 API Fence；不创建 GPU→CPU→GPU 读回链路。
同时建立最小 `IPathTracingDenoiser` 抽象，为后续 NRD REBLUR/RELAX、SVGF、其他 GPU 降噪器保留 RenderGraph 接口。

## Scope

- 一个独立的 `EPathTracingDenoiser` 设置（首版 `kNone`、`kOidnGpu`）和可插拔后端宿主；与原有 `EPathTracerAccumulationMode` 完全正交。
- 只在 `kReference` 有效，输入为现有 **HistoryCurr 累积的线性 HDR radiance**。有更新结果时替换显示源，但绝不写回 Reference History。
- OIDN `RT` filter，MVP **Color-only + hdr=true**；Quality `Balanced` 默认，可选 `Fast`/`High`；低频按 SPP 触发。
- 专用的 **GPU Shareable Buffer** 能力、Pack/Unpack Compute Pass、外部 CUDA GPU 同步协议、异步提交与安全发布。
- 设备与能力检查：LUID/UUID/PCI + external memory/semaphore type；**不支持时显示原图且记录原因**。
- 默认只允许一个未完成 OIDN 请求；输出发布有 generation/request ID、SPP 和尺寸校验；无 CPU 同步等待渲染关键路径。
- M4 选择性补充与光线路径第一命中严格一致的 Albedo/Normal guides 和性能优化。

## Non-goals

- **不实现、不链接、不隐藏回退到 OIDN CPU**；不使用 `ReadbackTexture`、CPU Filter、`Texture2D::SetPixelData/Apply` 上传降噪结果。
- 本轮不接入 NRD/SVGF，不做每帧低 SPP 的实时运动降噪，不添加 DX11/Vulkan 支持。
- 首个 MVP 只支持 **Windows DX12 + NVIDIA CUDA**；AMD HIP/Intel SYCL 的 GPU 接入仅列为后续支持项，绝不可冒充已支持。
- 不修改 PathTracer BRDF/NEE/MIS、DXR 或 Compute 射线求交、不替换 `raytrace_gi.hlsl` 中现有 `Denoise` 历史累积 Kernel。
- 不将 CUDA/OIDN 原生类型泄漏到 RenderGraph、`RayTraceGI.h` 和渲染器通用 API。
- 不为降噪需求重构全部 RenderGraph/GraphicsContext 或引入大而全的第三方 SDK 框架。

## Architecture Constraints

### 当前源码已确认的事实

1. `Engine/Src/Render/Features/RayTraceGI.cpp:403-471` 计算 `reset_history`（摄像机/场景签名/分辨率变化等），可复用生成降噪任务代数。
2. `RayTraceGI.cpp:706-765` 的 `Denoise` 是 **Temporal/Reference History 累积**；`_history_curr_handle` 才是 OIDN 静态预览的输入，不是 `_raw_path_trace_handle`。
3. `RayTraceGI.cpp:767-790` 在 `Debug GI` Blit 最后选择显示目标，可在这里局部插入 `PathTracingDenoiserHost::Record` 与输出选择；`_debug_temporal_target` 1～5 必须保持原样。
4. `RayTraceGI.cpp:822-870` 创建的 Raw/HistoryA/HistoryB/TemporalDebug 都是 `R32G32B32A32_FLOAT`；GPU Buffer 使用像素步长 16B，MVP 不引入 HDR→LDR 转换。
5. `Engine/Src/RHI/DX12/D3DBuffer.cpp:120-155` 使用 `CreateCommittedResource(... D3D12_HEAP_FLAG_NONE ...)`；**目前没有共享分配选项**。应在最小限定范围内新增 shareable buffer 路径，而不是让所有 GPUBuffer 都变为 Shared。
6. `Engine/Src/RHI/DX12/D3DContext.cpp:1149-1228` 的 `ExecuteRHICommandBuffers` 对所有命令列表集中 `ExecuteCommandLists`，并在 `_command_submit_mtx` 内由 `m_commandQueue->Signal` 发出内部 Fence，之后运行 PostSubmitCallbacks。**普通 RG Pass 执行时不等于实际 GPU 已提交**；外部共享 Fence 必须严格关联真实队列提交边界。
7. `Engine/Src/Render/RenderGraph/RenderGraph.cpp:782-~980` 的 Pass 调度/资源状态编译只了解 DX12 图内资源；**OIDN CUDA 工作不在 RG 依赖图内**，必须通过专用 interop 作业状态和 Fence 显式对接，不能假设 `builder.Read/Write` 会自动解决 CUDA 同步。
8. `Engine/Inc/Render/Buffer.h:112+` 的 `BufferDesc` 没有 `_shareable` 开关；可添加后端特定创建选项或新增专用 `D3DSharedGpuBuffer`，以最小且可证明正确的修改为准。不要盲用通用 `NativeResource()` 的底层 COM 生命周期。
9. `Engine/Inc/RHI/DX12/D3DContext.h` 存在命令提交互斥与设备访问；当前 DX12 创建/提交/KeepAlive/队列状态跟踪应继续由 DX12 RHI 所有者管理。
10. 压缩包未包含实际完整 ThirdParty/项目构建入口（sln/vcxproj/CMake等）；必须在工作区查实 OIDN 2.5.1 DLL、构建与部署位置，不得假称编译成功。

### 官方能力与边界（OIDN 2.5.1）

- 必须查询 `externalMemoryTypes` 和 `externalSemaphoreTypes`。CUDA（Windows）支持导入外部信号量；不保证每张 GPU/驱动都支持所选 Handle。
- `oidnNewSharedBufferFromWin32Handle()` **只能导入可共享的线性 Buffer**；普通 DX12 Texture 不能直接当成该 API 的输入。使用 GPU Pack/Unpack。
- DX12 **Committed Resource** 采用对应 `D3D12_RESOURCE`/`OPAQUE_WIN32` 类型；选择并验证 `DEDICATED` 修饰标志。实际 Flag 与 Handle 必须与被导出资源的类型严格匹配，不能随意混用 `D3D12_HEAP` 和 `D3D12_RESOURCE`。
- 使用 `oidnNewSharedSemaphoreFromWin32Handle(...D3D12_FENCE...)`，`oidnWaitSemaphoresAsync`/`oidnSignalSemaphoresAsync`，**必须保证在同一物理 GPU**；不依赖 CPU `oidnSyncDevice()` 当作实时运行的默认同步。
- `oidnExecuteFilterAsync` + 外部 Semaphore 的**提交顺序和完成顺序**均要可证明，Fence 值单调递增；GPU output 未完成之前不得读、重用或释放。
- 若缺少共享 Buffer 或共享 Fence，MVP **不以 CPU 拷贝或 CPU 阻塞代替**，直接拒绝启动 GPU 后端，`kNone` 原图回退。
- GTX 1050 / Pascal 不符合当前 OIDN CUDA GPU 最低架构；不能在其上验收该 GPU-only 后端。

官方依据：
- https://www.openimagedenoise.org/documentation.html （物理设备、GPU interop、external buffers/semaphores、filter API）
- https://www.openimagedenoise.org/downloads.html （2.5.1 Windows x64 包）
- https://github.com/RenderKit/oidn/blob/v2.5.1/include/OpenImageDenoise/oidn.h （版本锁定的 API 声明，实施时核对）

### 架构边界

```text
RayTraceGI / GIPass
  ├─ PathTracer Raw (Compute / DXR，现有)
  ├─ Temporal Reference HistoryCurr (现有，永不被 denoiser 写回)
  └─ PathTracingDenoiserHost (新增，小而稳定)
       ├─ IPathTracingDenoiser::Record / Reset / Shutdown
       └─ OidnGpuDenoiser (新增；CUDA / OIDN 代码仅在私有模块)
            ├─ RG: PackColorToSharedBuffer (DX12 Compute)
            ├─ DX12: Queue submit + Exportable Fence signal
            ├─ OIDN CUDA: GPU wait → RT filter async → GPU signal
            ├─ CPU: Fence 完成状态非阻塞查询（绝不 Map HDR）
            └─ RG: UnpackSharedBufferToPreviewTexture (DX12 Compute)
                     └─ Debug GI: 用当前有效的 preview 或现有 History Blit
```

建议新模块：

```text
Engine/Inc/Render/Denoiser/PathTracingDenoiser.h
Engine/Src/Render/Denoiser/PathTracingDenoiser.cpp
Engine/Src/Render/Denoiser/OIDN/OidnGpuDenoiser.cpp
Engine/Src/RHI/DX12/OidnGpuInteropD3D12.cpp                 # 或更中性的 ExternalGpuInteropD3D12.cpp
Engine/Inc/RHI/DX12/OidnGpuInteropD3D12.h                  # 仅 private / 后端可见，按工程组织调整
Engine/Res/Shaders/hlsl/ray_trace/oidn_pack.hlsl           # pack + unpack compute
```

### 最小后端契约（示意，不逐字照抄）

```cpp
enum class EPathTracingDenoiser : u8 { kNone, kOidnGpu };

enum class EDenoiserStatus : u8 { kDisabled, kUnsupported, kInitializing, kIdle, kSubmitted, kReady, kFailed };

struct PathTracingDenoiserInput
{
    RDG::RGHandle _radiance{};                 // 当前 Reference 线性 HDR 累积图
    RDG::RGHandle _albedo{};                   // 可选，后续 guides
    RDG::RGHandle _normal{};                   // 可选，后续 guides
    RDG::RGHandle _view_z{};                   // 未来实时后端
    RDG::RGHandle _motion_vector{};            // 未来实时后端
    u64 _generation = 0u;
    u32 _width = 0u;
    u32 _height = 0u;
    u32 _sample_count = 0u;
    bool _valid_radiance = false;
};

struct PathTracingDenoiserOutput
{
    RDG::RGHandle _display_texture{};
    u64 _generation = 0u;
    u32 _source_sample_count = 0u;
    bool _valid = false;
};

class IPathTracingDenoiser
{
public:
    virtual ~IPathTracingDenoiser() = default;
    virtual void Reset(u64 generation) = 0;
    virtual PathTracingDenoiserOutput Record(RDG::RenderGraph &graph, const PathTracingDenoiserInput &input) = 0;
    virtual void Shutdown() = 0;
};
```

`Record()` 的语义：**在 RG 中注册需要的 GPU 工作并返回当前已完成可显示纹理**，不承诺本次 OIDN 滤波在当帧可见；允许零或一个 in-flight 请求。
CPU OIDN 的所有概念（Readback、CPU Pixel、Map、JobSystem Filter）都不应成为该接口的必需部分。

### 推荐 GPU 作业状态

```text
Unsupported ──(GPU 不支持/缺少 Handle 类型)──────────────→ 原始 Reference 显示
Idle ──(SPP 阈值且空闲)───────────────────────────────→ PackRecorded
PackRecorded ──(真正 GPU submit 后已 signal ready fence)─→ FilterQueued
FilterQueued ──(OIDN GPU wait/filter/signal 已排入队列)──→ Filtering
Filtering ──(nonblocking CompletedValue 达目标值)──────→ UnpackRecorded
UnpackRecorded ──(DX12 Unpack 已提交/完成，纹理安全)────→ Ready
Ready ──(Unpack 已消费且 slot 回收、下一次 SPP 触发)───→ PackRecorded
Any ──(generation 改变/Resize/Dispose)────────────────→ Retiring→Idle/Disabled
```

同步默认策略：**异步多帧发布**，在 CPU 非阻塞 `GetCompletedValue()` 判断 OIDN done 后，才录制/提交 RG Unpack，避免每次在 DX12 Queue 上先排入 `Wait` 使画面无意停顿。若同代存在旧 Ready Texture，处理新任务时继续显示旧版本；无旧结果则显示当前 Reference。
所有阶段实际物理 Fence / UAV barrier / state transition 由 DX12 RHI 管理。不能对未完成的 OIDN output 直接进行 RG 采样。

## Baseline

- 原始 PathTracer 具有 `kReference/kRealtime/kDisabled`、`raw_executed` 有效帧门控、Temporal History 双纹理。
- GPUBuffer 目前采用默认 DX12 committed resource，尚无共享创建能力；专用 OIDN Interop 需要小规模 DX12 RHI 扩展。
- `D3DContext` 有队列提交互斥、Fence 提交回调和资源 KeepAlive，可成为外部 signal 的实际提交时机。
- 现有 `GBuffer0/1` 可作为未来 guides 的候选，但不能默认代表 PathTracer 的真实第一命中（特别是透明/折射）。
- 实际 Build/ThirdParty 未提供；目前 **仅静态审阅，没有任何完成的编译、设备联调或画质验证**。

## Target Outcome

- 支持的 NVIDIA Turing+ 设备：Reference 4/8/16/32 SPP 达到非阻塞 GPU OIDN 预览，全程无 HDR CPU 读回/上传；输出保持线性 HDR。
- `kNone` 或不支持设备：保持原有 PathTracer/History/Debug 行为，且没有共享 Buffer、Fence、CUDA 初始化的附加开销。
- 运行时 Resize、相机/场景/材质变化、模式切换、退出时不会使用失效资源或错代显示。
- 以后新接 NRD/SVGF 可复用 `Record`、Input/Output、Host、Debug GI 的显示插入点，不必修改现有追踪着色器。

## Relevant Files

- `Engine/Inc/Render/Features/RayTraceGI.h`，`Engine/Src/Render/Features/RayTraceGI.cpp`
- `Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl`（只验证 Reference，首轮不修改）
- `Engine/Inc/Render/Buffer.h`，`Engine/Src/RHI/DX12/D3DBuffer.cpp`，`Engine/Inc/RHI/DX12/D3DBuffer.h`
- `Engine/Inc/RHI/DX12/D3DContext.h`，`Engine/Src/RHI/DX12/D3DContext.cpp`
- `Engine/Inc/Render/CommandBuffer.h`，`Engine/Src/Render/CommandBuffer.cpp`，`Engine/Inc/Render/GfxCommand.h`
- `Engine/Inc/Render/RenderGraph/RenderGraph.h`，`Engine/Src/Render/RenderGraph/RenderGraph.cpp`
- `Engine/Inc/Render/Texture.h`，`Engine/Src/RHI/DX12/D3DTexture.cpp`
- 构建配置、第三方二进制部署和自动反射工具（实际仓库内先确认）。

## Milestones

| 阶段 | 执行文档 | 独立验收产物 | 必须满足 |
|---|---|---|---|
| M1 | `m1-denoiser-contract.md` | None 透传 + GPU-ready 抽象 + 生成失效 | 原 PathTracer/Debug 结果逐像素一致、无 SDK 依赖 |
| M2 | `m2-dx12-oidn-interop.md` | **独立 GPU Interop Spike**：DX12 Buffer / ShareHandle / Fence / OIDN CUDA Filter | 完成纯 GPU 闭环和同步/故障测试，实际支持的 NVIDIA Turing+ GPU |
| M3 | `m3-rendergraph-oidn-preview.md` | RG Pack → OIDN → RG Unpack → Preview，非阻塞发布 | 可见降噪图、无拷回、切换安全、性能合格；**MVP** |
| M4（可选） | `m4-guides-and-polish.md` | First-hit Guides、内存/性能/多场景硬化 | 量化画质收益且不引入 AOV 错位 |

**严格串行：当前 Milestone 的 Acceptance Criteria 未通过，不能进入下一个。M3 即可交付。**

## Current Milestone

M1～M3 已实现；用户于 2026-10-18 明确要求进入 M4 First-Hit Guides，当前实现和验收见
`m4-guides-and-polish.md` / `progress.md`。M3 完整场景画质、DXR→Draw #951 和正常帧性能遗留项
继续记录，不能因激活 M4 就标记通过。

## Progress

- [x] 审阅上传的 PathTracer / D3DBuffer / D3DContext / RenderGraph 源码路径。
- [x] 复核 OIDN 2.5.1 官方 Windows GPU interop / async signal / compatibility 边界。
- [x] M1 合同及回退、M2 独立 interop spike 已实现并完成构建验证。
- [x] M3 GPU preview 集成及独立 RenderGraph GPU smoke。
- [x] M4 可选 First-Hit Guides 与固定 Editor Compute/DXR 场景验证。
- [ ] 严格同 SPP/quality 画质收益、完整生命周期与正常性能验收。

## Decision Log

| 日期 | 决定 | 理由 |
|---|---|---|
| 2026-10-13 | **GPU-only**；彻底删除 CPU 读回计划 | GPU PathTracer 不应该通过 HDR CPU 回传进行预览 |
| 2026-10-13 | 首版限定 Windows DX12 + CUDA / NVIDIA Turing 或更新架构 GPU | OIDN 2.5.1 此组合具备外部 Memory + Semaphore 受支持路径 |
| 2026-10-13 | 无 GPU interop 能力时回退 **None**，不改用 CPU | 严格兑现 GPU-only 限制 |
| 2026-10-13 | 独立 GPU interop 技术验证先于 RG 集成 | 隔离显存共享/同步的最大风险 |
| 2026-10-13 | 首版 Color-only，GUIDES 延后 | 避免修改 PathTracer 第一命中输出及透明材质行为 |
| 2026-10-13 | 不在记录 Pass 时直接运行同步 OIDN | 当前引擎真正 GPU 提交发生在 D3DContext 层 |

## Surprises & Discoveries

- 原先名为 `Denoise` 的 Kernel 是现有 Reference/Realtime 历史处理，不应该替换。
- 当前 Buffer 分配始终使用 `D3D12_HEAP_FLAG_NONE`，共享导入需要新增真正的 Shared Allocation。
- DX12 内部 `_p_cmd_buffer_fence` 并不自动等价于**可导出/可被 OIDN 导入**的 Fence；必须专用共享对象或经验证的显式扩展。
- OIDN 2.5.x 可导入外部 Fence，但 CUDA/HIP/SYCL 支持不对称；不能把“OIDN 有 GPU 版”推断为“所有 GPU 都能 DX12 零回读”。

## Validation

- 静态审阅：已完成。
- ON Debug build、OFF 回归、contract/shader smoke 和 RTX 5070 GPU RG 100 次发布已通过；完整场景视觉验收待执行，见 `progress.md` / `m3-acceptance.md`。
- 实施时必须记录 DX12 Adapter LUID、OIDN device、externalMemoryTypes、externalSemaphoreTypes、OIDN 版本、驱动版本、GPU timings、RenderGraph validation 结果。
- 核心 KPI：每次 denoise 的 `pack_gpu_ms`、`oidn_gpu_ms`（若 SDK 无精确时间戳则记录可信的端到端延迟而非伪 GPU ms）、`unpack_gpu_ms`、frames delayed、GPU shared memory bytes、CPU submit cost、最大未完成请求数。

## Known Follow-ups

- 支持 AMD HIP 的独立认证与 DX12 interop；Intel SYCL 视 OIDN 外部 Semaphore 版本能力再立项。
- NRD/RELAX/SVGF 对 Diffuse/Specular、viewZ、motion vector、hit distance 的需求，在独立 ExecPlan 中规划。
- 多 Viewport：`RayTraceGI.cpp` 仍有静态前帧状态，现阶段不扩成多实例通用重构；后端状态严禁再引入同类 static。

## Outcomes

M1～M4 guides 主链路已实现。M4 完整 Debug、合成 GPU 12+100 次发布、固定 Editor Compute/DXR
18 个 capture 和 AOV/图像检查通过；Color-only 保持默认。严格同 SPP/quality 的收益、完整生命周期与
正常帧性能验收仍 pending，详见 `m4-guides-and-polish.md`。

## Resume Protocol

1. 阅读 `README.md`、`progress.md`，然后仅阅读 `progress.md -> Current Milestone` 指向的阶段文件。
2. 实际代码可能较 zip 更新：先以仓库最新代码定位符号，修正路径差异、记录 Decision Log，再实施。
3. 每个 Milestone 必须留下编译命令、运行环境、验证录像/截图/PIX 或日志的明确证据；没证据不能标记 Pass。
4. 当前阶段全通过后才在 `progress.md` 修改 Milestone 状态与 Next Step；失败则只修当前阶段。
5. Context compaction 前更新 `progress.md` 的 Current Milestone、已变更文件、Blockers、未通过测试、下一个具体动作。

## M3 implementation update (2026-10-14)

当前 M3 已实现并在 RTX 5070 上通过 100 次真实 GPU RG plumbing smoke；完整 Editor 场景视觉/性能验收待执行，M4 未实施。当前事实优先以 progress.md、m3-acceptance.md 和源码为准，上文设计阶段的 Unsupported/M3 未实现描述是历史基线。
