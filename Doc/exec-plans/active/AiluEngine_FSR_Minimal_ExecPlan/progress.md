# FSR Minimal Integration — Progress

> 本文件是唯一进度记录。实施者每次编辑后保留事实、决策和测试证据，不在其他文件追踪运行时状态。

## Purpose

为 AiluEngine FSR SR 最小接入保存可恢复的执行状态。

## Scope

M1 SDK Bootstrap → M2 SR Render Path → M3 UX & Robustness。

## Non-goals

不在此文件重复 Milestone 详细设计；不拓展到 FG / FSR4 / 画质优化。

## Architecture Constraints

参照 `README.md`；特别是无侵入 DX12 主设备、与 DLSS 并存、RG 状态和逐里程碑验收。

## Baseline

`AiluEngine_Source(20261013-031318).zip`，Windows 工程与 SDK DLL 未随该包提供。

## Target Outcome

三个里程碑逐一通过，可恢复验证日志，代码无退化。

## Relevant Files

`README.md`、`m1-sdk-bootstrap.md`、`m2-render-path.md`、`m3-stability-and-editor.md`。

## Milestones

| 阶段 | 状态 | 验收结果 |
| --- | --- | --- |
| M1 SDK Bootstrap | COMPLETE | PASS |
| M2 SR Render Path | IMPLEMENTED; ACCEPTANCE PENDING | PARTIAL |
| M3 UX & Robustness | BLOCKED BY M2 ACCEPTANCE | NOT RUN |

## Current Milestone

`M2` SR Render Path；M1 SDK Bootstrap 已完成。

## Progress

- [x] 锁定 SDK 版本，取得匹配的签名 DLL / headers
- [x] M1 编译与运行时探测
- [x] M2 Camera 解析、共享 RenderGraph Pass 与 DX12 dispatch implementation
- [x] Scene View 相机面板 FSR 选择、Quality 行与 active/inactive 状态更新已实现
- [ ] M2 FSR Quality 场景出图与 GPU 验收
- [ ] M3 配置、切换、回归验收

### M2 FSR Overlay Depth Regression (2026-10-13)

- 后台 Editor 的 `empty` 场景（3 entities，scene GUID `3df3d38a-e4cc-49e1-97a0-82d8e38d0e44`）已复现用户报告：开启 FSR 移动相机后，实际立方体旁的网格与坐标轴被偏移的旧轮廓遮挡。
- 根因是 `D3DConvertUtils::ConvertToD3D12DepthStencilDesc()` 在 `ZTest: Always` 时关闭 DepthEnable，忽略 `ZWrite: On`；`PostprocessOverlayDepth` 的 fullscreen `SV_Depth` 未写入，输出继续携带池化纹理的历史场景/坐标轴深度。修复为请求 depth writes 时保持 DepthEnable。另补齐 FSR context 的 `FFX_UPSCALE_ENABLE_DEPTH_INVERTED`，与当前 Reverse-Z 源数据一致，不启用 Infinite Depth。
- 临时 R32F 深度读回 pass 仅用于诊断，现已完整移除。before/after 各 64 连续帧：before 深度唯一帧数为 1，轮廓中心横向移动为 0；after 唯一帧数为 64，中心移动 189.107 px，最大深度由 0.997487（历史轴线残留）降至 0.001233807（当前立方体），确认深度按本帧相机更新。
- 最终正常渲染路径（无诊断 pass）：FSR Off 与 FSR Quality 各抓取 128 连续 FinalBlit PNG，均为 896×512，无 engine-frame gaps；FSR 输入 597×341、输出 896×512，FSR active=true。完整运动后的停稳尾段分别为 39/64 帧；抓帧负载下观察帧率约为 12/8 fps、控制更新约 3.4/3.3 Hz，不代表正常运行性能。逐帧抽样/对比图确认偏移网格遮挡消失；另有 TAA 恢复的 3 连续帧与 fsr_active=false/aa_mode=2 记录。
- 增加 `editor.set_scene_camera(fsr_enabled)` 与 getter 的 FSR active/render size，支持后台复验；`Test/FsrDepthEditorAcceptance.mjs` 使用既有隐藏进程 runner，保存相机/实体/运动观察记录并恢复相机，不保存场景。配置 restoration 全部通过。
- `D3DDepthStencilSmoke` PASS（PATH 包含 Editor runtime DLL 目录）；`cmake --build --preset build-debug` PASS；`node --check Test/FsrDepthEditorAcceptance.mjs` 和 `git diff --check` PASS。新增 smoke 初次 namespace/非导出构造函数链接失败已修正，通过编译现有 `PipelineState.cpp` 解决，不扩张引擎公共导出。
- 证据均位于实际 `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe` 旁：`fsr_depth_before_20261013_4`、`fsr_depth_after_20261013_1`、`fsr_depth_final_20261013_2`；最终目录含 `analysis.json`、`before_after.png`、原始 PNG/DDS、camera observations、capture manifests 和 restoration records。早期 `before_1/2/3` 的 DSV 直读不支持/超时不算完整 depth capture。
- 边界：本次确认该 SceneView FSR 遮挡回归已修复，未完成 RenderDoc/PIX barrier 检查、独立 MV 方向/动态物体、Camera Cut reset、DLSS 回归或长期 resize/context 回收验收；M2 整体验收仍 PARTIAL，M3 未启动。前后运动使用相同目标轨迹，但实时插值和抓帧负载不同，不将像素差当作画质等价指标。

