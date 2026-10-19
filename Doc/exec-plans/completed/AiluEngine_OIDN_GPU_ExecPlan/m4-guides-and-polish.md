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

M4 已由用户于 2026-10-18 明确激活；M3 尚未完成的真实场景验收继续保持 pending。

## Progress

- [x] 核对源码数据流，使用真实 First-Hit 而非栅格 guides；Compute/DXR AOV 捕获与语义检查通过。
- [x] GPU Guides Buffer/Pack/Filter 配置与 720p/1080p 合成输入 CUDA smoke。
- [ ] 画质与性能比较测试。
- [ ] 设备丢失/重建/多帧退役硬化。

## Validation / Acceptance Criteria

### 已执行的实现验证（2026-10-18）

- ON 完整 Debug 构建与正常反射生成通过；新增属性不手改 generated code，旧资产缺字段保留 false。
- 合同 smoke 拒绝请求 guides 却缺少对应句柄/owner 的输入；Pack/Unpack/Counter SM5.1 编译通过。
- `OidnGpuRenderGraphSmoke --full-hd --guides <新目录>` 真实 RTX 5070 / OIDN 2.5.1 CUDA 完成 12 次发布。
  覆盖 720p→1080p、Fast/Balanced/High、启停及 guides→color→guides 的异步 session retirement。
  持续 D3D12 回调未捕获 Error/Corruption/#951，正常退出。输入来自 GPU clear，不是路径追踪场景。
- 720p guides shared buffers 为 56.3125 MiB；1080p 为 126.625 MiB（不含 SDK scratch 和 preview/AOV textures）。
  1080p Balanced guides 的两个样本端到端延迟为 44.3585/31.4052 ms；High 三次为
  43.5415/46.6243/46.6579 ms。CPU HDR bytes=0。上述值不是纯 filter GPU 时间或正常 frame time。
- GPU 证据目录：`F:/AiluEngine/artifacts/out/test/bin/x64/debug/OidnM4_20261018_100326/`，
  `guides.log` / `guides/FileAppender_log.txt` / `contract.log`。
- 实际 Editor 验证使用 `Test/OidnGuidesEditorAcceptance.py --editor <Editor.exe绝对路径>`，
  自动生成独立漫反射/金属/玻璃/棋盘边缘场景，并通过通用反射开关设置；
  `Test/AnalyzeOidnGuides.py <证据目录>` 检查有限值、guide range/coverage 并生成对照图。
  此 runner 的序列有不同 SPP/质量，不能作为严格同 SPP 的画质收益验收。
  初始 DXR variants 冷编译曾超过 180 秒；启动等待改为有界 480 秒。失败运行的共享状态已恢复。

