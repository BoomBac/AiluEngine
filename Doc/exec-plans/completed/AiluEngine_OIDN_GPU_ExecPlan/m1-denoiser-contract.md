# M1 — GPU-ready Denoiser 合同与 PathTracer 最小接入

## Purpose

不引入 OIDN SDK，也不改动 DX12 RHI，先确认**后端插拔、RG 显示选路、生命周期**的最小扩展点与 None 回退。

## Scope

- 添加 `EPathTracingDenoiser { kNone, kOidnGpu }`，与 `EPathTracerAccumulationMode` 正交。
- 新增 `IPathTracingDenoiser`、`PathTracingDenoiserHost`、`PathTracingDenoiserInput/Output`；GPU Resource Handle 是公共契约，不定义 CPU `ImageBuffer`。
- 在 `GIPass::OnRecordRenderGraph` 的 Reference Temporal Pass 后、`Debug GI` 前插入统一 Denoiser 调用；首版 `kOidnGpu` 返回 Unsupported/未实现，使用原图。
- 设置显示优先级：Temporal Debug target 1～5 > Denoised 可用同代图 > Reference History > Raw。
- 复用 `reset_history`，引入 generation + frame/spp +尺寸+模式+后端 instance 身份。支持后端禁用、切换及不阻塞关闭。
- 不复用历史里的 `s_prev_view_proj` 静态状态为 denoiser，多 GIPass 尽可能保持 instance-local。

## Non-goals

不引入 GPU share buffer、Fence、CUDA、OIDN DLL；不改 shader、不做“未实现但假装生效”的属性状态。

## Architecture Constraints

- `Record()` 不要求当帧结果即时有效；只许返回**已经可显示**的纹理句柄，否则 `_valid=false`。
- 不把 `OIDNDevice`、`ID3D12Resource`、`CUDA Stream` 公开到 `PathTracingDenoiser.h`。
- **RenderGraph setup 读依赖必须根据最终选中的 display RGHandle 声明。** 目前 `Debug GI` setup 读取 Raw/History 并且 execute 重新选择 handle；引入 OIDN 时须使 setup/execute 使用相同的已捕获 display 结果，避免图中依赖不匹配。
- `Debug GI` 1～5 必须保持原选择优先级，只有 `_debug_temporal_target==0` 可以选 OIDN。
- `kOidnGpu` 只有 `kReference` 才有效，不自动切模式。
- 禁用 denoiser 时不得创建 OIDN/Interop 资源和新增 GPU Pass；打开未支持后端必须 100% 安全地透传。
- 所有新类型/函数命名遵循工程现行风格；新增成员 `_lower_snake_case`，常量 `kCamelCase`。

## Baseline

- `RayTraceGI.h:13-52` 保存模式配置和反射属性，`RayTraceGI.cpp:280-292` 转发配置。
- `RayTraceGI.cpp:403-471` 构建 history reset，`:706-790` 完成 Reference Accumulation + Debug GI Blit。
- `_history_curr_handle` 是稳定的线性 HDR 数据源；`raw_executed` 阻止无效帧累计。
- `Engine/Inc/Render/Features/generated/RayTraceGI.gen.*` 由反射生成，不得手工编辑。

## Target Outcome

无 OIDN SDK 时项目能编译并运行；切换 UI 开关时可观察状态，GPU 后端尚未实现时始终按 Reference 原图输出；可用合成假后端验证真正的显示切换分支。

## Relevant Files

- `Engine/Inc/Render/Features/RayTraceGI.h`
- `Engine/Src/Render/Features/RayTraceGI.cpp`
- 新增 `Engine/Inc/Render/Denoiser/PathTracingDenoiser.h`
- 新增 `Engine/Src/Render/Denoiser/PathTracingDenoiser.cpp`
- 实际仓库构建/反射入口（请先定位）。

## Implementation

1. 确认实际工作区的编译入口、第三方库布局、源码和压缩包差异。记录编译命令与 `AILU_WITH_OIDN_GPU=0` 的约束。
2. 实现 `PathTracingDenoiserInput`、`PathTracingDenoiserOutput`、`IPathTracingDenoiser`；输入预留 optional `albedo/normal/view_z/motion_vector` 等，不要求首版提供全部 AOV。
3. 添加 Host 工厂：`kNone` 直接旁路；`kOidnGpu` 当前返回 Unsupported，**不是**创建假的已成功设备。
4. 添加 per-instance generation，以下变化必须 `++generation`：`reset_history`、resize、后端切换、禁用、场景卸载、必要的参数变化。保持 Request ID 单调。
5. 在历史 Pass 之后确定 `display_target`。`DenoiserHost::Record` 可注册 pass，`Output` 只承诺返回合法/已发布的 RGHandle。
6. `Debug GI` 原 Debug 1～5 分支不变；在 `debug==0 && output._valid && output._generation == current_generation` 时显示 denoised。
7. 保证 `graph.Import(published_texture)`/`builder.Read` 是真实依赖；不能在 lambda capture 中后改 selected resource。
8. 用一个合成 RG shader 输出（如常量颜色，编译/调试使用）验证后端返回 valid 时的图像切换；合成输出只用于测试，完成后不留默认开销。
9. 将 Host 生命周期绑定当前 GIPass。初始化与 Shutdown 均须保证在功能关闭/设备销毁时不访问过期图资源。

## Milestones

本文件仅 M1。验收通过才能进入 M2。

## Current Milestone

M1。

## Progress

- [x] 查询并记录最新工作区项目配置和反射生成入口。
- [x] Denoiser 接口与 Backend 工厂。
- [x] Host 接入 `GIPass`，None 旁路；实际逐像素回归待用户验收。
- [x] Debug GI 依赖/显示选路与 generation 失效实现；contract/RG compile smoke 已通过。

## Validation / Acceptance Criteria

- [ ] `AILU_WITH_OIDN_GPU=0` 不依赖 SDK，可编译、启动、关闭。
- [ ] `kNone` 下 Raw/Reference/Realtime/Debug1～5 与基线逐像素一致（忽略正常采样随机性，测试应固定 seed 或比较确定性图）。
- [ ] `kOidnGpu` 当前 Unsupported：没有新增 GPU 工作，不误显示旧图；开关瞬时安全回退。
- [ ] synthetic RG 后端通过 `Debug GI` 显示且正确注册被读取资源；RG validation 不报障碍/资源读写错误。
- [ ] Resize/Reset/Shader reload/后端切换时 generation 递增；合成旧输出不会被展示。
- [ ] 没有修改现有积分器和 `raytrace_gi.hlsl` 主路径。

## Decision Log

先固定稳定的 GPU-only 后端接口，并让生成器/编译在没有 OIDN SDK 时仍可完成。

## Surprises & Discoveries

`Debug GI` setup 和 execute 当前分别列资源及选择输出；未来接入 OIDN 时必须捕获单一选择结果，防止 setup/execute 分叉。

## Known Follow-ups

M2 添加 GPU DX12/CUDA Interop 的真实物理设备与 Shared Buffer 验证。

## Outcomes

实现已完成；视觉/GPU acceptance 待用户执行。

## Resume Protocol

先确认 `progress.md` 指向 M1；只执行 M1 范围，逐条记录编译/运行证据；全部验收通过后标记 Pass 并转入 M2。

## Implementation update (2026-10-13)

代码已实现；具体文件、构建与测试证据见 progress.md。用户要求 M1/M2 一并交付、之后自行验收。上方 Acceptance Criteria 仍作为待执行的视觉/设备验收，不标记 Pass。运行方法见 acceptance.md。
