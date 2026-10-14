# OIDN GPU-only — Live Progress

## Purpose / Scope

完成 M1 合同和 M2 独立 DX12/CUDA interop 实现；用户自行执行视觉及硬件验收。M3 已实现并通过 GPU plumbing smoke；M4 未实施。

## Architecture Constraints

- GPU-only；不创建 CPU OIDN device，不读回或上传 HDR。
- 生产 GIPass 已接入可选 OIDN GPU preview；OFF/不支持时原图回退，完整场景视觉验收待执行。
- M2 使用专用共享 committed buffers、两个共享 timeline fences 和独占测试队列，不修改通用 Buffer 分配。
- 用户于 2026-10-13 明确要求同时实现 M1～M2、之后自行验收。因此允许交付两个阶段的实现；未执行的 acceptance 不标记 Pass。

## Milestones / Current Milestone

| 阶段 | 当前状态 | 证据 | 下一步 |
|---|---|---|---|
| M1 | Implemented; acceptance pending | Debug build / contract + synthetic RG compile smoke | 用户验证 None/Unsupported 显示、Debug1～5、Resize/Reload |
| M2 | Implemented; GPU acceptance pending | OFF build / ON standalone compile | 用户在 Turing+ CUDA GPU 上执行 spike / PIX |
| M3 | Implemented; visual acceptance pending | ON Debug / shader compile / RTX 5070 GPU RG 100 次通过 | 用户场景/性能验收 |
| M4 | Deferred | 无 | 另行决定 |

## Progress

- [x] 核对实际工作区 CMake、AiluHeadTool、GIPass/RG 与 DX12 提交流程。
- [x] GPU Resource contract / Host / reflected denoiser setting / per-instance generation。
- [x] Debug GI setup/execute 捕获同一显示句柄，保留 Debug1～5 选择顺序。
- [x] None/Unsupported 无新增 GPU pass；None 默认不初始化 SDK/interop 对象。
- [x] 合成 HDR RG producer/consumer 编译测试，拒绝旧 generation、instance、size、future SPP。
- [x] OIDN 2.5.1 可选动态加载；同 LUID/nodeMask CUDA-only 设备检查。
- [x] 专用 DEFAULT/SHARED/UAV committed input/output，独立 NT handles 和 imported buffers。
- [x] Pack 真正提交 → ready signal → OIDN wait/filter/signal → done 非阻塞查询 → consume → slot 回收。
- [x] 独立窗口 GPU 显示 noisy/filtered；另保留线性 RGBA32F HDR result。
- [x] `--shared-only` 分配/共享/释放测试；`--disabled` 无设备的 OFF 回退测试。
- [x] 官方 Windows runtime 安装脚本，固定 release SHA256；依赖已部署，并保留 ON 独立程序副本。
- [ ] 用户执行 GPU-only 滤波、PIX/debug layer、设备/故障/关闭/尺寸验收。

## Relevant Files

- `Engine/Inc/Render/Denoiser/PathTracingDenoiser.h` / generated enum files
- `Engine/Src/Render/Denoiser/PathTracingDenoiser.cpp`
- `Engine/Inc/Render/Features/RayTraceGI.h` / generated files / `Engine/Src/Render/Features/RayTraceGI.cpp`
- `Engine/Src/RHI/DX12/OidnGpuInteropD3D12.h/.cpp` (private)
- `Test/PathTracingDenoiserSmoke.cpp`, `Test/OidnGpuInteropSpike.cpp`, `Test/InstallOidnGpuRuntime.ps1`
- Root / Engine / Test CMakeLists; `acceptance.md`

## Decision Log

- 2026-10-13: 用户要求实现 M1～M2，GPU/视觉验收留给用户，不以编译证明 GPU interop 成功。
- 动态 C ABI 最小子集按 OIDN 2.5.1 官方 oidn.h 核对，避免 SDK/import library 对 OFF 配置的依赖；所有原生类型留在 private DX12 实现。
- M2 独立队列只有测试程序调用；M3 不允许直接借用本接口提交引擎共享队列。正式 hook 应位于 D3DContext `_command_submit_mtx` 内，Pack 所属 ExecuteCommandLists 后 Signal，再交给异步 OIDN owner。
- DEFAULT committed resource 使用 D3D12_RESOURCE | DEDICATED；不尝试偷换 D3D12_HEAP/OPAQUE_WIN32。
- 正常请求仅轮询 Fence；同步只用于独立程序关闭/重建。M3 disable/resize 必须异步 retirement，不能在正常 frame 调用这里的 Shutdown。
- 没有 UUID/PCI fallback：LUID/nodeMask 不能证明同卡即 Unsupported，首版只支持 single-node。

