# M6 — 场景深度遮挡、云合成及大气层次

> 前置：M5 PASS。此阶段将分离式测试结果放回整个 Renderer/RG，重点验证深度/合成语义。

## Purpose / Scope / Non-goals

- 目的：让正确的云散射按透射率准确叠加到天空/场景，避免浮在物体后方、固定屏幕背景剪裁造成云体错误消失。
- 范围：Cloud RT RGB/A 约定、线性 HDR 合成、Scene Depth 裁剪、Camera ray reconstruction、Sky LUT 依赖、Sky/Cloud/Fog/TAA pass 顺序。
- 不做：重构整条 PostprocessStack；完全 RGB wavelength-dependent extinction（除非明确另设额外数据通道）；给地面加实时云阴影。

## Architecture Constraints / Baseline / Target Outcome

- 旧 `CloudMain` 对 `depth<0.99` 直接输出 `(0,0,0,1)`；它将云渲染退化为纯天空背景，对于**物体后方云**可遮挡，但无法表达**物体前方云**。
- 当前 `global_volumetric_cloud.hlsl` pass CloudBlit 注释 `Blend: One,Src`，通过 `Shader.cpp` 确认 `Src` 解析为 `SrcAlpha`，与 `A=T` 的使用意图一致。**仍必须核对最终 PSO 的 color blending 实际配置**，不能仅根据注释断言运行一致。
- `VolumetricClouds_Compose` 对 SceneColor 应 Load，不 Clear；保持跨 RG handle 的读写依赖与资源 barrier。

## Relevant Files

`global_volumetric_cloud.hlsl`, `Compute/volumetric_cloud.hlsl`, `VolumetricClouds.cpp`, `Shader.cpp`, `CommonPasses.cpp`, `PostprocessStack.cpp`（只读确定顺序）、`Camera.cpp`, `atmosphere_common.hlsli`。

## Implementation / Algorithm

### A. Cloud RT 的严格契约

采用灰/白云散射的标量透射率：

`CloudTex.rgb = L_cloud_scattered`，`CloudTex.a = T_cloud`。

**正确合成**（在 LINEAR HDR，tone mapping 之前）：

`SceneColor_after.rgb = CloudTex.rgb + SceneColor_before.rgb * CloudTex.a`。

- 写一个全屏恒定颜色测试：`scene=(0.2,0.4,0.8)`、`cloud=(0.3,0.1,0.0)`、`T=0.25`，预期 `(0.35,0.20,0.20)`。
- 断言 `T=1` 保留背景，`T=0` 完全遮挡背景（仅 cloud radiance），没有常见 straight/premultiplied alpha 混淆。
- 如果未来需要 RGB chromatic transmittance，则当前单通道 A 不足，必须讨论第二 RT/自定义 compute compose；本轮明确暂不启用彩色消光。

### B. Scene depth 应裁切 ray，而不是直接丢掉整条射线

当前对任何可见几何 `depth<0.99` 全部输出透明云。正确做法：

1. 采样 scene device depth，沿 Camera 当前**带 jitter 的投影**还原与 Cloud sample 相同的 ray 对应的**相机前向世界位置**或 intersection t。
2. 把场景遮挡变成 `t_scene`，在 M1 云壳区间上做 `t_end = min(t_cloud_end, t_scene)`；`t_end <= t_begin` 才输出 `(0,0,0,1)`。
3. 场景物体比云远时应渲染**云在物体前方的部分**；场景物体比云近时云完全被遮挡；距离天空无限时使用完整壳段。
4. Depth 在 reversed-Z 采用 engine `DeviceDepthToLogic()`；**不要既转逻辑又按 device depth 再反一次**。采样 GBuffer Depth 的真实类型以 M0 捕获为准。
5. 对 opaque/deferred 只处理现有 GBuffer depth；透明物体排序若暂时不支持需列明限制，禁止声称已经解决透明云排序。

### C. Sky / Fog 与 LUT 坐标

- M4 的日光透射 LUT 是**太阳到样本点**的 attenuator，不是 SkyView。合成时 Cloud 的背景可能包含天空辐亮度或已有体积雾；记录天空/云/雾实际 pass 顺序，避免重复衰减。
- 将模型区分为 `L_cloud_scattered`、`T_cloud`、`L_scene_background`，大气透视对云前/后方的遮蔽须具体说明：近似版本可以保持既有 pass 顺序，但不得同时对同一 cloud 加两次大气 extinction。
- 从 M0 已知 Cloud RG 依赖读取 sky_view_lut；所有 Feature off/On 与中途 resize 必须更新完整 handle 和 history invalidation。

### D. 合成可测试路径

- 在 CloudBlit 单独加入 `debug_constant_blend` 路径，Shader debug 输出固定 `L_cloud`/`T`，不调用云噪声。
- 捕获**Blend 前 SceneColor + CloudTex + Blend 后 SceneColor** 的同帧数据，离线逐像素计算理论值。明确 Alpha RT 是否保存其它管线语义，避免影响 postprocess；如果 scene alpha 有用，单独说明其保持策略。
- 在云/opaque 交界 ROI 按 t_scene 正反两种排序测近处遮挡和远处透过，不只测天空背景。

## Validation

| 测试 | 必须通过 |
|---|---|
| 固定 alpha `T=0,0.25,0.5,1` | RGB 结果与手算 `cloud+T*scene` 逐像素一致；RGBAHalf 按预先标定量化误差 |
| depth：距离 1km 的物体、5km 起始云底 | 不可看到被近处物体挡住的云 |
| depth：距离 20km 的物体、5km 云层 | 有云在几何前面；不得被 `depth<0.99` 无条件剔除 |
| Depth sky far plane | 逻辑 depth 接近 1 时完整云段；reversed-Z 仅一次转换 |
| Sky high/low light、Cloud on/off | 无 LUT 缺失、重复 tone map、Cloud 穿帮 |
| 1920×1080、317×179、resize、历史 Off/On | 无未初始化 RT/RG read/write 版本错配 |
| Two cameras（同帧相反朝向） | LUT、Cloud history 不串相机；如当前仅支持单 camera，必须显式限制并用独立状态实例证明 |
| 场景后处理组合 | TAA on/off、Fog on/off 与 cloud 视觉位置正确；稳定色彩输出 |

## Acceptance Criteria

- [ ] GPU blend 测试准确符合 `L_cloud+T*L_scene`，无 alpha、tone mapping 或 premultiply 错误。
- [ ] 深度裁剪使用同世界 ray 的 scene hit t；物体前/后与地平线各类遮挡正确。
- [ ] 所有 RG 资源声明与 GPU 实际读写匹配，无读旧版本/非法 UAV/场景 RT Load 失效。
- [ ] Camera jitter 与 LUT 方向符合真实渲染数据；多视图/Resize 回归完成。
- [ ] 记录透明排序尚未解决的明确范围，不能把已知限制写为算法 PASS。
- [ ] 通过 GPU/色彩验证并在 progress 登记，才开 M7。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。若需要更改 Cloud pass event，必须同时运行既有 TAA/Cloud acceptance。Resume 顺序 README → progress → 本文件。Outcome 应是**精确数值的合成和合理几何遮挡**，不是仅一张纯天空截图。
