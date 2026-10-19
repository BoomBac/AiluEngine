# M6 — Editor, Debug UI, Automated Validation & Final Acceptance

## Purpose
保证物理相机系统可用、可观察、可复现、可维护，而不仅是正确 shader。

## Scope
- Camera Inspector：Physical Lens enabled、focal/sensor、F-number、shutter/1/x、ISO、focus distance（仅数据）、EV100 实时只读；Exposure Modes；FOV vs focal 对应显示。
- Postprocess Volume：Metering method、Low/High Percent、Min/Max EV、Exposure Compensation、Speed brighten/darken、Center-weight mask、Debug histogram；ToneMapping 继续负责 AgX/ACES/Standard/None/Gamma。
- 通过 UI/脚本支持 per-camera debug：metered luminance、meter EV100、target EV、current EV、current multiplier、Pre-Exposure、Histogram；捕获无 CPU runtime readback，debug 可另行异步小量读回。
- 写稳定自动化验证，包括相机 mode switch、CameraCut、TAA history、SR、HDR 输出、不同 Viewport、旧 asset roundtrip。

## Non-goals
不实现 DOF、光学 lens effects、用户专属镜头数据库。

## Architecture Constraints
- Editor 中 Camera 参数不应在全局 PostprocessVolume 的最高优先选择中串到别的 Camera；Volume 只定义测光策略与补偿，不覆盖 Camera 物理成像参数，除非显示启用 override。
- M1–M5 的数学/空间合同不因 UI 层发生变化；对 NaN、负、0、high < low 等编辑输入校验。
- Debug 对 SceneView 与 GameView 分开；焦距 FOV UI 和脚本对当前 camera mode 一致。

## Baseline
Editor 目前两种 inspector 入口可以设置 Camera 的旧 FOV、Postprocess 的 `_exposure` 与 Gamma、ACES/AgX 模式。

## Target Outcome
编辑器能无歧义地配置物理相机并查看其实际曝光状态，自动测试与 GPU 性能/渲染基线齐全。

## Relevant Files
`Editor/Src/Inspector/ComponentEditors/{CameraComponentEditor,PostprocessVolumeComponentEditor}.cpp`, `Editor/Src/Inspector/ComponentInspectorFields.cpp`, `Editor/Src/Automation/{AutomationCaptureService,AutomationSceneService,AutomationReadModel}.cpp`, `Engine/Inc/Render/Camera.h`, `PostprocessSettings.h`。

## Implementation
1. [x] Camera Inspector 支持 Lens Projection、传感器/焦距、曝光模式、f-number、快门、ISO、补偿和仅数据的对焦距离；展示垂直 FOV、相机设置 EV100 和参考倍率，明确不等同于 GPU 自动曝光读数。
2. [x] Postprocess Volume 支持计量模式、percentile、EV 限制、亮/暗适应速度与亮度标定；新字段通过反射生成并进入 Scene Save/Load。
3. [x] Scene automation 提供 SceneView 物理设置读写，并扩展 Camera / PostprocessVolume component 字段描述供 inspect/write 使用。
4. [ ] 完成 Legacy/Physical、Tonemapper、Bloom、AA/SR、相机数、亮度、Pre-Exposure、SDR/HDR 自动测试/截图矩阵并记录 skips。
5. [ ] 增加异步小量 GPU exposure diagnostics / histogram debug view；当前 UI 与 automation 只给 CPU 可计算设置值，不伪报 GPU EV。
6. [ ] 完成低端/普通 GPU 的 AE GPU timing 与显存/稳定性报告；1080p AE ≤0.3ms 为目标，不作硬件无关保证。
7. [ ] 通过 Legacy M0 回归和 UI/Gizmo 不参与测光验证。
8. [x] 文档记录参数单位、EV 计算、物理亮度系数近似、超分颜色域及光学非目标。

## Acceptance Criteria & Validation
- [ ] UI、Reflection、Scene Save/Load、Automation API 的每个物理字段一致并可回滚。
- [ ] M0–M5 的 acceptance 全 PASS；本阶段自动测试矩阵可重复运行，并保存被 skip 的原因。
- [ ] HDR 中强烈天空光/发光材质不会因错误 domain 突然全黑或闪动。
- [ ] SceneView/GameView per-camera 互不污染；Probe 捕获等特殊视图不被普通 AE 误改。
- [ ] ≥60s GPU 重复稳定性，无明显历史泄漏、显存增长、光敏闪烁、资源 barrier 错误。
- [ ] 性能报告至少覆盖一张低端和一张普通 GPU（若不可用则记 blocker，不能伪造性能结果）。

## Current Milestone
M6
## Progress
Inspector、序列化设置、Scene automation field descriptors 与 GPU histogram metering controls 已实现，并通过 Debug build。GPU diagnostic readback/histogram UI、自动捕获矩阵、GPU profiling 与 M0–M5 的用户验收仍待完成；不得据此标记 M6 acceptance 通过。
## Decision Log
调试优先透明数据，优化在正确性通过后进行。
## Surprises & Discoveries
本次系统不仅是曝光，Editor 与 Automation 也需保证物理模型被正确控制。
## Known Follow-ups
DOF / Bokeh / Motion Blur / Lens aberration 未来另开可独立验收 Milestone。
## Outcomes
Editor/serialization/automation/AE-control implementation is partial and Debug-built. GPU exposure diagnostics, repeatable image matrix, GPU performance/stability evidence, live UI/persistence validation, and final acceptance are pending.
## Resume Protocol
完成后在 progress.md 标记 FINAL ACCEPTANCE，附命令、机器、数值、截图和剩余问题；旧模型仍需可回退。

SceneView camera settings now expose the physical lens and exposure parameters on its own camera, including live EV100 and reference multiplier readouts. Debug build passed; live Editor interaction remains for user acceptance.
