# AiluEngine Volumetric Clouds 重构 ExecPlan

> 版本：2026-10-14 · 基线仓库：`BoomBac/AiluEngine`，`master`，分析 SHA `f49b6f1d0f4da2e802f9ea33bf9681af1a9b23cb`。
> 状态：**规划完成；实现与测试未执行**。本文件不是已通过测试的声明。
> 原理补充：`AiluEngine_体积云原理与重构解析.md`（用户已有图文文档），推荐同时阅读。

## Purpose

在不重写 AiluEngine 渲染框架的前提下，将体积云的**形状、采样、单次散射、环境光、多次散射近似、时间重建**分阶段纠正为可测量、可解释、可回归的系统。算法正确性以**数学不变量 + CPU 参考解 + GPU 数值比对 + 图像验收**四层证据判断；“编译通过”和“视觉好看”均不足以单独完成验收。

## Scope

- 球形云层的几何相交、径向高度、遮挡与积分区间；基础 Debug、基线捕获与固定种子。
- Weather coverage/type、高度轮廓、Perlin-Worley、detail erosion、curl/wind；每个环节独立可视化。
- 物理量约定、Beer–Lambert、HG 相函数、单次散射、日光云影、天空环境光、受约束的多次散射近似。
- 云与现有场景、天空、透明合成及 RG 的契约；运动云的 reprojection、4×4 tile 更新；性能分档。

## Non-goals

- 不做大型全球天气模拟、雷暴动力学或流体模拟；不要求实时多阶 Monte Carlo 路径追踪。
- 不重构渲染器/RG/Camera/TAA 现有公共接口；不将体积云改为 ECS 完整实体系统。
- 不在第一阶段加入云影投射到地形、局部云实体、月光、星光、天空 LUT 全面重构。
- 不把“经验多次散射近似”宣传为严格辐射传输解。

## Architecture Constraints

1. 保留 `VolumetricClouds`、`VolumetricCloudsPass`、现有 RenderGraph 三阶段 (`Compute` → 可选 `TileReproject` → `Compose`) 为边界；仅在职责清晰后拆必要的 `.hlsli`。
2. 每里程碑必须**实现—单测—GPU 捕获—复查—记录—验收**全闭环，`PASS` 前禁止进入下一阶段。仅修改当前 Milestone 的相关文件，禁顺手大面积重构。
3. 所有数值先声明**单位和空间**：世界长度 `m`（以地球半径 6,371,000 为米制假设，须在 M0 验证）、光学系数 `m^-1`、密度无量纲 `rho ∈ [0,1]`、方向向量单位长度、辐亮度为线性空间。
4. 主形状、次形状、光学参数、时间历史分别有独立切换，不允许一次同时改多个系统后用单帧效果判优。
5. 当前 Cloud RT 约定 `RGB=预乘场景的云散射辐亮度`，`A=标量透射率 T`，合成应为 `L_out = L_cloud + T * L_scene`；云非有色吸收时优先标量 extinction，防止 RGB T 被打包丢失。
6. 颜色/光照不在 CloudMain 做 `pow(exposure)`；固定自动曝光、Tone Mapping、TAA、天气时间等干扰变量进行核心测试。
7. 新增 HLSL/C++ 命名沿用工程兼容规则；局部变量 `snake_case`，struct/class 成员 `_snake_case`，函数/类 `PascalCase`，常量 `kCamelCase`，文件内行宽尽量 ≤160。
8. **任何历史行为更改都必须验证** SceneView + GameView 多相机、Resize、Feature Off/On、RG handle 生命周期、TAA on/off；避免从既有 TAA acceptance 回归。
9. 如测试环境缺失只能记录 `BLOCKED`，绝不可标记 `PASS` 或填造 GPU 性能、截图、误差。

## Baseline / 当前确定的问题与待核对项

