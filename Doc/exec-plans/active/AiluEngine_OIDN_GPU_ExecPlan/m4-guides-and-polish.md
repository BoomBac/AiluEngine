# M4 — 可选：First-Hit Guides、性能与生命周期硬化

## Purpose

在 M3 已稳定运行的条件下，按实际收益为 OIDN GPU 预览引入高质量 Albedo/Normal 引导，并降低共享缓冲和滤波准备开销。

## Scope

- First-Hit Albedo + Normal GPU AOV，验证与 PathTracer 主光线第一命中一致；通过 GPU Compute Pack 到独立共享 Buffer。
- OIDN `RT` filter 的 `albedo` / `normal` / `cleanAux` 参数组合和质量档位比较。
- 有限且受控的 Buffer/PreviewTexture 复用、异步 Fence 退役优化；必要时加入双 slot，但以可测的实际瓶颈为准。
- 用户可以选择 Color-only 与 Color+Guides；增益指标和破坏性伪影要量化。

## Non-goals

不新增 CPU fallback、不为 NRD 先实现完整 GBuffer Diffuse/Specular 分离、不改 PathTracer 的主积分算法。

## Architecture Constraints

- 当前 `GBuffer0.xy`、`GBuffer1.rgb` 是光栅 GBuffer，并不保证与路径追踪第一命中一致，特别是透明/折射、AlphaCutout、任意可见性差异；**先验收一致性再决定是否复用**。
- 引导图必须使用 OIDN 所需语义与空间：Normal 与被追踪表面法线一致并单位化（选择 world/camera space 要全帧一致），Albedo 为光照无关表面特征。
- 不能把阴影、曝光或噪声混进 Albedo；`cleanAux` 只能在这些 guides 足够干净时启用。
- Color+Guides 参数改变意味着 Filter Features 改变，旧 Filter/Buffer 先安全退役后再 Commit；GPU SPP/Gen 绑定不可漏。
- 真实最终色彩仍为线性 HDR，在显示后才经过原有 tone mapping。
- 每张 guide 的 Pack 与 Input Buffer 均 GPU-only，无 HDR CPU map/readback。

## Baseline

- M3 必须在无 guides 的 Color-only 条件下全部 Pass。
- `defered_standard_lit.hlsl:85-91,141-149` 有光栅 GBuffer 候选信息，但 PathTracer 并不必然共享第一命中。

## Target Outcome

在给定固定场景和 SPP 下测得 Color+Guides 的画质提升或明确放弃不适合的引导；保持第一版纯 GPU-only 且不损害内存/性能稳定性。

## Relevant Files

- `Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl`（仅在确认需要真实第一命中 AOV 时作极小局部修改）
- `Engine/Res/Shaders/hlsl/DXR/Raytracing.hlsl`（DXR 路径 AOV 一致性）
- `Engine/Src/Render/Features/RayTraceGI.cpp`
- `Engine/Src/Render/Denoiser/OIDN/OidnGpuDenoiser.cpp`
- `Engine/Res/Shaders/hlsl/ray_trace/oidn_pack.hlsl`

## Implementation

1. 固定 3 类测试场景：不透明漫反射、粗糙金属高光、玻璃/半透明与背景边缘。保存高 SPP Raw 参考图。
2. 评估 Raster GBuffer 是否可以作为 guides；为每种材质输出 debug Color/Normal/Albedo 三视图，在 PathTracer First-Hit 错位时必须新增第一命中 AOV，而不是盲用。
3. 指定 AOV 的像素布局、法线坐标空间、Sky Miss 默认值、RGBA stride、有效性 mask；实现 GPU Pack 与 OIDN import。
4. 比较 Color-only 和 Guides `FAST/BALANCED/HIGH`，检查小字/玻璃/反射边缘是否改善或恶化。
5. 仅在 profiling 显示原来性能瓶颈后才做独立 Input/Output/Preview 资源缓存与适度多 slot；严格约束提交并发和 GPU Fence 生命周期。

## Milestones

M4 是增强，可在 M3 MVP 完成后独立排期，不影响交付。

## Current Milestone

M4（可选，当前未激活）。

## Progress

- [ ] 栅格引导与真正 First-Hit 对齐检查。
- [ ] GPU Guides Buffer/Pack/Filter 配置。
- [ ] 画质与性能比较测试。
- [ ] 设备丢失/重建/多帧退役硬化。

## Validation / Acceptance Criteria

- [ ] 每种测试场景保留 1/4/16/32 SPP、Reference 高 SPP、Color-only、With-Guides 对照。
- [ ] Albedo/Normal 能通过 debug AOV 验证语义与像素对齐，不存在明显沿透明物体错位。
- [ ] Guides 带来可解释的高频细节改善，若伪影更严重则保留 Color-only 作为默认。
- [ ] 所有 AOV/Preview 仍停留 GPU，Copy/Map/Readback HDR bytes=0。
- [ ] 不增加无限缓冲缓存，峰值 VRAM 和滤波延迟有数值证据；Resize/Shutdown 无资源泄漏。
- [ ] 原 None、Debug、Reference 逻辑不回归。

## Decision Log

引导先正确后优化；只基于可重复画质数据决定是否把 Guides 设为默认。

## Surprises & Discoveries

光栅 GBuffer 和路径追踪 First-Hit 很可能在玻璃、透明材质上不一致，错误引导可能劣于 Color-only。

## Known Follow-ups

动态实时 NRD 独立阶段需要 per-frame 新采样 Diffuse/Specular/HitDistance/NormalRoughness/MotionVector；不以本 M4 的 Preview AOV 直接替代。

## Outcomes

未执行，可省略。

## Resume Protocol

M3 全验收且在 `progress.md` 明确激活 M4 后执行；仅修改此阶段所需代码与文档，不返工已稳定的 GPU-only 同步架构。
