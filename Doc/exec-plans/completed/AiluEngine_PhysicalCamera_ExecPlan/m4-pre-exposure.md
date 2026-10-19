# M4 — Pre-Exposure Across Renderer and Histories

## Purpose
把真实物理光照所需的大亮度范围映射到精度有限的 SceneColor 中，同时维护自动曝光、时域重投影和后处理的恒等关系。

## Scope
- `P_frame` 在每相机每渲染帧固定，写入 `CBufferPerCameraData` 和 `RenderingData`；渲染帧开始快照上一帧稳定 display exposure。
- 场景 HDR RGB 输出经过 `P_frame`：deferred、forward、sky、volume/cloud、sprite/emissive、GI 组合与所有直接写 SceneColor 的分支。
- TAA history rescale；HDR Bloom threshold 语义、tone mapping 的 P 反算；AE histogram 按 P 反算真实场景亮度。
- 相机 probe/static capture 可强制 P=1；不把场景物理光照缓存（GI/IBL/SH）永久写成针对某相机的预曝光空间。

## Non-goals
不重构 SSR/GI 算法本体；不对 GBuffer 的 roughness/normal、motion vector、depth、shadow 进行预曝光。

## Architecture Constraints
**核心恒等式**：`C_pre = C_scene * P_frame`；`L_scene = L_pre / P_frame`；`ToneInput = C_pre * E_display / P_frame`。
- 保持 `P_frame` 的时间点不变：显示曝光 `E_display` 可在帧内统计更新，但下一帧才作为预曝光 P 使用。不要在同一帧先写一部分 scene color 再改变 P。
- 若缓存上一帧 `C_hist_pre = C_scene_old * P_old`，本帧 TAA 混合前应转换 `C_hist_adjusted = C_hist_pre * P_frame / P_old`。
- 注意 TAA reactive snapshot、overlay 的曝光域：不携带线性光颜色的 mask 不能乘曝光；只在已声明的 Scene HDR 颜色阶段预乘一次。
- 此快照 `kDefaultHDR=R11G11B10_FLOAT`，禁止引入大量负值/非有限值；检查高光溢出与暗部精度。
- RenderGraph 使用 transient 创建、per-camera persistent Import、显式 SRV/UAV 依赖；Shader/global CB 在多相机录制中不得依赖变化中的全局裸指针状态。

**实现决策（2026-10-17）**：Auto Exposure 的稳定 EV 只存在 GPU per-camera history texture，现有路径禁止 GPU→CPU readback；因此不能把自动模式的上一帧 `E_display` 伪装成 CPU 写入的 `CBufferPerCameraData` 标量。自动模式的 `P_frame` 必须由 GPU 可读的 per-camera 持久资源在本帧场景着色阶段消费，并且保持上一帧值至本帧结束；Manual Physical 可由 CPU 设置提供确定值。任何实现若仍要求自动模式的 CPU 标量快照，须先改变无读回约束并给出成本依据。

## Baseline
CameraCB 有保留 padding；TAA 有自己的 ping-pong history 和 reset revision；Bloom 使用 Scene HDR source，并会参与 threshold/composite。

## Target Outcome
`P=1` 与原管线一致；`P!=1` 时所有正常场景的最终 ToneMap 值与 P=1 数学等价（给定当前 `E_display`），且实际 HDR RT 范围收敛。

## Relevant Files
`ShaderInterop.h`, `Renderer.cpp`, `RenderingData.h`, `Camera.h/.cpp`, `Features/{DeferredLightingPass,PostprocessStack,BloomPass,ToneMappingPass,TemporalAA}.cpp`, `Compute/taa.hlsl`, `lighting.hlsli`, `sky_atmosphere.hlsl`, `global_volumetric_cloud.hlsl`, `RayTraceGI.cpp`, `RenderGraph.h`。

## Implementation
1. 初始化 per-camera GPU exposure history；Manual P 写入 CameraCB，Auto P 通过 bindless SRV 消费。为遵守 no-readback 合同，Auto 的 `P_frame / E_display / previous history P` 通过 GPU texture capture 观测，不建立 CPU 镜像。
2. 用 producer inventory 逐个检查 SceneColor 写入；尽量在**最终线性 HDR 光照结果的汇合写出点**乘 P，避免在中间 radiance buffer/GI cache 中重复 P。透明/体积 additive compositing 必须保证前景和背景域相同。
3. TAA/反应性颜色历史跨帧乘 `P_current/P_previous`；对 camera cut、viewport resize、AA 切换等 reset 场景采用合法 seed。
4. Bloom 如使用绝对 HDR threshold，则 threshold 也必须变换为 pre-exposed 的对应域或对亮度除 P 再判定；compose 留在 pre-exposed 域。
5. Histogram 中 `L_pre/P_current`；ToneMapping 中 `C_pre * E_display / P_current`；旧关闭模式强制 P=1 以保证向后兼容。
6. 捕获 HDR input/out、threshold 前后画面、TAA history 和曝相机的读写纹理；断点验证直接点光、天空、云、透明、2D 的每个 color domain。

## Acceptance Criteria & Validation
- [ ] 固定 E 时 P=1 / P=0.1 / P=10，ToneMapping 前归一化值和最终 SDR/AgX/ACES 相同（浮点容差；R11G11B10 误差要给出 max/RMS）。
- [ ] P 有 2×/4× 突变时，静态 TAA 结果不随 P 明显明灭，历史 RGB 重投影统一。
- [ ] 光照、天空、IBL、云、透明、emissive、Gizmo/Editor overlay 各一次且仅一次曝光；GBuffer 和非 color buffer 无误乘。
- [ ] 相机切换、删除、跳帧与重置不会读取生命周期无效的历史纹理；HDR overflow 和低亮精度对比有可证数据。
- [ ] 禁用时 M0 帧图不变；新增路径相关 RenderGraph barrier 无错误。

## Current Milestone
M4
## Progress
M3 尚有 sky/IBL/emissive/UnitlessLegacy 标定待办；按用户指示继续 M4。已实现 RenderGraph camera 的 GPU-resident per-camera `P_frame`：Auto/Av/Tv/AutoISO 在本帧读取上一稳定 EV texture，当前适应结果只供下一帧使用；Manual Physical 用 Camera 参数构造 P。已接入 deferred/forward、sky、cloud/fog/volume、sprite、RTXDI 等线性 HDR producer，以及 AE histogram 反归一化、TAA 历史缩放、Bloom 绝对阈值和 ToneMapping P 反算。探针及 Legacy 路径保持 P=1。TAA 在曝光模式变化时清除旧域历史，避免 auto GPU EV 历史与 CPU 标量 fallback 混用。

当前仅完成源码实现与 Debug 编译；shader 编译、RenderGraph 资源/barrier、HDR 数值图像 oracle、P=1/0.1/10 对照和实际视觉验收均未完成。DLSS/FSR 与非 RenderGraph 路径留给 M5。
## Decision Log
P 使用上一稳定帧，不在帧中变化；GI Radiance Cache 等为 scene linear，不存 per-camera P。
## Surprises & Discoveries
仅在 ToneMapping 乘一个倍率并不构成 Pre-Exposure；必须追溯所有 SceneColor 写入及时间历史。
## Known Follow-ups
MSAA/其他 temporal 特性加入时须沿用同一 domain。
## Outcomes
M4 runtime code implemented; `cmake --build --preset build-debug` passed. Shader/runtime, GPU/image acceptance, and the full Acceptance Criteria remain pending.
## Resume Protocol
通过全部验收再进入 M5，保留 P=1/10/0.1 抓帧证据。