### M2 Context Creation Fix (2026-10-13)

- 用户实际选择 FSR 后报告 `context creation failed ... (unknown descriptor type)`。源码将 `ffxCreateContextDescUpscaleVersion` 扩展作为 `ffxCreateContext` 的入口，错误链为 Version → Override → Backend → Upscale。
- 修正 `FsrRuntime::Evaluate()` 的调用入口与链顺序为 Upscale → Backend → Override → Version；保留 SDK 2.3.0 API version、HDR/Auto Exposure flags 和查询得到的 3.1.5 provider ID。
- 本机独立 DX12 SDK smoke 使用 Editor 输出目录的真实 DLL：旧链稳定返回 `2`（UNKNOWN_DESCTYPE），新链返回 `0`（OK）；随后查询实际 context provider 为 `3.1.5` / `17700776140660019205`，销毁返回 `0`。源码、可执行文件与日志位于 `artifacts/diagnostics/fsr-context/`；本次未运行 Editor/capture，不能据此认定 M2 图像/GPU acceptance 通过。
- `cmake --build --preset build-debug` PASS；`git diff --check` PASS（仅现有 LF/CRLF 提示）。构建后的 engine 和 Editor 输出目录 `Engine_d.dll` SHA-256 一致。重启更新后的 Editor，再选择 FSR 验证实际 dispatch 与画面。

### M2 Implementation

- `RenderPipeline::Render()` now resolves FSR/DLSS/fallback before `PrepareRenderFrame()`; FSR Quality render dimensions come from the SDK query and do not multiply `RenderScale()`.
- The existing SR feature/pass now serves DLSS and FSR, declares SRV/UAV usage, and snapshots FSR camera/frame parameters before the native callback. The existing spatial fallback and `_upscaled_color` handoff remain in place.
- Camera FSR request is mutually exclusive with DLSS; resolved FSR dimensions drive jitter and `RenderSize()`. TAA is skipped and its history invalidated for both SR modes.
- `FsrRuntime` creates a provider-overridden 3.1.5 Upscale context per camera key, dispatches HDR Color/Depth/Motion Vectors into the output UAV, marks reset on first dispatch or history revision changes, and falls back on a later frame after dispatch failure.
- Scene View camera settings now offer Bilinear / Point / DLSS / FSR. FSR and DLSS requests stay mutually exclusive; the shared quality row lists FSR's currently supported Quality mode or DLSS's three modes and reports active dimensions or the standard-rendering fallback.

### M1 SDK Lock

