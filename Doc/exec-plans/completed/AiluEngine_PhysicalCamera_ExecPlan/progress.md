# Physical Camera ExecPlan Progress

## Purpose
维持阶段状态与恢复入口，避免长任务每次读全量设计。

## Scope
追踪 M0–M7 的关卡、决策、验证与下一原子步骤，不保存重复的完整实现细节。

## Non-goals
本文不代替 mX 的算法描述，不宣称已运行 GPU 测试。

## Architecture Constraints
严格遵守 README 的单位、Color Domain、兼容性、RG 资源声明与前一阶段验收门槛。

## Baseline
已基于 2026-10-16 源码快照及当前 checkout 审计；新增 camera CPU smoke 与 ExecPlan 记录。Camera smoke 已在 Debug 构建并通过；未运行 Editor 或取得 GPU 图像。

## Target Outcome
按阶段完成并保留可追踪的证据、结果、风险。

## Relevant Files
`README.md`、`m0-contract-and-baseline.md` 与后续 mX；源码详见 README。

## Milestones
- [ ] M0 单位/管线审计与可重现基线
- [ ] M1 物理相机数据、数学、序列化与兼容
- [ ] M2 GPU Histogram / Exposure Adaptation (implementation complete; GPU acceptance pending)
- [ ] M3 物理光照单位/灰卡验证 (部分 GPU acceptance 完成；HDR sky/IBL/emissive/legacy-domain calibration pending)
- [ ] M4 Pre-Exposure + TAA / Bloom / SceneColor
- [ ] M5 DLSS/FSR/非 RenderGraph 兼容
- [x] M6 Editor + 自动验收与性能 (用户于 2026-10-18 声明完成；本次未独立复核)
- [ ] M7 物理景深与多边形散景

## Current Milestone
**M7**。用户于 2026-10-18 声明 M6 完成并要求开始 M7；该状态记录为用户验收，本次没有独立复核 M6。当前实现景深与散景 pass、Volume 参数及自动化接口。

## Progress
- 已读取并分析主要 Camera/曝光/光源单位/超分相关源码（静态分析）。
- 设计明确 Physical EV、metering 和 Pre-Exposure 数学合同。
- 核实 `_fov_h` 当前按垂直 FOV（度）解释，投影矩阵按 aspect 推导水平 scale；旧 Camera 文本字段 `_fovh` 保持原名。
- 记录 SceneColor、TAA、Bloom、ToneMapping、FinalBlit 的源码格式/消费路径与后处理默认差异。
- 扩展 `CameraTemporalHistorySmoke`，加入 CPU EV100/metering 与三种 aspect 的垂直 FOV 矩阵 oracle。Camera archive 源码确认仍读写 `_fovh`；该 smoke 无法直接调用未从 Engine DLL 导出的自由 archive 函数。
- 当前会话没有运行中的 Editor，也没有可连接的 render capture automation；未采集 PNG、GPU readback 或图像统计。
- `ToneMappingSmoke` Debug target 构建成功；运行初始化时报 ImGui `Could not load font file!` assertion，未生成 capture。失败输出目录记录在当前 M0 milestone。
- M1 已加入 `PhysicalCameraSettings`、投影/曝光模式、EV100 与统一曝光倍率接口；相机设置进入 SceneCamera 文档并覆盖场景/Prefab 保存加载。
- Legacy 默认保持原 FOV 与曝光；手写 Camera 文本 archive 保留旧字段并在尾部追加版本化可选设置块。
- CPU smoke 覆盖物理曝光/FOV 数学、曝光修改不清除投影历史、无效输入钳制，以及新旧 SceneCamera 文档读写。
- M2 新增 GPU-only 三阶段 histogram/EV adaptation pass、自动模式 tone mapping 输入与 64-bin compute shader；metering 在 Bloom 前采样所选线性 HDR 输入。
- M2 的 Auto/Av/Tv/AutoISO 状态按 camera hash 保存 R32_FLOAT ping-pong history；history reset/cut/跳帧时重新收敛，优先模式仅在 GPU 上虚拟反解曝光参数。
- M2 保持 Legacy Manual scalar 曝光；Manual Physical 使用 M1 曝光倍率。没有引入 Pre-Exposure 或 CPU readback。
- M3 新增可配置 `_scene_luminance_to_cd_m2`（default 683.0，来自 `LightUnits.h` 683 lm/W authored-light 近似的反向映射），只用于 Manual Physical 与 Auto/Av/Tv/AutoISO；Legacy 输出不变。
- Auto histogram 先把线性 SceneColor 的 log luminance 转为近似 cd/m² 求 Meter EV，再把 exposure multiplier 换回 SceneLinear 域；Manual Physical 将相同系数乘到 M1 相机倍率上，保证两种路径共用换算。
- `CameraTemporalHistorySmoke` 增加 CPU analytic oracle：1000 Lux、0.18 Lambertian 卡计算约 57.30 cd/m²、单位线性及手动/自动灰点重合；不代表实际 shader/GPU 图像验证。
- 源码审计确认 Raster/RT punctual light extraction 共用 `GetLightRadiometricScale`，点/聚光 shader 使用 inverse-square + smooth range window；天空仍有经验 PI 缩放，IBL/Emissive/UnitlessLegacy 不保证共享光度尺度，Blender `scale_length` 未用于坐标换算。

