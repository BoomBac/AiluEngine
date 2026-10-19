# Execution Progress — AiluEngine Background Automation

> 这是**唯一**跨上下文的执行状态账本。由实施 Agent 在每个工作回合更新，不要将计划中的“预期”改写为“已验证”。

## Current Milestone

- **计划状态：Closed by user decision; deferred follow-ups**
- 当前实现覆盖：M1/M2/M3 已实现的部分与当前主机验证证据
- 上次更新：`2026-10-19`（关闭归档；剩余项转后续使用中排查）
- 负责人：实施 Agent / 用户指定
- 工作分支：`master`（保留用户已有工作树变更）
- 源码版本 / commit：`b6eeb9269362017134e9c8954029d3970e9e37c8`

## Progress

- [x] P00 在完整仓库调查工程生成/编译/测试脚本，保存真实 build 命令、测试项目路径、运行环境及当前 commit。
- [x] P01 建立正常编辑器启动、`--exit-after-frames`、`--plugin-smoke` 的未修改基线。
- [x] P02 实现 M1 CLI + hidden-compat + 自动化 Runner + 报告/退出码。
- [x] P03 完成 M1 异常/超时/并行与配置保护验证。
- [ ] P04 M1 Acceptance 全部通过并附证据。
- [x] P05 经用户明确授权越过未完成的 M1 gate，启动 M2 实现；M1 仍未 Accepted。
- [ ] P06 在 M2 Accepted 后启动 M3。
- [x] M3-I01 经用户明确授权越过未完成的 M1/M2 gate，实现 `--graphics none` CPU-only 路径与 `Cpu.*` 验收测试；M3 gate 保留未满足项。

### Baseline delta

首次执行结果：完整仓库为 CMake + Visual Studio 2026，preset `build-debug` 使用 `F:/AiluEngine/build/debug` 与
`F:/AiluEngine/artifacts`；Editor target 输出为
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`。当前源码相对快照新增
`AutomationBatchRunner.h/.cpp`，并修改 `ApplicationInitContext`、隐藏窗口标志、EditorApp 生命周期和 EditorMain CLI；
新增测试设施未复制 Named Pipe/RPC，仍复用 `EditorAutomationService`。
本轮 M2 增量新增无窗口 Runtime、独立 OffscreenOutput RT、DX12 `EndFrame` 命令、offscreen capture Runner、
`--scene`/`--fixed-delta` 参数及 Batch 下 ResourceMgr 只读退出边界。

## Decision Log

| ID | 时间 | 结论 | 证据/原因 |
| --- | --- | --- | --- |
| D001–D005 | 2026-10-19 | 初始架构决策 | 见 `README.md` |
| M1-I01 | 2026-10-19 | Batch 默认跳过自动插件加载、资源迁移、Pipe session 和 Editor/Engine 配置保存 | 保护真实工程与用户配置；AutomationService 仍初始化并由应用帧边界驱动 |
| M1-I02 | 2026-10-19 | Runner 首批测试为 `Automation.Ping`、`Automation.ListMethods`、`Automation.ProjectOpen`，故意失败/超时命名为 `Runner.*` | 保持 `Automation.*` 正向筛选可全通过，同时保留 Runner 自测覆盖 |
| | | | |

## Surprises & Discoveries

- 当前源码快照只有 `Engine/` 与 `Editor/`；没有提供完整构建与测试环境，**尚不能宣称任何测试通过**。
- 后续发现按事实追加：具体文件、触发条件、影响、验证方法及对应 commit。
- 首次真实 Batch 启动需要约数分钟建立 shader PDB cache；首次 30 秒观察窗口未进入 Runner，之后重试在 cache 部分建立后成功。`--timeout-seconds` 当前只约束 Runner，不覆盖 Editor/GPU 初始化。
- `Test.exe` 的 Automation core/read-model/adapter/transport/command/AI 分组全部通过；总程序仍有 6 个既有 Widget/WidgetAsset 序列化失败，不能作为 M1 全量回归通过。

## Validation

| Milestone | 构建 | 自动化用例 | 回归/隔离 | 退出码与报告 | 结果 |
| --- | --- | --- | --- | --- | --- |
| M1 | `build-debug` / Test target 通过 | list-tests 及 `Automation.*` 通过；failure/timeout/no-match 已验证 | 两进程、配置 hash、正常 bounded Editor 和旧 CLI 通过 | 0/1/2 已验证；初始化失败报告已生成 | In Progress |
| M2 | `build-debug` 通过 | 两个用户工程场景各自 `Rendering.*` 8/8 通过；含 FinalBlit/ToneMapping、pending capture、128 帧压力和同进程 OffscreenResize | VolumetricFog 323 帧、DDGI 284 帧，均无窗口/SwapChain/Present；320x180/800x450/1280x720/1491x902 输出尺寸已核对；hidden-compat/offscreen 同场景对比 RMS=0.638、P99=2 | offscreen reports、resize result、RenderGraph manifests、PNG/diff、V08 comparison 和 golden 契约报告已生成 | Partial; live-object、完整 Debug Layer、Shader smoke 有界完成、M1 GUI gates remain |
| M3 | `build-debug` 通过 | `Cpu.*` 4/4；`Rendering.*` 能力拒绝 8 skipped/整体 failed；三个 ResourceMgr 失败注入阶段均结构化失败 | 正式项目/Library/DDC/config hash 0 差异；20 次顺序 + 2 进程并行通过；hidden-compat Automation 3/3 | CPU report 0 window/0 GPU context/0 SwapChain/0 Present；GPU reject exit 1；failure injection exit 2 | Implemented on current host; formal gate Partial |

### 证据模板（复制并填入每次验证）

```text
Milestone / testcase:
Host OS / GPU / driver / build configuration:
Repo commit:
Exact command:
Expected:
Actual exit code:
Report/artifact paths:
Pass/Fail/Blocked:
Failure analysis and next action:
```

### M1 evidence (2026-10-19)

```text
Host: Windows, Debug, D3D12; repo commit: b6eeb9269362017134e9c8954029d3970e9e37c8
Build: cmake --build --preset build-debug -- /m:1                         -> exit 0
Test target: cmake --build --preset build-debug --target Test -- /m:1     -> exit 0
Fixture: F:\AiluEngine\artifacts\out\editor\bin\x64\debug\diagnostics\ui-scissor-regression-20261014\stable-project\Validation.ailuproject
Editor: F:\AiluEngine\artifacts\out\editor\bin\x64\debug\Editor.exe