- SDK: AMD FidelityFX SDK `v2.3.0`，Upscaler provider 严格选择 `3.1.5`；DLL 自身文件版本不能代表选中的 provider 版本。
- Runtime files: `amd_fidelityfx_loader_dx12.dll` (`2.3.0.2740`, SHA-256 `E2D85AA05A9BD9ED8B38935FDF5199372CCA6F74C12015143BB6F945EE1608AA`)；`amd_fidelityfx_upscaler_dx12.dll` (`4.1.1.2740`, SHA-256 `D0DCCCC74A43C44BA435B7A369B456E0970D8A4464E4BD683119B374F2C9FB46`)。两者 Authenticode 均有效，签名者为 Advanced Micro Devices。
- Release package `FidelityFX-Samples-v2.3.0-prebuilt.zip` SHA-256: `F90890B9323BB2F4F2404AC4CDC9395E8495ECDAC6F7AA0BCDF1AD1848422273`。匹配源码 tag archive SHA-256: `E7F274930086405071C01B4E7E1DE8CD3BB9FBCB987D711EBF65169464C28455`。仅将所需 API/upscaler headers 和上述两个 DLL 暂存于本机 Temp SDK root；没有把 SDK/vendor 二进制或头文件复制进仓库。
- 暂存 SDK 子集（8 headers + 2 DLLs）SHA-256 tree manifest: `e4c177831a63687b65f218087775bccfe8803ee1502f6523d8a765c2b239a1de`。

### M1 Implementation

- 新增 `FsrRuntime` PIMPL；SDK 关闭时保留可编译 stub。SDK 启用时从 Engine 模块旁的 `FSR` 子目录按绝对路径加载 DLL，离线检查 Authenticode，只动态解析 SDK C API，并对 D3D12 device 查询 Upscale providers；按返回的名称选择 `3.1.5`，复制名称并记录 provider ID。
- `D3DContext` 在 DX12 pipeline 初始化后探测 FSR，GPU idle 后、原生设备释放前关闭 FSR runtime。FSR 不代理或替换 DX12 device/factory/swapchain，也不改 Streamline 路径。
- CMake 增加默认关闭的 `AILU_WITH_FSR` 和 SDK root 配置；默认从 `Engine/Ext/Packages/fidelityfx-sdk-2.3.0` 读取 SDK，启用时检查所需 headers/DLLs，并在 Engine 后构建步骤将两个 DLL 放到应用输出目录旁的 `FSR` 目录。
- 架构说明已同步至 `Doc/architecture/rhi-dx12.md`。

## Decision Log

- 2026-10-13：完成 M1，选用官方 FidelityFX SDK v2.3.0 headers 与签名 runtime；使用 provider 查询名称选择 FSR 3.1.5，不硬编码 provider ID。
- 2026-10-13：FSR runtime 作为 `D3DContext` 拥有的可选 DX12 helper；只在 DX12 pipeline 建立后探测，在 GPU idle 后先于 native device teardown 关闭。
- 2026-10-13：M2 复用一个 SR RenderFeature；`Camera::FsrSR(bool)` 是当前 development switch，启用时互斥清除 DLSS 请求，Quality render size 由 SDK query 获取。
- 2026-10-13：尺寸变化时将旧 FSR context 保留到 runtime shutdown（其发生在 GPU idle 后）再销毁，避免 GPU 仍有工作时释放 SDK 资源；多次 resize 的回收策略留给 M3。
- 2026-10-13：本机 FidelityFX 2.3.0 与 NVIDIA Streamline 2.14.1（含 DLSS runtime）均使用 ignored `Engine/Ext/Packages/` 子集；版本、来源和树校验值由 `Scripts/packages.json` 锁定，`Scripts/setup_ext.ps1` 可验证/恢复；CMake SDK root 默认指向对应包目录。
- 2026-10-13：按用户要求将 FSR 接入 Scene View camera upsampling selector；M2 UI 只暴露当前 FSR Quality 能力，不让 Balanced/Performance 选项误写到 DLSS 或显示尚未支持的 FSR 档位。