### M4 Pre-Exposure implementation
- RenderGraph Physical camera views use per-camera EV history textures as the stable previous-frame pre-exposure source; no GPU-to-CPU readback was added.
- Manual Physical supplies a deterministic scalar P. Auto/Av/Tv/AutoISO expose the previous stable GPU EV through the camera bindless SRV; current adaptation output is consumed on the next frame.
- Scene HDR producers include deferred/forward lighting, sky, clouds, fog/volume, sprites, and RTXDI. Probe/Legacy paths keep P=1; editor overlays remain after tone mapping.
- Auto histogram removes P, TAA rescales history and resets on exposure-mode changes, Bloom preserves its absolute threshold, and ToneMapping divides display exposure by P.
- `Doc/architecture/rendering.md` now describes the implemented RenderGraph contract and M5 boundaries.
- Runtime shader compilation, RenderGraph capture/barrier checks, P=1/0.1/10 image comparison, and visual acceptance are pending. DLSS/FSR and non-RenderGraph integration remain M5.

## Decision Log
- D1：现有 ToneMapping_None 仍执行实际 Pass；不再按旧版条件跳过理解。
- D2：旧场景默认 Legacy；新 Camera 显式切换 Physical。
- D3：GPU-first / 不做运行时 readback，保留 debug readback 工具的可选验证用途。
- D4：按用户要求在 M0 runtime acceptance 待验收时继续 M1；不更改 M0 通过状态。
- D5：Auto/Av/Tv/AutoISO 在 M1 暂返回物理手动基准曝光倍率；测光/自动调节属于 M2。
- D6：按用户明确指示，在 M1 验收未记录时继续 M2；不追记 M0/M1 验收通过。
- D7：M2 用当前未标定的线性 HDR 值执行 Legacy 18% gray metering；直方图百分位裁剪及 EV 域指数适应仅提供 GPU 曝光结果，不改写序列化光圈/快门/ISO。
- D8：Av/Tv/AutoISO 当前用同一未标定 HDR 域做虚拟参数反解；其物理数值在 M3 亮度标定前不视为最终 oracle。
- D9：source-derived 683 只作为 authored photometric-light approximate inverse coefficient；Postprocess exposure setting 可独立校准，且 Physical-only，legacy 不缩放。
- D10：按用户明确指示在 M2 GPU acceptance 未记录时进入 M3，不追记 M2 通过。

## Surprises & Discoveries
- FOV 输入实际为垂直 FOV。
- HDR 格式 R11G11B10_FLOAT。
- FSR preExposure 固定 1.0；DLSS autoExposure true。
- Sky/cloud 对物理单位需单独审计，不能只靠点灯数值推定真实亮度。

## Validation
M0: `ToneMappingSmoke` target 构建成功，但执行在字体初始化 assertion 失败；GPU 图像基线、帧完整性、RGB 统计与实际视觉验收未执行。用户负责后续验收。

M1: `cmake --build --preset build-debug --target CameraTemporalHistorySmoke` 成功；`F:/AiluEngine/artifacts/out/test/bin/x64/debug/CameraTemporalHistorySmoke.exe` 通过。此为 CPU/API 验证，不代表 Editor、GPU 图像或实际视觉验收；后续验收由用户自行完成。

M2: `cmake --build --preset build-debug` 成功，包含 Engine 与 Editor 编译/链接及反射生成。此构建没有编译运行时 HLSL，也未执行 GPU dispatch、RenderGraph capture 或视觉验收；由用户完成验收矩阵。源码检查覆盖 Histogram、history 与 ToneMapping 的 RenderGraph 读写声明。