M1-V02 list-tests: exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m1_list\result.json
M1-V03 Automation.*: exit 0; 3 passed; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m1_automation\result.json
M1-V04 deliberate failure: exit 1; assertion_failed with expected/actual; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m1_failure\result.json
M1-V06 zero match: exit 2; no_tests_matched; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m1_no_match\result.json
M1-V07 deliberate timeout: exit 1; timeout/test_timeout; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m1_timeout\result.json
M1-V11 parser checks: no project, offscreen, unknown option -> exit 2
M1-V08 parallel: two Automation.* processes -> exit 0/0; isolated reports under Temp\background_automation_m1_parallel_a and _b
M1-V09 Batch side effects: main_widget/layout/EngineConfig/EditorConfig SHA-256 unchanged; report under Temp\background_automation_m1_side_effect\result.json
M1-V01 bounded normal Editor: --exit-after-frames 8 -> exit 0; baseline files restored after run
M1-V10 legacy CLI: --exit-after-frames -> 0, --plugin-smoke -> 0, profiler-disabled --profile-capture -> expected 1
M1-V12 initialization failures: missing project -> 2/report missing_project; nonexistent project -> 2/report initialization_failed
M1 core regression: Automation core 5/5, read-model 4/4, adapter 3/3, transport 5/5, command 6/6, AI 3/3 passed.
Interactive normal-Editor visual/GUI acceptance is still not covered; the baseline above is bounded command-line startup. The hash scan covers the key files above, not every project/plugin path.
```

### M2 evidence (2026-10-19, explicitly out of order)

```text
Host: Windows, Debug, D3D12; fixture: Validation.ailuproject; Editor: F:\AiluEngine\artifacts\out\editor\bin\x64\debug\Editor.exe
Build: cmake --build --preset build-debug -- /m:1 -> exit 0
Command: Editor.exe <fixture> --batch --graphics offscreen --run-tests Rendering.* --fixed-delta 0.0166667 --report <report> --timeout-seconds 30
Report: C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_offscreen_final\result.json
Result: exit 0; OffscreenFrameBoundary passed; OffscreenCapture passed
Capabilities: window_bound=false, swapchain_count=0, present_count=0, gpu_frame_count=33
Capture manifest: C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_offscreen_final\captures\session_1\capture.json
Capture: PNG completed, 1491x902, one frame, engine_frame=8; source fixture is empty so this run's scene capture is black.
Synthetic readback: C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_synthetic_capture\captures\session_1\offscreen_0000.png
Synthetic result: exit 0; 1491x902 non-black cyan clear/readback image; image visually inspected; proves RT clear -> GPU submit -> readback -> PNG.
Read-only check: Engine/Res/Materials/M_Default.alasset and StandardPBR.alasset SHA-256 unchanged across the final smoke.
Evidence level: Debug build, capture transport, manifest integrity, synthetic image inspection. No real-scene parity or live GUI acceptance.
```

### M2 follow-up validation (2026-10-19)

```text
Build: cmake --build --preset build-debug -- /m:1 -> exit 0
Single frame: Rendering.OffscreenFrameSingle -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_frame_single\result.json
Single frame result: frame_count=1, window_bound=false, swapchain_count=0, present_count=0.
Stress: Rendering.OffscreenFrameStress -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_frame_stress\result.json
Stress result: frame_count=128, gpu_frame_count=127, window_bound=false, swapchain_count=0, present_count=0.
Pending capture: Rendering.OffscreenCapturePending -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_capture_pending\result.json
Pending capture result: non-terminal capture state observed before completed; capture manifest completed; 1491x902 PNG written.
Rendering suite: Rendering.* -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_rendering_all\result.json
Rendering suite result: 4/4 passed before the single-frame follow-up; frame_count=188, gpu_frame_count=187, window_bound=false, swapchain_count=0, present_count=0. The follow-up single-frame case passed independently after the suite.
Deterministic unsupported graphics: `--graphics none` -> exit 1; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\background_automation_m2_graphics_none\result.json; status=error, code=invalid_arguments, message=unsupported_graphics_mode, no tests executed.
User-provided project: `C:\Users\liusongchen_e\OneDrive\AiluTestProj\empty.json`; explicit scene `Assets/scene/NewScene.almap` contains StaticMesh and Light entities. `Rendering.OffscreenFrameStress` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_newscene_stress\result.json; frame_count=128, window_bound=false, swapchain_count=0, present_count=0. `Rendering.*` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_newscene_rendering\result.json; 5/5 passed, frame_count=198, window_bound=false, swapchain_count=0, present_count=0, capture manifests completed at 1491x902.
The captured PNGs for this run were visually inspected and are full cyan because the current synthetic capture test clears the target before capture; this is transport evidence, not real-scene image evidence. `empty.json` remained 450 bytes with its original timestamp. The external project already had a dirty Git worktree during inspection, so existing project changes cannot be attributed to this run; no project file was intentionally edited.
Real-scene capture implementation: added `Rendering.OffscreenSceneCapture` using the existing RenderGraph capture bridge on `FinalBlit`, requiring an active scene with enabled `MainCamera` and a ready StaticMesh VertexBuffer. `NewScene.almap` was rejected as `scene_camera_unavailable`; `Surfel/01_SinglePlane_DirectLight.almap` was also rejected because it has no enabled MainCamera. `VolumetricFogBaseline.almap` now passes the real-scene capture: report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_scene_capture_graph_final\result.json; manifest C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_scene_capture_graph_final\graph_capture\session_1\capture.json; 1491x902 PNG is non-black and was visually inspected. The separate `Rendering.OffscreenGraphCapture` on `ToneMapping` also produced a visible Cornell image at C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_graph_capture_authorized\graph_capture\session_1\offscreen_graph_0000.png. The original direct `ImageCaptureService::CaptureOnce` path was reading the target from before the current RenderGraph frame and remains a diagnostic timing failure; the default real-scene acceptance now uses FinalBlit bridge capture.
Synthetic regression after the new scene-capture code: `Rendering.OffscreenCapture` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_synthetic_capture_regression\result.json; manifest completed at 1491x902 with window_bound=false, swapchain_count=0, present_count=0.
Full VolumetricFog regression: `Rendering.*` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_fog_rendering_all\result.json; 7/7 passed, frame_count=261, gpu_frame_count=260, swapchain_count=0, present_count=0. Both ToneMapping and FinalBlit graph sessions completed in the same run.
Cross-scene validation: `DDGITest/01_cornell_bleed.almap` `Rendering.OffscreenSceneCapture` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_ddgi_scene_capture\result.json; 1491x902 FinalBlit PNG contains visible green geometry. The same scene `Rendering.OffscreenFrameStress` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_ddgi_frame_stress\result.json; frame_count=128, window_bound=false, swapchain_count=0, present_count=0.
Full DDGI regression: `DDGITest/01_cornell_bleed.almap` `Rendering.*` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_ddgi_rendering_all\result.json; 7/7 passed, frame_count=266, gpu_frame_count=265, swapchain_count=0, present_count=0. FinalBlit and ToneMapping PNGs are both non-black and were visually inspected.
Golden contract smoke: report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_golden_contract\result.json; a generated test baseline was used only as a comparator fixture, not approved as a long-term golden. Correct image passed; RGB pixel difference failed with `changed_pixels=72171` and `changed_ratio=0.0536634440791088`, missing image failed with `golden_missing`, and 1x1 image failed with `dimension_mismatch`; incorrect diff artifact C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_golden_contract\incorrect_diff.png was visually inspected.
Missing-scene injection: explicit nonexistent scene `Assets/Scenes/Missing_M2_Test.almap` exited `-1073741819` (`0xC0000005` access violation) without a structured report; this is a failure of diagnostic handling, not a pass. No project file was intentionally modified.
Evidence level: exact logical-frame smoke at 1/8/128, 128+ frame no Present stress, capture pending/complete transport, real-scene ToneMapping/FinalBlit PNG capture, synthetic PNG transport, cross-scene full-suite regression, and comparator contract behavior. This still does not prove multi-size resize/recreation, live-object stability, GPU debug-layer cleanliness, shader-compilation injection, normal Editor GUI acceptance, or M1 completion.
```

### M2 failure repair validation (2026-10-19)

```text
Root cause: SceneMgr::OpenScene() returned a null Scene when ResourceMgr could not load the requested path. EditorApp continued registering/rendering the pipeline, so the next frame dereferenced the missing scene/camera path and exited with 0xC0000005 before the batch report was written.
Fix: EditorApp now treats a null OpenScene result as initialization failure, records scene_not_found plus the requested path, and calls the existing Finalize() path before returning. EditorMain forwards the specific initialization code/message to the structured startup report.
Build: cmake --build --preset build-debug -- /m:1 -> exit 0; reflection generation and Editor link completed.
Missing-scene retry: nonexistent Assets/Scenes/Missing_M2_Test.almap -> exit 2; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_missing_scene_fixed\result.json; status=error, code=scene_not_found, no crash and no project modification.
VolumetricFog regression after fix: Rendering.* -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_fog_rendering_fixed\result.json; 7/7 passed, frame_count=309, gpu_frame_count=308, swapchain_count=0, present_count=0. FinalBlit/ToneMapping images were visually inspected and non-black.
DDGI regression after fix: Rendering.* -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_ddgi_rendering_fixed\result.json; 7/7 passed, frame_count=288, gpu_frame_count=287, swapchain_count=0, present_count=0. FinalBlit/ToneMapping images were visually inspected and contain visible geometry.
The previous black direct target readback is no longer used by the registered real-scene acceptance; OffscreenSceneCapture resolves through the RenderGraph FinalBlit bridge, which passed both scene regressions. The direct ImageCaptureService path remains unregistered diagnostic code and is not claimed as an independent acceptance path.
```

### M2 remaining-test validation (2026-10-19)

```text
Implementation additions: `--render-size WxH` now controls offscreen output dimensions; `Rendering.OffscreenResize` recreates the offscreen target at 640x360, 1280x720, 896x512 and 1491x902 with GPU waits and camera target rebinding. Offscreen output targets now use linear UNORM to match the hidden-compat SwapChain format; this removed the prior color-space mismatch. Batch hidden-compat skips the interactive EditorLayer so the deterministic MainCamera is not overwritten by SceneView UI logic; normal non-batch Editor behavior is unchanged.
VolumetricFog after all additions: `Rendering.*` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_fog_rendering_srgbfix\result.json; 8/8 passed, frame_count=323, gpu_frame_count=323, swapchain_count=0, present_count=0.
DDGI after all additions: `Rendering.*` -> exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_ddgi_rendering_srgbfix\result.json; 8/8 passed, frame_count=284, gpu_frame_count=283, swapchain_count=0, present_count=0.
Multi-size scene capture: VolumetricFog `OffscreenSceneCapture` passed at 320x180, 800x450, and 1280x720; PNG dimensions were independently verified. The existing 1491x902 full-suite capture was also verified. Reports: C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_fog_offscreen_320x180\result.json, C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_fog_offscreen_800x450_srgbfix\result.json, C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_ailutestproj_fog_offscreen_1280x720\result.json.
M2-V08 same-scene comparison: hidden-compat Batch MainCamera/FinalBlit at 800x450 versus offscreen `--render-size 800x450`; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_v08_hiddencompat_offscreen_compare\result_800x450_srgbfix.json; size match, RMS=0.6379168629646301, P99 absolute difference=2, max=7, mean=0.31162869930267334. Recorded acceptance tolerance is RMS <= 1 and P99 <= 2; diff artifact C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_v08_hiddencompat_offscreen_compare\diff_800x450_srgbfix.png was generated and visually inspected.
Hidden-compat Automation after Batch UI change: exit 0; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_hiddencompat_automation_fog_batch_no_ui\result.json; Automation.Ping/ListMethods/ProjectOpen 3/3 passed, window_bound=true, swapchain_count=1, present_count=4.
CLI parser negative: `--render-size 0x0` -> exit 2; report C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_invalid_render_size\result.json; structured `invalid_arguments`.
Existing Test.exe regression: Automation core/read-model/adapter/transport/command/AI and animation groups passed; the complete Test.exe run still reports six pre-existing Widget/WidgetAsset serialization failures.
ToneMappingSmoke: target build passed, but the first isolated run spent more than five minutes in global shader/PDB cache warmup without reaching the shader/debug-layer assertions; a second run after cache warmup still advanced only about one PDB every 45 seconds and also failed to reach the assertions within the bounded observation window. Both runs were terminated after observing continued progress to avoid unbounded GPU/memory use. Directories/logs: C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_tonemapping_smoke_20261019 and C:\Users\liusongchen_e\OneDrive\AiluEngine\Temp\m2_tonemapping_smoke_20261019_retry. This is not a PASS and leaves independent Debug Layer/forced-shader-error evidence incomplete.
```

## Blockers

- B001：完整构建环境已获得；preset 构建树在 `F:`，需授权访问。首次 GPU shader cache 初始化耗时较长。
- B002：正常 Editor 的交互式视觉/GUI 基线和全量工程/插件副作用扫描仍未完成，因此 M1 仍不能标记 Accepted；本轮已完成 bounded Editor、旧 CLI 和初始化失败报告。
- B003：M2 已完成两个真实场景的 1/8/128 逻辑帧、128+ 帧无 Present 压力、RenderGraph capture、同进程多尺寸 resize 和多进程多尺寸 PNG 证据；V08 同场景比较已在记录容差内通过。GPU live-object、独立 Debug Layer 结果和长期 golden 基线仍未完成。
- B004：最终 offscreen smoke 的日志仍包含空场景 HZB/资源 ready 警告；未将这些 warning 误判为 M2 关键错误，但需在真实场景与 Debug Layer run 中分类。
- B005：当前 Runner 在本帧 EndFrame 前写报告，128 逻辑帧报告的 `gpu_frame_count` 为 127，综合 188 逻辑帧报告为 187；需要补充最终 GPU drain/fence 完成后的计数证据，不能仅凭逻辑帧计数宣称最后一帧已完成。
- B006：旧 direct `ImageCaptureService::CaptureOnce` 在 `before_update` 读取 OffscreenOutput 时会看到当前 RenderGraph 尚未写入的黑 target；当前注册的真实场景验收使用 FinalBlit bridge，两个场景和多尺寸均通过。direct path 仍未作为独立验收入口重新启用。
- B007：已修复并复测缺失场景错误处理；现在返回结构化 `scene_not_found` 报告和退出码 2。Asset 缺失路径已有证据；Shader 编译错误/Debug Layer smoke 未在有界时间内完成，设备初始化失败仍只有参数级 unsupported 证据。
- B008：本轮 M3 在当前可用 D3D12 主机完成了真实 `none` 路径，但没有物理无 D3D12 设备的验收主机，因此 A1/A6 不能宣称完成。
- B009：M1/M2 formal Accepted gate、Scene/Prefab RoundTrip、坏数据独立 fixture 和完整 GPU 回归仍未完成；M3 结果保持 Partial，不改写前置里程碑状态。
- B011：ResourceMgr `after_asset_database`、`after_handlers`、`after_cpu_metadata` 三个失败注入均 exit 2、结构化 initialization_failed、0 项目/Library/DDC/config hash 差异、0 残留 Editor。
- B010：已修复 hidden-compat shutdown 的 2170。根因是 `LayerStack/ImGUILayer` 在 GPU drain 前销毁，DXGI Debug Layer 在 `ImGui_ImplDX12_DestroyRenderBuffers` 触发 `0x0000087A`；同时修复 `WM_DESTROY` 后的 DragDrop/OLE/旧 HWND 清理。修复后 3 次 `Automation.Ping` 与 `Automation.*` 均真实退出码 0，`crash.log` 的 0x87A 计数保持不变，Editor 残留数为 0。
- 不得把待调查项标记为通过。

## Deferred Follow-ups

以下项目未在本计划关闭前继续推进，后续仅在实际使用中遇到问题时单独排查：M1 交互式 GUI/副作用全量验收、M2 live-object/完整 Debug Layer/Shader smoke、真实无 D3D12 主机、Scene/Prefab RoundTrip 与坏数据独立 fixture。

## Known Follow-ups

- `--listen` 常驻后台与会话 discovery 应待一次性 Batch 稳定后单独评估。
- GPU CI / Session 0 / RDP 运行策略须实机验证；禁止推测所有无桌面机器都能工作。

## Outcomes

- 计划关闭：用户明确要求将整个计划移动到完成，未完成验收项保留为 deferred follow-ups，不宣称为已通过。
- 当前：M1 核心实现、启动失败报告、bounded Editor/旧 CLI 与正向/失败/超时/零匹配验证已完成，剩余验收项见 B002。
- M1：已交付 CLI、hidden-compat、Runner、报告和退出码；交互式 GUI 与全量隔离证据待补。
- M2：实现了真无窗口/无 SwapChain/无 Present 的 GPU frame boundary、独立 linear/UNORM RT、synthetic capture、fixed-delta、RenderGraph pass capture、同进程 resize 和可配置多尺寸输出；两个用户工程场景在修复后各自 `Rendering.*` 8/8 通过，V08 同场景比较在记录容差内通过，缺失场景已转为结构化失败；当前仍为 Partial，live-object、独立 Debug Layer/Shader smoke 和 M1 GUI 回归待补。
- M3：已实现 CPU-only Runtime/ResourceMgr 分阶段初始化、GPU-backed Load 能力拒绝、CPU 场景文档兼容检查、失败回滚注入和正式 hash audit；当前主机 `Cpu.*` 4/4、20 次顺序 + 2 进程并行 22/22 通过，三个失败阶段均安全退出，`Rendering.*` 在 none 下非零拒绝。A1/A5/A6 作为 deferred follow-ups 保留。
- Shutdown fix：新增 Runtime GPU/render-work wait，并修复 WinWindow `WM_DESTROY` 的 DragDrop/OLE/句柄生命周期；`Temp/m3_shutdown_fix_ping/result.json` 和 `Temp/m3_shutdown_fix_automation/result.json` 均通过。

## Resume Protocol

新 Agent 开始前按顺序读 `README.md` → 本文件 → 当前 `mX-*.md`；复核仓库差异，继续 `Next Step`。当前 Milestone 失败、证据不足或构建不可用时保留 Blocked，不越过门禁。完成一个 Milestone 后在同一次提交中更新本文件与相应验收记录。