| 代码 | 已确认的源码事实 | 分组 |
|---|---|---|
| `Compute/volumetric_cloud.hlsl` | `cloud_coverage=1`，Weather R 未消费；高度曲线第二次 `lerp` 覆盖结果 | M2 |
| 同上 | 云壳外半径把 `height` 重复累加；求交结果未做健壮检查；高度计算用 `y` 而球壳用半径 | M1 |
| 同上 | `step*step_size` 与步长递增混用；`MAIN_RAY_THINKNESS=0.07` 与实采样长度脱钩 | M1/M4 |
| 同上 | `BeerLambert` 声明标量返回、输入向量；`LightMarch` 固定长度并求密度平均，不是完整积分 | M4 |
| 同上 | `ambient=0`；相函数余弦先映射到 `[0,1]`；经验常数 `SUN_LIGHT_POWER=300` | M4/M5 |
| `Compute/volumetric_noise_generator.hlsl` | Shape 128³、Detail 32³、Curl 128²；Curl 生成但计算密度未绑定/使用 | M3 |
| `VolumetricClouds.cpp` | 4×4 逐帧一像素，History 重投影使用场景 MV，云自身风场无完整补偿 | M7 |
| `VolumetricClouds.cpp::UpdateShaderParams` | 低质量分支只 Enable `_QUALITY_LOW`，未显式 Disable `_QUALITY_HIGH`；需验证 keyword 排他性 | M0/M8 |
| `global_volumetric_cloud.hlsl` | `Blend: One,Src`；其 `Src` 在 Shader 解析中映射为 `SrcAlpha`；Cloud RT A 语义必须保留 T | M6 |
| Camera Depth | 云用 `SAMPLE_TEXTURE2D_DEPTH_LOD`，该宏调用 `DeviceDepthToLogic()` | M0/M6 |

> **待 GPU 验证**：像素深度的实际数据约定，SRV 读写与 blend 的具体 PSO，SkyLUT 在所有相机下的空间约定，场景云体像不像云，光源方向是否在某个调用链额外翻转。不要将静态审查等价为运行结果。

## Target Outcome

1. 对**均匀密度、已知长度**可得到与解析式一致的透射率和散射辐亮度；通过低阶数值与 CPU double reference 对照。
2. 对 Coverage=0 恒无云；Coverage=1 存在合理形状；高度区间外为零；三类高度曲线均可独立呈现；密度严格非负、有限。
3. 面向太阳/背向太阳、低太阳高度、云层上中下、相机切换均合理，无明显积分/合成接缝。
4. 关闭 Temporal 时有稳定高质量参考；开启 Temporal 后给出逐像素历史正确性与运动误差证据。
5. 有固定场景、图像序列、定量误差、GPU timings 和回归门槛；质量/性能变化有可解释记录。

## Relevant Files

- `Engine/Src/Render/Features/VolumetricClouds.cpp`
- `Engine/Inc/Render/Features/VolumetricClouds.h`
- `Engine/Res/Shaders/hlsl/Compute/volumetric_cloud.hlsl`
- `Engine/Res/Shaders/hlsl/Compute/volumetric_noise_generator.hlsl`
- `Engine/Res/Shaders/hlsl/global_volumetric_cloud.hlsl`
- `Engine/Res/Shaders/hlsl/geometry.hlsli`（**只读核对，优先新建 cloud 私有函数**）
- `Engine/Res/Shaders/hlsl/atmosphere_common.hlsli`, `sky_atmosphere.hlsl`, `sampler.hlsli`
- `Engine/Src/Render/Renderer.cpp`, `Engine/Src/Render/Features/CommonPasses.cpp`
- `Engine/Res/Textures/weather_map.*`, `_blue_noise` 相关资产
- `Test/TaaCloudAcceptance.h`, `Test/AnalyzeTaaCloudAcceptance.py`, 图像抓帧设施（复用，不侵入）

## Milestones