M3: `cmake --build --preset build-debug --target CameraTemporalHistorySmoke` 与完整 `cmake --build --preset build-debug` 均成功；`F:/AiluEngine/artifacts/out/test/bin/x64/debug/CameraTemporalHistorySmoke.exe` 通过，其中包含 CPU analytic 光度转换与 gray-card/manual-auto oracle。已尝试隐藏启动 Debug Editor，但该进程通过 `SHGetFolderPath(CSIDL_PROFILE)` 定位到 `C:/Users/CodexSandboxOffline/OneDrive/AiluEngine`，与工作区 `C:/Users/BoomBac/OneDrive/AiluEngine` 不同，未能发布 automation session；临时 junction 创建被 Windows ACL 拒绝，因此没有产生 GPU capture。启动日志和一次性灰卡项目保存在 `.artifacts/out/editor/bin/x64/debug/M3_PhotometricCalibration_20261017T121113Z/`；共享 Editor 配置校验均未变化。sky、IBL、emissive、UnitlessLegacy 和世界尺度仍未由图像数值 oracle 定标。

M4: `cmake --build --preset build-debug` succeeded, including reflection generation and Engine/Editor link. This validates C++ compilation/link only; runtime HLSL compilation, GPU dispatch, RenderGraph barriers, numerical image comparisons, and visual acceptance were not run.

### M5 Upscaler exposure-domain integration
- `SuperResolutionFrameData` now carries a frame index, finite P, and explicit pre-exposed/scene-linear input domain.
- GPU Auto EV is a log2(P) GPU history. SR input normalization divides color by `2^EV`; output restoration multiplies by the same stable EV texture before PostprocessStack. No CPU readback was introduced.
- Manual/Legacy vendor input stays in its known pre-exposed domain. DLSS `preExposure` receives P and `useAutoExposure` remains enabled; when normalized GPU-Auto input is supplied, DLSS receives P=1.
- FSR dispatch receives the frame-data P. Its scene-linear GPU-Auto path gets P=1 after normalization; pre-exposed input gets actual P.
- SR destination is seeded from an upscaled valid input before vendor evaluation. On a failed evaluate, the seeded valid color remains available to the graph; GPU-Auto output restoration returns that fallback to the expected pre-exposed domain.
- Camera exposure-mode changes advance temporal reset revision. Existing `ResolveTemporalMode` resets history on DLSS/FSR/TAA/spatial mode, render-size, and availability changes. Legacy non-RenderGraph rendering retains P=1 and does not enqueue the RG SR bridge.
- Official SDK references checked: [Streamline DLSS programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md) documents auto-exposure behavior when no exposure resource is supplied; [FSR upscaler guide](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/super-resolution-upscaler.md) describes preExposure normalization. Installed Streamline headers expose the `DLSSOptions::preExposure` field.
- `cmake --build --preset build-debug` succeeded and linked Engine and Editor. FSR is disabled in this build configuration; vendor runtime, runtime HLSL compilation, resource-format support, RenderGraph capture/barriers, numerical comparison, and visual acceptance remain pending user validation.

### M6 Editor and regression implementation
- Camera Inspector now selects Legacy FOV versus Physical Focal Length, edits sensor/focal settings, edits exposure mode and physical camera controls, and shows vertical FOV plus setting-derived EV100/reference multiplier. Auto GPU EV is explicitly not represented as a CPU value.
- Postprocess Volume Inspector now exposes the auto-exposure calibration coefficient, histogram percentiles, min/max target EV, metering mode, and separate brighten/darken response speeds. New reflected fields retain constructor defaults for older archives; negative speed fields use the legacy shared adaptation speed until explicitly overridden.
- AutoExposure GPU histogram supports average and center-weighted sample weights; target EV is clamped and brighten/darken adaptation uses separate rates.
- Scene automation camera response adds camera-settings EV100/reference multiplier; component descriptors expose physical camera settings and PostprocessVolume auto-exposure settings for inspect/write.
- `cmake --build --preset build-debug` succeeded, including reflection generation and Engine/Editor link. One intermediate build caught an AddField forward-declaration arity mismatch; corrected before the successful build.
- GPU-resident diagnostic EV/histogram UI, automatic render/capture matrix, GPU timing/memory/stability report, Legacy visual regression and UI/Gizmo measurement checks remain unimplemented or unverified. M0–M5 acceptance gaps remain listed above; no runtime Editor or image acceptance was performed in this implementation turn.

## Blockers
M0 GPU baseline remains unverified: ToneMappingSmoke reaches ImGui initialization but fails loading its font (`Could not load font file!`). M3 directional/point-light and gray-card exposure checks pass; sky/IBL/emissive/UnitlessLegacy calibration remains pending. Editor automation read/write/readback was previously verified; the new Camera/Volume Inspector and expanded component-field automation still need live Editor interaction and scene save/load validation. M6 GPU diagnostics, matrix and profiling remain open.

