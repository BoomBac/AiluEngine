# M2 — GPU Histogram & Eye Adaptation

## Purpose
完成完全 GPU 计算的自动测光、曝光模式切换和时间自适应，暂不开 Pre-Exposure。

## Scope
- 新增 `AutoExposurePass.h/.cpp` 与 `Compute/auto_exposure.hlsl`、compute shader asset；遵守项目现有 AssetRef/Kernels/RenderGraph 约定。
- `ClearHistogram` → `BuildHistogram` → `ReduceAndAdaptExposure` 三 Compute pass；64 bins、groupshared + sparse sampling、百分位裁剪、Log2 亮度求均值。
- `R32_FLOAT` 1×1 ping-pong GPU Exposure History；每相机历史仅 GPU 更新，无 CPU readback；共享 CPU fallback scalar 只用于 disabled/failure path。
- LegacyManual / ManualPhysical / Auto / Av / Tv / Auto ISO 四类策略；计算自动目标 EV 后对可控参数应用物理约束，非法结果回退。
- Histogram 的输入采用 PostprocessStack 选中的第一路线性 HDR 输入，严格在 Bloom 前，后续 M4 加 domain 归一化。

## Non-goals
不开启 Pre-Exposure，不实现 Local Exposure，不引入与镜头无关的自动白平衡。

## Architecture Constraints
- 输入为 Linear HDR、Tonemap 前、Bloom 前；计量的是 SceneColor，Gizmo/UI 不参与。
- Histogram bins 可设 `log2_min` / `log2_max`；所有像素阈值 clamp、负值/NaN/Inf 丢弃或明确定义处理。总和为 0 时使用有效历史或 default EV。
- 建议整帧按 4×4 或 8×8 降采样计算，不要构建全尺寸 histogram per pixel 全局 atomic；统计初始参数可设 low=5 high=95，支持中间偏重调节。
- 适应使用 `alpha = 1-exp(-speed*delta_time)` 对 **EV/Log Exposure** 平滑；`speed` 单位 1/s；如果 UI 要用 stops/s，必须更换成速率限制算法而非直接改标签。
- 默认兼容：legacy scalar `_exposure` 仍照旧作用；模式未启用不分配额外 GPU 资源。
- 关闭 `camera._postprocess_enabled` 时不能悄悄接入曝光/偏移；离屏 probe 使用固定曝光/无 AE。

## Baseline
ToneMapping shader `_Exposure` 来源于 `ToneMappingSettings::_exposure`，未有 GPU exposure texture。

## Target Outcome
ToneMappingPass 从 `exposure RGHandle` 的 SRV 获取当前帧结果，显式 ReadSRV；选 LegacyManual 时采用旧标量倍率并与 M0 bitwise/epsilon 基线匹配。

## Relevant Files
`PostprocessStack.h/.cpp`, `ToneMappingPass.h/.cpp`, `PostprocessSettings.h`, `RenderingData.h`, `RenderGraph.h`, `TemporalAA.cpp`, `ShaderInterop.h`, `Editor/Src/Automation/AutomationCaptureService.cpp`。

## Implementation
1. `AutoExposurePass::Record(graph, source, camera, settings)` 返回 `RGHandle`；如果自动模式关闭则没有多余 Dispatch。
2. 清 Histogram：64×uint R/W buffer；构建：16×16 groupshared histogram，按减少后的样本数发起 Dispatch。注意与 BufferDesc/资源池/重编译的兼容；每次写新 RDG handle/version。
3. Reduce: 对直方图累计百分位，求加权平均 logL，`L=exp2(avg_logL)`；若场景已完成 cd/m² 标定，用 `EV_meter=log2(Lcd*100/12.5)`，否则 Legacy Auto 使用 uncalibrated 协议，并显式报告 domain。
4. Auto/Av/Tv/Auto ISO 的参数解算：
   - Auto：可只控制虚拟 EV，不伪称改变了 Camera 的存档光圈/快门。
   - AperturePriority：保持 N、ISO，`t = N²*100/(ISO*2^EVtarget)`；限制 shutter 范围，记录限制后的实际 EV。
   - ShutterPriority：保持 t、ISO，`N=sqrt(t*ISO*2^EVtarget/100)`；限制 f 值，记录实际 EV。
   - AutoISO：保持 N、t，`ISO=100*N²/(t*2^EVtarget)`；限制 ISO 范围，记录实际 EV。
5. 独立 camera state：identity / frame index / reset revision / previous EV / read-write ping pong，持久资源不能被 transient pool 复用。
6. Tonemap 显式依赖曝光结果和 source，两者作为 SRV；允许手动 scalar branch fallback，并维护 output `_final_color` 原约定。

## Acceptance Criteria & Validation
- [ ] 单值图 0.018 / 0.18 / 1.8（legacy luminance）对应目标倍率约 10 / 1 / 0.1，停留时间足够后收敛。
- [ ] 1% 极亮像素不应主宰 5%～95% 测光；100% 白/黑 histogram 无 NaN。
- [ ] 同一路亮度变化，30/60/120 FPS 对等时间适应结果误差 < 1e-3 EV（排除帧采样差异）。
- [ ] Manual Physical 按 M1 EV oracle，Av/Tv/AutoISO 参数满足对应反解方程且达到限值后曝光行为明确定义。
- [ ] 每相机曝光 history 独立；editor 两视口、相机切换/删除、停帧恢复无串帧与 GPU read-write race。
- [ ] GPU 曝光常态无需同步 CPU readback；RenderGraph SRV/UAV Barrier 清晰可抓帧。
- [ ] disabled / Legacy 模式图像与基线一致。

## Current Milestone
M2
## Progress
源码实现完成：新增 64-bin compute histogram、percentile reduction、GPU EV adaptation history、priority-mode 虚拟参数反解，并接入 ToneMapping。Debug 全量构建通过。Runtime shader compile、GPU dispatch、RenderGraph capture 及画面验收留给用户。
## Decision Log
按 GPU-only、EV domain 平滑、双纹理历史；可选操作学参数由摄影曝光公式反解。
## Surprises & Discoveries
Auto/Av/Tv 不仅是不同浮点倍数，还必须明确哪些物理参数实际发生变化、是否持久保存。
## Known Follow-ups
M4 后直方图改从 pre-exposed 场景按 P 还原。
## Outcomes
`M2_IMPLEMENTED_AWAITING_USER_ACCEPTANCE`。Legacy scalar 路径保留；自动模式按 4×4 稀疏采样、每 camera R32_FLOAT ping-pong，不做 CPU readback。当前 HDR 亮度仍是未标定 domain；Av/Tv/AutoISO 的物理参数解算仍待 M3 cd/m² 标定，Pre-Exposure 尚未实现。
## Resume Protocol
通过全部验收后进入 M3；保存 frame capture、GPU dispatch timing 和历史 reset 证据。
