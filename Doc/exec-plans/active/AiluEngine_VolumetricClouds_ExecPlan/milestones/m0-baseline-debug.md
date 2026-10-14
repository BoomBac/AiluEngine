# M0 — 固定基线、Debug View 和验证框架

> 前置：无。此阶段 **只增加观测与测试，不调整体积云的视觉算法**。

## Purpose / Scope / Non-goals

- 目标：把“云不好看”拆成可观测的密度、光学厚度、云自阴影、合成与时序问题。
- 范围：固定场景、固定帧参数、云层 debug mode、GPU capture、CPU reference test、性能基线、所有 RenderGraph 依赖记录。
- 不涉及：修改 Coverage、云型插值、球壳半径、RayMarch 采样或 Lighting；禁止在本阶段“顺手优化画质”。

## Architecture Constraints / Baseline / Target Outcome

- 当前主链为 `VolumetricCloudsPass::OnRecordRenderGraph()` → `CloudMain` → 可选 `CloudReprojection` → `CloudBlit`。
- 保留现有 Pass 注册和 RT 命名。Debug view 可以复用 `_CloudTex`，**不可悄悄写入历史**：切 Debug Mode 时禁止复用非同类历史数据。
- 建立 two-mode 诊断：**全像素、无 Temporal** 用于算法 reference；原有 4×4 Tile 用于独立时间性能基线。
- 终点：同一设置重复捕获得到可重复的数据（若启用时间抖动，固定 frame index/seed），所有数据带参数、commit、环境元数据。

## Relevant Files

- `Engine/Src/Render/Features/VolumetricClouds.cpp`, `Engine/Inc/Render/Features/VolumetricClouds.h`
- `Engine/Res/Shaders/hlsl/Compute/volumetric_cloud.hlsl`
- `Test/TaaCloudAcceptance.h`, `Test/AnalyzeTaaCloudAcceptance.py`（尽量复用结构）；RenderGraph ImageCapture bridge。
- `Engine/Res/Shaders/hlsl/sampler.hlsli`, `Engine/Res/Shaders/hlsl/common.hlsli`

## Implementation

1. 核对当前仓库工作树与 SHA，记录编译 preset，记录编译器、DX12 debug layer、GPU、driver、分辨率及 GraphicsContext 帧索引。
2. 创建固定场景：`camera_below`、`camera_inside`、`camera_above` 三高度；`sun_high`、`sun_low`、`sun_backlit` 三光方向；每套锁定 WindTime=0、speed=0、曝光与 tone mapping、天气图，镜头/分辨率稳定。录制 RGB16F/32F 的 CloudMain 结果和 Compose 前后 SceneColor。
3. 加入 `ECloudDebugMode` 或 `uint _debug_mode`：Off、GeometryInterval、HeightFrac、WeatherCoverage、BaseDensity、FinalDensity、ViewTau、SunT、SingleScattering、Ambient、FinalTransmittance、HistoryValidity；先实现无副作用的能实现部分，后续里程碑填充真实值。
4. 诊断时由输出条件保障 Shader 不读取尚未就绪的参数：无效模式统一 0。Texture 噪声和 Sky LUT 用 RG 显式引用；避免仅靠全局 shader 资源。
5. 接入 numeric capture：固定像素位置和 ROI（云底、核心、边缘、晴空、地平线），记录 min/max/mean、NaN count、frame-to-frame diff；保留原始线性 HDR，不只保存 PNG。
6. 在仓库增加独立、零依赖的 `Test/CloudReference...` 或复用此任务包 `validation/cloud_reference_tests.py`；确保可在 Windows Python 直接运行（纯 `unittest`）。
7. 对 `SAMPLE_TEXTURE2D_DEPTH_LOD` 调用链核查：device depth→logic depth 映射、背景值=1 与 reversed-Z 配套；至少抓 `CopyDepthPass` ROI 证明真实纹理是预期格式。
8. 捕获全分辨率 128 steps / 32 steps、Tile on/off 三种情况的 GPU timestamp，不设想象的 ms；记 steady state 的 p50/p95，避免将初始化/Shader 编译计入常态。

## Algorithms and invariants

- 锁定 `_Time.y` 与 `_FrameIndex` 时，随机 offset 不应变化；检查历史值不可跨 debug mode 误读。
- 任意 debug buffer 输出应有限（`isfinite`），密度/coverage/透射率约定 `[0,1]`；色彩辐亮度非负。
- 纯数学 oracle 在开始 GPU 对照前运行；导出 reference 向量不依赖 shader 的同一算法实现（避免复制相同错误）。

## Validation (须记录实测)

| 用例 | 测量 | PASS 判据 |
|---|---|---|
| 静态同输入两次 full-res | 线性 HDR hash 或逐像素误差 | 禁时间噪声后相同；若存在非确定浮点路径，解释误差并设可重复容差 |
| 视角/高度/太阳条件 | 9 组可重放截图/参数 | 不缺少 Depth/Cloud/SceneColor 捕获 |
| Sky background vs object depth | Depth samples | 确认逻辑深度远平面近 1，物体深度低于 1；若不符，不得继续 |
| Debug mode 切换 | HistoryValid/Color | 不把前一模式 history 当正常云历史；无残影或未初始化采样 |
| 全像素/Tile 帧基准 | per-pass GPU time | 输出记录、保持按相同环境对照，不要求性能提升 |
| CPU reference tests | `python validation/cloud_reference_tests.py` | 全 PASS；日志与版本记录 |
| Shader/RG validation | 编译/资源状态 | 无 read-before-write、非法 UAV、debug layer 错误 |

## Acceptance Criteria

- [ ] 可由命令或固定 Editor 设置复现所有 9 组场景；时间、天气、光照、相机参数可归档。
- [ ] Full-res 非 Temporal 路径作为视觉 reference，可单独执行。
- [ ] Shader 捕获、深度值、GPU 时间均有真实证据（含路径），无 NaN/Inf。
- [ ] 单元测试执行真实 PASS；至少保留一组失败/修复前基线。
- [ ] 记录 baseline commit、实测结果、变更文件，维护 `progress.md` 并标记 M0 PASS。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Outcomes

当前：M0，**NOT_STARTED**。实现时在 `progress.md` 记录实情：如果 Debug output 受 LUT/Stencil 影响无法抓帧，先解决 capture 通路；不可跳过以肉眼替代。完成后的 Outcome 应是可重复诊断仪表盘，**不是更漂亮的云**。

## Known Follow-ups / Resume Protocol

后续 Milestone 将逐项填充已经定义的 Debug view。恢复执行：读取 `README.md` → `progress.md` → 本文件 → Relevant Files；检查 M0 验收清单，全部通过再移动到 M1。