| ID | 文件 | 核心正确性门槛 | 前置 |
|---|---|---|---|
| M0 | [基线、调试、参考测试](milestones/m0-baseline-debug.md) | 冻结输入、测试可复现、GPU/CPU 读数通路可用 | 无 |
| M1 | [球壳求交与 Ray March](milestones/m1-geometry-stepping.md) | 几何边界/步长覆盖/非有限值测试通过 | M0 |
| M2 | [Weather、Coverage、高度轮廓](milestones/m2-weather-height.md) | 0/1 coverage、云型边界、密度界限通过 | M1 |
| M3 | [噪声、侵蚀、风场](milestones/m3-noise-erosion.md) | 纹理可重复、无断缝、细节只侵蚀、形状冻结验收 | M2 |
| M4 | [单次散射与日光传输](milestones/m4-single-scattering.md) | Beer/HG/解析单次散射与 CPU reference 对照 | M3 |
| M5 | [天空环境光和多次散射近似](milestones/m5-ambient-multiscatter.md) | 无光下暗、增加环境光有界、无 NaN、可回退 | M4 |
| M6 | [合成、遮挡与大气集成](milestones/m6-compositing-atmosphere.md) | `L_cloud + T*L_scene` 数值匹配；遮挡/景深正确 | M5 |
| M7 | [云运动历史、Tile 重投影](milestones/m7-temporal.md) | 无风静止/相机移动/风动等对照无系统性错误 | M6 |
| M8 | [性能优化与回归收口](milestones/m8-performance-acceptance.md) | 全测试矩阵 PASS，性能预算有真实证据 | M7 |

## Current Milestone

**M0 — 未开始。** 当前唯一允许开始实现的 Milestone 为 M0；每一阶段只有验收签收后才修改下一阶段。

## Progress / Decision Log / Surprises & Discoveries / Validation / Known Follow-ups / Outcomes

这些字段的动态事实、测试命令、日志与复盘写在 [`progress.md`](progress.md)。不要把它改为单个越滚越大的实现文档；Milestone 中保存各自完整算法、实现与验收细节。

## 全局算法验证规则

- **三层证据**：静态推导（单位/符号/边界）→ CPU double reference（`validation/cloud_reference_tests.py`）→ 实际 Shader GPU 捕获（数值、截图、无 NaN）；图像美术验收作为第四层。
- **数值容差**：默认 CPU double 参考相对误差 `1e-9` 量级（纯算术解析），GPU float32 光学分析案例 `abs <= 2e-3` 或相对 `<= 2%` 选其适用者；每个 Milestone 可另设更严格容差。**必须先确认捕获格式和量化精度**；禁止对 half RT 直接套用 float32 的绝对门槛。
- **变体独立**：每个阶段提供 isolated test（禁用无关效果），再提供 integrated smoke test；无“通过基准套图即可跳过数值基准”。
- **截图必须可比**：固定资产 hash、camera、sun、cloud time、seed、resolution、HDR 输出格式、曝光、tone mapper、feature flags、采样数量；统一保存测量的 ROI。
- **变更记录**：记录 git SHA、编译命令、shader 编译日志、GPU 捕获、测试失败、重试与修正；不删失败记录，只新增更正。
- **失败回退**：发现错误立即在当前 Milestone 修复或回退，不以 TODO 形式把数学正确性问题遗留到下阶段。

## Resume Protocol

1. 只读本 `README.md`、`progress.md`、`Current Milestone` 对应 `mX-*.md` 及其列出的源文件；不必每次加载九个 Milestone。
2. 核对当前分支与本文件 baseline SHA 的差异，只以工作树实际代码为准。
3. 检查 progress 最近一次验证证据与状态；若缺运行产物，状态应为 `NOT_ACCEPTED` 或 `BLOCKED`。
4. 仅执行当前 Milestone；完成后将具体文件/公式/测试/截图路径/结果写入 progress，再推进指针。
5. context compaction 后从以上 4 步恢复，不默认之前声称的 PASS 已被复现。

## 参考材料

- AiluEngine 体积云原理与重构解析（随用户之前的图文文档交付）。
- Andrew Schneider, *Real-Time Volumetric Cloudscapes* / Nubis: https://www.schneidervfx.com/
- Sébastien Hillaire, *Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite*：https://blog.selfshadow.com/publications/s2016-shading-course/
- Sébastien Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering Technique* (2020)：https://doi.org/10.1111/cgf.14050