## Surprises & Discoveries

- v2.3.0 upscaler DLL 文件版本为 `4.1.1.2740`，同时暴露 FSR 3.1.5 provider；因此 DLL file version 不能用于判断 FSR provider 版本。
- 官方预编译 release 包包含签名 DLL，但缺少本集成需要的公开 API headers；headers 从相同 `v2.3.0` tag 的源码包提取。
- M2 SDK-on 路径使用 SDK query 得到 Quality 输入尺寸；SDK-on Debug 全量构建现已通过，FFX descriptor 链、Streamline/DLSS 集成均完成编译与链接；该次 Editor 首帧运行时受独立 ECS 崩溃阻断；后续 empty 场景后台抓帧已不复现，见本文件的 Overlay Depth Regression 记录。
- `StreamlineDx12BridgeSmoke` 直接编译 bridge 源文件而不链接 Engine；FSR-off 构建需在 bridge 中编译掉 FSR runtime 调用，避免该 standalone target 引入新的链接依赖。

## Validation

| 日期 | Milestone | 环境 / 命令 / 场景 | 结果 | 证据路径 |
| --- | --- | --- | --- | --- |
| 2026-10-13 | M1 | `cmake --build --preset build-debug`，`AILU_WITH_FSR=OFF` | PASS；完整 Debug preset 构建 | `F:/AiluEngine/build/debug` |
| 2026-10-13 | M1 | Debug `Engine`，`AILU_WITH_FSR=ON`；`cmake --build --preset build-debug --target Engine -- /m:1` | PASS；SDK API 编译、链接和 post-build DLL 部署 | `F:/AiluEngine/build/debug` |
| 2026-10-13 | M1 | `cmake --build --preset build-release`，`AILU_WITH_FSR=OFF` | PASS；完整 Release preset 构建 | `F:/AiluEngine/build/release` |
| 2026-10-13 | M1 | Release `Engine`，`AILU_WITH_FSR=ON`；`cmake --build --preset build-release --target Engine -- /m:1` | PASS；SDK API 编译、链接和 post-build DLL 部署 | `F:/AiluEngine/build/release` |
| 2026-10-13 | M1 | Hidden DX12 Editor startup + `RunDescriptorPageGrowthSmoke.ps1`，GTX 1050，FSR enabled | PASS；provider `3.1.5` ID `17700776140660019205`；smoke exit 0、`D3D12_ERROR_CORRUPTION=0`、正常 `D3DContext Destroy` | `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/fsr_m1_probe_3f88b1c919074c6c80837a92c9c218a3` |
| 2026-10-13 | M1 | 同一 hidden startup smoke，分别移除 loader / upscaler DLL | PASS；两种缺失情形均记录 FSR unavailable，Editor smoke exit 0 | `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/fsr_m1_missing_amd_fidelityfx_loader_dx12_23649bf8e90c4fd1a026ac03f9dfe288`；`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/fsr_m1_missing_amd_fidelityfx_upscaler_dx12_3b303d760f224b4bbd702ceabc2b5326` |
| 2026-10-13 | M1 | `git diff --check` | PASS；无 whitespace errors | workspace |
| 2026-10-13 | M2 | `cmake --build --preset build-debug` | PASS；FSR OFF 完整 Debug preset；Engine、Editor 与 smoke target 均完成构建。未运行 smoke executable | `F:/AiluEngine/build/debug` |
| 2026-10-13 | M2 | `git diff --check` | PASS；无 whitespace errors | workspace |
| 2026-10-13 | M2 | `cmake --preset debug -D AILU_WITH_FSR=ON -D AILU_FSR_SDK_ROOT="C:/Users/liusongchen_e/AppData/Local/Temp/fsr-sdk-2.3.0-build"`；`cmake --build --preset build-debug` | PASS；SDK-on 全量 Debug preset 构建成功，Engine、Editor 和默认 smoke target 链接完成；`F:/AiluEngine/build/debug/CMakeCache.txt` 确认 `AILU_WITH_FSR=ON` | `F:/AiluEngine/build/debug` |
| 2026-10-13 | SDK package layout | Configure Debug with `AILU_WITH_FSR=ON` and `AILU_WITH_STREAMLINE=ON`, both SDK roots set to `Engine/Ext/Packages`; `cmake --build --preset build-debug` | PASS；完整 Debug preset 编译和链接成功；CMakeCache 确认两个开关均为 ON、roots 均在 `Engine/Ext/Packages/`；Editor 输出同时包含 FSR DLL 及 Streamline/DLSS runtime | `F:/AiluEngine/build/debug`；`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/FSR`；`.../Streamline` |
| 2026-10-13 | M2 | SDK-on Editor 启动，Stage5 Acceptance project，GTX 1050 / DX12 | PARTIAL；日志确认 SDK 2.3.0 已加载，provider `3.1.5` ID `17700776140660019205` 可用；首帧在 `Register::View<PostprocessVolumeComponent>()` 越界访问崩溃，未触发 FSR dispatch 或输出图像 | `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/PathTracerStage5Acceptance_20260929_104243/project/FileAppender_log.txt`；`.../project/crash.log`；启动输出 `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/fsr_m2_sdk_on_startup_20261013_1409` |
| 2026-10-13 | M2 Scene View UI | `cmake --build --preset build-debug` | PASS；AiluHeadTool 重新生成 RenderView reflection code，Editor 和全部默认目标成功构建；FSR selector 与状态代码已编译。未进行 Editor 交互验收（已知首帧 ECS crash 仍在） | `F:/AiluEngine/build/debug` |
| 2026-10-13 | SDK package layout | Stage AMD FidelityFX 2.3.0 and NVIDIA Streamline 2.14.1 under ignored `Engine/Ext/Packages`; verify 10/45-file package trees against `Scripts/packages.json` | PASS；FSR tree SHA-256 `e4c177831a63687b65f218087775bccfe8803ee1502f6523d8a765c2b239a1de`; Streamline release archive SHA-256 `92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b`, trimmed package tree SHA-256 `911e3af85541391d346b27f9d8063fb2153804b166be448b6b830cd0db5161bf`; both package paths ignored by Git | `Engine/Ext/Packages/fidelityfx-sdk-2.3.0`; `Engine/Ext/Packages/streamline-sdk-2.14.1` |
| 2026-10-13 | M2 | `git diff --check` | PASS；无 whitespace errors（仅提示若干现有工作文件的 LF/CRLF 转换） | workspace |