## Surprises & Discoveries

- 默认 PATH 无 CMake；使用 VS2026 自带 CMake。Debug 输出 F:/AiluEngine，需要工作区外构建写权限。
- AiluHeadTool 不支持单行反射枚举，已展开定义并通过正常工具生成。
- RG id=0 保留为 invalid，独立 synthetic smoke 需先创建占位 frame resource。
- 原积分器与 raytrace_gi.hlsl 未修改；SPP 元数据来自成功录制的 Reference 累积，不代表外部 GPU 已完成。M3 必须绑定实际提交和 fence，不得用这项计数推断 GPU completion。

## Validation

- `cmake --build --preset build-debug` (AILU_WITH_OIDN_GPU=OFF): 已通过；最终增量复核记录见 acceptance.md。
- `cmake --preset debug -DAILU_WITH_OIDN_GPU=ON` + `cmake --build --preset build-debug --target OidnGpuInteropSpike`: 开启分支编译通过；M3 真实 GPU 滤波证据见下文。
- `PathTracingDenoiserSmoke.exe`: passed，合约/RG compile；没有 GPU 执行及视觉证明。
- `OidnGpuInteropSpike.exe --disabled`: passed，OFF 无设备初始化回退。
- `OidnGpuInteropSpike.exe --shader-only`: passed，Pack/Unpack HLSL 离线编译，无 GPU 初始化。
- 初次生成枚举格式错误、早期合成测试 id=0 导致拒绝，均已修复并复测。
- GPU 型号/驱动/LUID/能力与小尺寸端到端 latency 已记录于 m3-acceptance.md；PIX/完整场景视觉结果待用户执行。

## Blockers / Next Step

没有已知实现阻塞。实际硬件与画面行为尚未验收；依 `acceptance.md` 操作并记录结果。
M2 初始化如果缺 DLL、CUDA 同卡设备、external types 或 import 失败，返回明确 Unsupported/Failed，不回退 CPU。

## Outcomes / Resume Protocol

用户于本轮明确要求实施 M3，当前 M3。已接入真实 signal hook、后台 session、RG Pack/Unpack、双 preview、SPP/quality 设置和只读诊断；隔离 GPU smoke 已通过。M2 滤波与 M3 视觉 acceptance 尚未标记 Pass。
## M3 update (2026-10-14)

已实现真实 submit Signal/已完成 Wait、后台 Session init/filter/retirement、共享 Buffer 的稳定 GPU registry handles、GPU Pack/Unpack、双 HDR preview、SPP 节流、quality 设置、只读诊断快照、activation revision、generation invalidation。
RenderGraph EndFrame 清空 executor captures，debug snapshot 不持有 live callback；可选 RenderPipeline 的受控清理避免独立 GPU 模式空指针。
RTX 5070 63x33/127x65、Balanced/High、10 次启停下 100 次有效发布、正常退出，通过；证据与边界见 m3-acceptance.md。
最终 OFF 回归和 ON 完整 Debug 构建已通过；完整 Editor Compute/DXR 场景视觉验收仍 pending。没有修改积分器或 raytrace_gi.hlsl。

最终复核：ON 完整 Debug build、OFF Engine/contract/spike build 与 disabled smoke、ON contract smoke、三个 M3 shader entrypoint 编译均通过。最终 GPU 复测目录 OidnRG_20261014_09，Ready=100/request=110/source SPP=92，正常退出，末次端到端 latency 24.0312ms；无 D3D12 error。Debug cache 保持 ON，默认 denoiser 仍为 None。git diff --check 通过。

## M3 runtime warning fix (2026-10-14)

用户已确认开启降噪有效果，但实际 Editor 报 D3D12 #951 PIPELINE_STATE_TYPE_MISMATCH，DrawIndexedInstanced 绑定着 Raytracing PSO。源码确认 DispatchRays 直接调用 native SetPipelineState1，未使 graphics PSO 缓存失效，后续相同 PSO 的 Draw 会跳过重绑。已新增 D3DCommandBuffer::SetRayTracingPipelineState 并替换 DXR 调用，行为与现有 Compute PSO 缓存失效一致；审计只剩该 wrapper 调用 SetPipelineState1。完整 build-debug 和 contract smoke 通过，diff --check 通过。尚未在用户当前场景复测 #951 消失；此前合成 GPU RG smoke 不覆盖 DXR→Draw 切换，不把它当作这次 runtime 修复的证据。
