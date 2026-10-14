# M3 — 3D Noise 规范化、局部侵蚀、Curl/Wind

> 前置：M2 PASS。目标是使云形细节合理且稳定，保持 Base Shape 的大尺度几何组织。

## Purpose / Scope / Non-goals

- 目的：验证 128³ Shape 及 32³ Detail 生成路径，消除数值越界、噪声分布失控、纹理 tiling 缝、侵蚀方向反转和卷曲风漂移。
- 范围：Shape Perlin-Worley、Worley FBM/Perlin Detail 的选择与统计、detail erosion、Curl distortion、风场统一。
- 不做：加入 256³/512³ 大体素资源，直接引入新的天气模拟、光照/Temporal 优化。

## Architecture Constraints / Baseline / Target Outcome

- 当前 `ShapeNoiseMain`（Perlin-Worley + Worley 多 octaves）；`DetailNoiseMain` 三通道 `perlinfbm`，不是标准 Worley Detail；`CurlNoiseMain` 已输出 `_curl_noise`，但 CloudMain 未绑定/使用它。
- 优先保留纹理对象生成、CPU/GPU 资源管理；算法判断后才调整 detail 纹理内容，固定显存预算。
- 输出：大轮廓不被细节噪声割成碎花；密度、UV、时间动画连续；关闭 detail 时与 M2 Base Density 精确一致。

## Relevant Files

`Compute/volumetric_noise_generator.hlsl`、`Compute/volumetric_cloud.hlsl`、`VolumetricClouds.cpp`（Curl 绑定），`VolumetricClouds.h`（频率/侵蚀/风场属性）。

## Implementation / Algorithm

### A. 基础噪声数值审计

- 检查 `perlinfbm` 的范围：当前 `gradientNoise` 为有符号输出，`pfbm = abs(pfbm*2-1)` 后理论不必然落在 `[0,1]`；生成后用 GPU histogram/极值确认，并决定是归一化还是重新映射，避免先假定正确。
- `worleyNoise = 1-minDist` 可以低于 0；对重映射层做显式 clamp，是否在噪声原函数 clamp 则取决于统计需求；不要不经测量全链 saturate 造成平台化。
- 每个 Noise 3D slice 导出 R/G/B/A，评估有无大面积饱和/断层；统计 `nan_count`/`inf_count`、min/max、p5/p50/p95、频谱趋势。
- 注意 `hash33(mod(p, freq))` 的整数周期及三维纹理采样 Wrap；检查 3D 纹理边界**函数值与一阶变化率**，而非只检查 Wrap 地址模式。

### B. Shape 与 detail erosion 的职责

- `shape_value = PerlinWorley(...)` 构造大尺度体积；`weather_coverage` 与 `height_profile` 已在 M2 确定。
- 计算 `rho_base = ApplyCoverage(shape,coverage) * height_profile`。
- Detail 选用 3D WorleyFBM 或经测试合理的 detail noise；**侵蚀只降低密度**：`rho_final = max(0, rho_base - edge_erosion * erosion_weight)`，`erosion_weight = _erosion_strength * f(height_fraction)`，避免 `Remap(base, high_freq*0.2, 1)` 产生负数或无控制增长。
- 云体核心不要都被侵蚀：`erosion_weight` 可乘近边界函数 `edge_weight = 1 - smoothstep(edge_min, edge_max, rho_base)`；先选择稳定连续的函数并记录参数。
- DetailNoise `SampleLOD` 不必和 BaseNoise 相同：屏幕足迹远距离降低高频侵蚀，避免一圈圈 mip switching 引起跳变。LOD 选择由可计算的 ray footprint 或固定 conservative bias 驱动；最后验证。

### C. Curl + Wind

- Curl 采样 2D XZ 或 X/Y 平面必须先明确定义坐标，向量 remap `[0,1]→[-1,1]` 后做 `sample_pos.xz += curl * curl_strength`；强度使用 `m` 而不是以纹理 texel 直接解释世界偏移。
- 对所有 shape/coverage 的世界空间动画用**同一个已定义**的 advected coordinate：`sample_pos - wind_velocity * time_s`。云团运动方向应该与 `wind_velocity` 同向；天气图和 base shape 的风场关系要可切换/可复现。
- 细节流动可另设小尺度扰动，但本阶段留出 `detail_flow_velocity` 接口时必须明确 Temporal 不能只用单个 MV 完全重建细节（M7 有历史拒绝策略）。
- 风场冻结时 `time` 只能用于抖动而不能改变 density；`_BaseWorldPos` 之前在 Shader 中几乎未参与 shape，需要按已声明作用接入或删除，禁止成为暗含坐标偏移。

### D. 迭代顺序

1. 固定所有随机种子，保存 Shape/Detail 纹理参考；修 NaN、区间、频谱异常。
2. 在**BaseDensity 已正确**的前提下启用 Detail；先固定 erosion_strength=0，再逐步增大。
3. 加 Curl：幅度设 0 与小值、固定时间，证明仅移动轮廓而不无故增加云体。
4. 加 Wind：速度 0 → 典型速度，冻结相机验证位移方向和速度（单位 `m/s`）。
5. 启用远近 LOD；对比相同光照下静态形态和高频闪烁。

## Validation

| 测试 | 判据 |
|---|---|
| 两次同种子生成 | 3D noise data 逐 texel 相同或有可解释浮点容差 |
| 每通道 3D noise 统计 | 全 finite；无预料外负值/大于 1；非零方差；无大片平台饱和（记录阈值与直方图） |
| 三维 UV 边界与邻域梯度 | 平铺两侧无明显 seam；截面数值和导数差在预设纹理精度容差 |
| erosion_strength=0 | `rho_final == rho_base`（GPU 绝对误差 ≤1e-5，格式允许时） |
| 正常 erosion_strength>0 | 对固定输入 `0 ≤ rho_final ≤ rho_base`；10k 采样 violations=0 |
| coverage=0 | Curl/Detail 开启时仍 density=0 |
| static speed=0 | 连续 32/64 帧的固定 world positions 密度保持不变 |
| wind=(v,0,0) | 特征点位置约以 `v*dt` 正向移动；与 CPU coordinate oracle 一致 |
| LOD 切换 | 相邻距离带密度统计无不合理阶跃、无明亮闪点 |

## Acceptance Criteria

- [ ] 噪声通道数值与统计已归档；无 NaN 和边界 seam。
- [ ] Shape、Detail、Curl 分阶段截图，审稿可分辨各自贡献。
- [ ] 高度/coverage 语义仍满足 M2 的强不变量。
- [ ] Erosion 是有边界的侵蚀，不是无意增加/扩大云体。
- [ ] 风场有统一世界单位并有冻结可复现场景；动画方向正确。
- [ ] Compile/GPU validation/性能对照记录；M3 PASS 后进入 M4。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。Noise 分布/视觉满意度是工程选择，不属于可数学证明的唯一解；需同时保存此前后图与数值断言。未来可探索更高分辨率/多云层但不在本阶段。Resume：README → progress → 本文件；失败即停在 M3。