## Blockers

- Earlier M2 runs encountered a first-frame ECS crash in `Register::View<PostprocessVolumeComponent>()`; the current `empty` scene background run no longer reproduces that blocker and now completes actual FSR dispatch, continuous image capture and TAA restoration. Remaining M2 acceptance includes RenderDoc/PIX barrier inspection, independent motion-vector/dynamic-object and Camera Cut/reset checks, and DLSS regression. Package relocation and basic FSR dispatch are no longer current blockers; M3 must still wait for complete M2 acceptance.

## Next Step

完成 M2 FSR Quality Scene/Main Camera GPU/image acceptance and state validation；只在 M2 acceptance 全部通过后进入 M3。

## Known Follow-ups

见 README。

## Outcomes

M1 SDK bootstrap、Debug/Release SDK 开关构建、FSR provider discovery 与缺失 DLL fallback 均通过。M2 implementation and SDK-on Debug build passed; the local Editor loaded FSR 3.1.5 provider, then hit a first-frame ECS crash before any FSR dispatch. AMD FidelityFX 2.3.0 and NVIDIA Streamline 2.14.1 package subsets now live under Git-ignored `Engine/Ext/Packages/`, locked by source/archive and tree hashes; local binaries remain outside Git history. Image/GPU-state acceptance remains open.

## Resume Protocol

每次任务开始：读取 `README.md`、本文件、**当前 M 文件**；执行 -> 验收 -> 记录 -> 下一阶段。跳过任何未通过测试的阶段视为违反任务约束。