## Next Step
下一步实现 editor-only 异步 GPU exposure diagnostics/histogram view 和可复现 capture matrix；用户自行验收 M0–M5 已实现路径，包括 M4/M5 域往返、M3 sky/IBL/emissive/UnitlessLegacy calibration 与其余历史缺口。随后在低端/普通 GPU 记录自动曝光耗时、显存和 ≥60s 稳定性；验收前不标记 M6 完成。

## Known Follow-ups
DOF、motion blur、镜头/光谱校准及 Local Exposure 按 README Non-goals 单独跟进。

## Outcomes
M3 remains partially GPU accepted: directional gray-card ±5% and lux scaling, point-light 2x/inverse-square, Manual Physical +1 EV ratio, Legacy invariance, and GPU Auto EV vs M1 CPU oracle passed; HDR sky/IBL/emissive/UnitlessLegacy checks remain open. M4 source implementation is present, but runtime/GPU/image acceptance is pending. M2 GPU acceptance, M1 scene/visual acceptance, and M0 GPU baseline remain unrecorded.

M6 implementation details above describe the independently observed source/build state. The user declared M6 complete on 2026-10-18; remaining M0–M5 evidence gaps and any M6 runtime/performance work are not independently re-audited here.

## Resume Protocol
阅读 README + progress + 当前 mX；阶段 Acceptance Criteria 全绿后才修改 Current Milestone，提交可复现实验记录。

M3 follow-up: the user-started Editor automation transport worked after elevated pipe access. `DeferredLightingPass` / `_CameraColorAttachmentA` produced 8 native DDS frames at 896x512 with contiguous engine frames 18804-18811. The live session was `C:/Users/BoomBac/OneDrive/AiluTestProj`, not the disposable M3 gray-card fixture; record this as transport smoke only. Capture files: `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PhotometricGrayCard_20261017/session_1/`. No scene settings were modified. M3 GPU gray-card acceptance and radiometric image analysis remain pending.

M3 GPU update: the expected disposable `M3PhotometricGrayCard` scene was opened and queried in the running Editor. Corrected only the temporary directional transform to identity because its original 180-degree X rotation sent the engine default `(0,-1,0)` light vector upward. The original runtime transform and camera were restored; the test scene was not saved. Captured 8 contiguous 896x512 native HDR frames at each 1000/2000 lux. Uniform gray-card RGB was 0.087890625 / 0.17578125 in R11G11B10_FLOAT; using the current 683 lm/W conversion yields 60.0293 / 120.0586 cd/m2 against 57.2958 / 114.5916 expected (+4.77%), with exact 2x response. Each sequence is byte-identical across frames; ToneMapping PNG visually shows the centered card. Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PhotometricGrayCard_20261017_radiometric/session_6-7/` and `..._visual/session_8/`. Directional gray-card tolerance and lux-scaling checks pass. Manual/auto exposure and sky/IBL/emissive/legacy checks remain pending; point-light ratios were later captured as noted below. M3 is not complete.

M3 point-light follow-up: 1000→2000 cd at 4 m doubled center 8x8 median HDR radiance (0.005533854→0.011067708). With range falloff disabled, 4→8 m at 1000 cd yielded 0.0055338541667→0.0013834635417, exactly 1/4; each sequence has 8 contiguous frames, and the inverse-square frames are byte-identical. Point absolute luminance is +5.55% versus the ideal Lambert estimate; the +/-5% criterion applies to the controlled directional gray card. Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PointLight_20261017/session_9-11/` and `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PointLight_InverseSquare_20261017/session_12-13/`.

The fixture contains no scene camera entity; M3 captures use the Editor-owned SceneView camera. The initial automation API lacked physical-setting control, so the SceneView automation was extended in this follow-up. GPU Manual Physical 1 EV/Legacy, Auto EV versus CPU oracle, and sky/IBL/emissive/legacy-domain captures remain open until rerun with the rebuilt Editor. The prior capture-only light/camera changes were restored; no scene was saved.

M3 automation implementation: `editor.get_scene_camera` now returns `physical_settings`; `editor.set_scene_camera` accepts partial values for all `PhysicalCameraSettings`, validates finite numbers and enum ordinals, applies them to the Editor-owned SceneView camera, and returns the sanitized readback. No Camera reflection or scene serialization was added. `cmake --build --preset build-debug` succeeded and linked `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`. Runtime RPC and GPU recapture require a new Editor process because the currently running PID 46632 predates the build; it remains open and untouched.