最终 Debug 构建包含跨帧 UAV barrier 和 guide owner/尺寸门控，合同测试通过。
最终实际 Editor 运行：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/OidnM4Editor_20261018_102003/`。
独立工程 512×320，AA 关闭，固定相机，漫反射球/粗糙金属球/玻璃球/棋盘背景/mesh emitter；
Compute 与 DXR 均成功捕获 Color-only Balanced、Guides High、较晚 Raw Reference、Albedo/Normal，
共 18 个单帧 session。AOV 捕获选 `OIDN Pack HDR` 的 input index 1/2，resource_name 用作一致性校验，
不是自动搜索输出的 selector。初次错误索引导致 capture 失败，修正后全部通过。
最终 10 项共享状态备份逐字节恢复，测试 Editor 正常退出；本轮原有用户 dirty changes 保留。

`analysis.json` / `comparison.png`：两后端全部有限；Albedo 与 coverage 为 [0,1]，
Normal 为 [-1,1]，最大向量长度约 1（Compute 1.000000000001，DXR 1.000000058），
surface pixels 分别 87,198 / 87,196，显示源 HDR peak 分别 182 / 224。
对照图已人工查看：首命中玻璃的 albedo 为白色、normal 为玻璃表面，而非背景的栅格替代；
checker/material 边界可辨。图中的 normal 为仅供显示映射到 [0,1] 的图，OIDN 输入仍为 signed mean。
最终 debug/FileAppender 日志未发现 D3D12 Error/Corruption/#951；存在其他既有 warning，不称完全无告警。
这验证了本固定场景的 DXR→Draw 路径，但不能代替用户原场景 #951 回归。

三列 ROI 的 log(1+HDR) RMSE 仅供观察：Compute matte/metal/glass 的 Color-only→Guides
分别 0.05854→0.05267、0.09694→0.08560、0.06451→0.05948；DXR 分别
0.04819→0.04426、0.09221→0.08434、0.06522→0.06531。
SPP 不同、quality 也不同，且 Raw reference 尚未保证高 SPP 收敛，不能将这些数值归因为 guides 收益。
玻璃并非必然改善，保持 Color-only 默认；严格同 SPP/quality 的 1/4/16/32 SPP 对照仍 pending。
验证捕获有显式 DDS/PNG 读回；生产 Pack/filter/Unpack 没有 CPU HDR 读回/上传路径。

非整尺寸 guides 100 次发布也通过，证据：
`F:/AiluEngine/artifacts/out/test/bin/x64/debug/OidnM4Odd_20261018_101421/guides.log`。
没有新增双 slot/资源缓存；无 profiling 瓶颈依据，暂不作性能架构改动。

- [ ] 每种测试场景保留 1/4/16/32 SPP、Reference 高 SPP、Color-only、With-Guides 对照。
- [ ] Albedo/Normal 能通过 debug AOV 验证语义与像素对齐，不存在明显沿透明物体错位。
- [ ] Guides 带来可解释的高频细节改善，若伪影更严重则保留 Color-only 作为默认。
- [ ] 所有 AOV/Preview 仍停留 GPU，Copy/Map/Readback HDR bytes=0。
- [ ] 不增加无限缓冲缓存，峰值 VRAM 和滤波延迟有数值证据；Resize/Shutdown 无资源泄漏。
- [ ] 原 None、Debug、Reference 逻辑不回归。

## Decision Log

引导先正确后优化；只基于可重复画质数据决定是否把 Guides 设为默认。

2026-10-18：不复用光栅 GBuffer。Compute/DXR 共用 TracePath 在 stochastic coverage 接受后、
第一次 reflection/refraction 前捕获 guides，避免再追踪一次光线或改变随机序列。
Albedo 是 saturate(material albedo)，glass 为 1；Normal 是入射侧 world-space shading normal（同 BSDF 的
geometric fallback），逐样本单位化后按 Reference 同样本权重平均，不对最终均值重新单位化。
Miss/analytic light 使用零 guide，alpha 为表面 coverage。cleanAux=false，Color-only 默认不变。
两个 guide 与 HDR 分别占输入共享 allocation 的三个 float4 planes；只导入一个共享输入，沿用单 slot 和既有 fences。
输出 allocation 不扩大。无 profiling 瓶颈证据，不增加缓存或双 slot。

新增 `_denoiser_use_guides=false` 反射属性；启停重置 Reference history/generation，filter feature 改变走原异步 retirement。
通用 `render.feature_property` 通过现有反射桥读写属性；桥按 enum 的真实 Size 访问，防止 u8 质量枚举覆盖相邻 bool。

## Surprises & Discoveries

光栅 GBuffer 和路径追踪 First-Hit 很可能在玻璃、透明材质上不一致，错误引导可能劣于 Color-only。

实际 Editor 首轮启动发现 DXR library 的自动 UAV 寄存器为 UINT_MAX，CreateStateObject #1194；
已给 DXR AOV 明确指定 u1/u2，后续 Editor state object 创建通过。Compute 仍由编译器分配绑定。
正常 Debug 构建和 Compute/DXR guides 离线 DXC 编译通过（必须带引擎的 SHADER_DXC 宏）。
首轮离线缺少该宏导致共享 color_space_utils 的 HLSL 2021 vector ternary 错误，补齐编译参数后通过，未改共享函数。

## Known Follow-ups

动态实时 NRD 独立阶段需要 per-frame 新采样 Diffuse/Specular/HitDistance/NormalRoughness/MotionVector；不以本 M4 的 Preview AOV 直接替代。

## Outcomes

First-Hit Guides 实现及本轮构建、真实 GPU plumbing、固定 Editor 场景 capture/数值/人工图像检查已完成。
M4 acceptance 保持 pending：同 SPP/quality 收益、完整 AlphaBlend/热载/设备丢失与故障矩阵、
正常 frame time、SDK scratch/峰值 VRAM 和长期泄漏验收尚未完成。未将 Guides 改为默认。

## Resume Protocol

用户已明确激活 M4。恢复时读取 README/progress 和本文件，先保留已完成的实现与证据，
继续同 SPP/quality 与完整生命周期/性能验收；M3 原用户场景遗留项不自动关闭。不返工既有 GPU-only 同步架构。
