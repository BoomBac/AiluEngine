# M1 — Camera Model, EV100 & Compatibility

## Purpose
引入真正可配置的相机感光参数并与现有 FOV、存档和 Legacy exposure 共存。

## Scope
- Camera 持久字段：`_sensor_width_mm`, `_sensor_height_mm`, `_focal_length_mm`, `_aperture_f_number`, `_shutter_seconds`, `_iso`, `_focus_distance_m`, `_exposure_compensation_ev`, `_lens_projection_mode`, `_exposure_mode`。
- `LensProjectionMode`: LegacyFov（保持旧 `_fov_h`）、PhysicalFocalLength（由焦距和 sensor height 求 vertical FOV）；其余镜头特征只保留必要元数据。
- `ExposureMode`: LegacyManual, ManualPhysical, Auto, AperturePriority, ShutterPriority, AutoISO；优先级模式后续由 M2 的测光目标驱动。
- Camera 纯数学辅助：`ComputeEv100`, `ComputeExposureMultiplier`, `ComputeVerticalFov`, `ComputeFocalLength`；添加参数范围与有限值校验。
- 相机元数据和渲染瞬时状态分开（不可把每帧 AE 的调整结果写回实体序列化字段）。

## Non-goals
不在本阶段实现 DOF、运动模糊、GPU AE；未校准场景不得宣称曝光达到摄影测光标准。

## Architecture Constraints
`fov_y = 2 * atan(sensor_height_mm / (2 * focal_length_mm))`；在现有投影矩阵作为垂直 FOV。`EV100 = log2((N*N/t)*100/ISO)`，`E = 1.44 * 2^(-EV100 + comp_ev)` 仅在选择 K=12.5/中灰=0.18 的校准策略时成立。
**不能对 EXIF 快门分母和秒数混用。** 使用 `_shutter_seconds=1/60` 表示 1/60s；UI 可显示 `1/60`。
- 默认 LegacyManual 且 `_exposure=1`，投影视角不改变；只在 PhysicalFocalLength 时由焦距决定视角。
- 修改 Camera 属性要触发正确 matrix dirty 和必要的 temporal reset；光圈/ISO/快门变化不应错误地重置几何运动历史。
- 现有 `Camera` 使用手写 `operator<< / operator>>`，新增字段需版本化迁移，不能简单假定所有旧文件有新字段。

## Baseline
现 Camera 没有物理镜头参数；`CameraComponentEditor` 与 `ComponentInspectorFields` 都能修改 FOV；多处 Camera Controllers 使用 FovH。

## Target Outcome
独立数学单测通过，旧 `.scene` 摄像机数据不变化；新物理参数持久化，手动曝光可通过统一接口返回 EV 和 multiplier。

## Relevant Files
`Camera.h/.cpp`, `Scene/Component.h`, `Framework/Math/Projection.cpp`, `Renderer.cpp`, `PostprocessSettings.h/.cpp`, `CameraComponentEditor.cpp`, `ComponentInspectorFields.cpp`, `AutomationSceneService.cpp`。

## Milestones
当前 M1；完成再 M2。

## Implementation
1. 引入轻量值类型 `PhysicalCameraSettings`，Exposure runtime state 放在渲染侧 `CameraExposureState`（非场景字段）。
2. 保留旧 `Camera::FovH()` 接口；如果镜头模式=Physical，Rebuild 仅在必要时用 sensor/focal 计算 vertical FOV。不因 `Aspect()` 调整改变选定 sensor fit。
3. 明确 setters 修改 dirty/reset 的策略；旧 Camera 序列化必须按版本兼容，新增参数默认光圈 f/4、快门 1/60、ISO100、sensor_height=24mm、focal=50mm（默认仅 Physical 模式，不改变 Legacy）。
4. 将物理光圈/快门 ISO 的曝光倍率输出规范化成统一接口，避免计算点散布在 Tonemapping、Postprocess Volume 与 Shader。
5. 在 M2 出图之前就做纯 CPU oracle：快门×2 => +1 档亮度；N×sqrt(2) => -1 档亮度；ISO×2 => +1 档亮度；改焦距只改 FOV 不改变 exposure。

## Acceptance Criteria & Validation
- [x] `f/4, 1/60 s, ISO100`，EV100=`log2(960)`≈9.90689；`f/4, 1/30 s, ISO100` 相比前者曝光恰好×2。
- [x] `f/5.656854, 1/30 s, ISO100` 与 `f/4, 1/60 s, ISO100` 的曝光相等（误差阈值 1e-4 EV）。
- [x] sensor_height=24mm、focal=50mm 得 vertical FOV≈26.99°；改变 aspect 不改变纵向 FOV。
- [ ] 旧场景逐场景 load/save 和 FOV 不变；无新模式时图像与 M0 基线一致。当前 CPU smoke 覆盖最小旧 JSON 默认值和新文档往返，实际场景/图像验收由用户完成。
- [x] 物理镜头合法输入 clamp 和无穷/NaN 防护通过。

## Current Milestone
M1
## Progress
按用户要求在 M0 runtime 验收仍待用户完成时开始 M1。Camera 数据模型、数学接口、Scene/Prefab 持久化及 CPU smoke 已实现，Debug 构建和 smoke 通过；场景级旧资产和图像验收待用户完成。
## Decision Log
纵向 FOV、Scene 元数据与 per-camera Runtime state 分离、默认 Legacy。
## Surprises & Discoveries
历史字段名 `_fov_h` 不反映实际纵向语义，保存格式需兼容旧版。
## Known Follow-ups
未来 DOF 将使用 focus distance 和 aperture，但不属 M1 画面表现。
## Outcomes
Camera 数据/数学/文档序列化接口已实现；Debug 构建和 `CameraTemporalHistorySmoke` 通过。用户负责 M1 场景级/视觉验收，M0 图像基线仍未通过。
## Resume Protocol
验收全部通过再进入 M2；重要迁移测试写 progress。