M3 automation and GPU retest update (2026-10-17):
- Added `physical_settings` readback to `editor.get_scene_camera` and partial validated writes to `editor.set_scene_camera`; rebuilt Editor was restarted by the user. RPC read/write/readback worked, and an invalid enum was rejected.
- Manual Physical +1 EV doubled the neutral ToneMapping gray-card ROI (0.087890625 to 0.17578125); Legacy Manual remained unchanged (0.0859375 at both 0 and +1 EV). Each result used eight contiguous, stable HDR frames.
- GPU Auto Exposure card-fill capture returned median EV 1.0271 versus ideal M1 CPU oracle 1.1015 EV (difference -0.0743 EV, within half a histogram bin of 0.2188 EV). The measured GPU card luminance (+4.77% versus ideal) predicts 1.0342 EV, leaving -0.0071 EV residual. Auto ToneMapping gray-card output median was 0.171875 against target 0.18. Auto history frames were contiguous; EV drift across the eight frames was 0.0047 EV.
- Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/captures/M3_PhysicalExposure_20261017/session_centered/` and `.../session_auto_cardfill/`. Final camera physical settings/pose and light rotation were restored by RPC, with no scene save.
- Remaining M3 work is sky/IBL/emissive/UnitlessLegacy brightness calibration. M3 is not complete; M2 GPU, M1 visual, and M0 GPU acceptance remain unrecorded.

## M7 Update (2026-10-18)

- The user declared M6 complete and requested M7. That is recorded as user acceptance; this turn did not independently
  re-run M6's editor or GPU acceptance.
- Implemented default-off reflected DOF settings, physical thin-lens CoC, bounded polygonal depth-aware gather, and
  PostprocessStack ordering before Bloom/ToneMapping. The pass reads post-upscale/TAA HDR color and camera device depth.
- Added Postprocess Volume UI and automation fields for enable, maximum CoC diameter, and aperture blades. Camera
  Inspector and SceneView expose focus distance/aperture in Legacy exposure mode because DOF also consumes them.
- `cmake --build --preset build-debug` passed, regenerated reflection, and linked Engine and Editor. This verifies C++
  compilation/link only; runtime HLSL compilation, RenderGraph capture, image analysis, and live visual acceptance were
  not run and remain for the user.
- Git status and diff checks were blocked by inaccessible loose objects in `.git` and read permission errors for the
  user's Git ignore configuration; no clean-diff claim is made.
- The user's runtime crash stack traced to `DepthOfFieldPass` calling `Material::SetInt` while the material's active
  shader was null. Replaced the unchecked `ResourceMgr::Get<Shader>` lookup with the standard `.alasset`
  `LoadAssetRef<Shader>` path and only creates the material after successful asset resolution and shader compilation.
  `Material::SetInt` was restored unchanged per user direction; the follow-up `build-debug` passed.
- The user reported DXC parse errors on the HLSL parameter name `point`; renamed it to `sample_point`. The local shell
  has no standalone `dxc` command, so runtime shader compilation still needs confirmation in Editor.
- The user reported visibly unnatural directional blur in the live SceneView. At the shown 40 px maximum CoC, the
  original 15 fixed gather offsets were too sparse; switched the shader to a 48-point Vogel disk with bilinear color
  sampling. This shader-only revision still needs runtime DXC compilation and the user's visual retest.
- M7 remains active until the user's live visual acceptance. Next step: enable DOF in a test Postprocess Volume, set
  SceneView aperture/focus, and inspect CoC sharpness, polygon bokeh shape, edges, HDR continuity, and upscaled views.

### M7 layered Bokeh quality follow-up — 2026-10-18
- Replaced the single gather with half-resolution signed CoC prefilter, separable near-support dilation,
  independent 256-sample polygon near/far gathers and full-resolution coverage composition.
- Debug build, runtime HLSL and controlled captures completed. All 35 final frames were written; multi-frame
  captures were contiguous. Zero CoC and f/128 match disabled DOF exactly; focused-checker interior RMS is zero.
- Near edge transition improved from 5 to 20 px. Bokeh interior variation decreased from 0.570 to 0.0147.
- TAA eight-frame worst adjacent RMS/P99 decreased from 6.705/36 to 3.018/12; residual fluctuation remains.
  Motion, vendor upscaling and GPU performance are not certified. User-scene visual acceptance remains separate.
- Evidence: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/dof_bokeh/20261018-094203/report.md.
  All shared Editor/config state was restored byte-for-byte and owned processes closed; no fixture scene saved.
