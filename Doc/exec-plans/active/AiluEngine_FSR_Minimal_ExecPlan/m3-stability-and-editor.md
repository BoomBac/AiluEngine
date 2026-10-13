# M3 — Editor Integration, Lifetime, Switching & Regression

## Purpose

把 M2 的能出图版本变为能日常使用的功能：画质档位、选项反馈、模式切换、大小变化、多相机/Context 回收和稳定性回归，仍然避免引入新的大型超分系统。

## Scope

- SceneView：Bilinear / Point / DLSS / FSR，Quality/Balanced/Performance 同一套质量设置或各自清晰映射。
- 稳健的 Quality / Balanced / Performance 分辨率、Context 创建和销毁时机。
- 运行时 GPU fence 安全、SDK 失败路径和 Debug/Release 双路径稳定。
- 对 DLSS、TAA、Spatial、窗口 Resize、重复 Camera Cut 的回归。
- 调试日志：requested / resolved mode、render/output size、SDK provider、实际是否发生 Dispatch。

## Non-goals

不做 FG、FSR4、Reactive Mask、高复杂度多 Camera 缓存机制、自动质量控制、性能基准大规模采集、序列化格式大迁移。

## Architecture Constraints

- 不改变 `StreamlineSR::Evaluate()` 已能工作的资源通路；仅共用入口/Pass。没有 SDK 时不显示虚假的“Active”。
- 能力/运行状态查询应低成本且不依赖每帧重新探测 DLL/GPU；Context 所有者必须明确。
- Context Destroy 前等待相关 GPU 工作完成，不能在 callback 或记录 CmdList 中途立即释放当前可能被 GPU 使用的资源。
- 不给 Renderer 增加散落的特定超分后端判断；模式解析留在统一入口，RG 仅调用当前 backend。

## Baseline

- `Editor/Src/Widgets/RenderView.cpp` 的 Upsampling 当前为 `Bilinear`, `Point`, `DLSS`，DLSS quality 行通过 `SetDlssControlsEnabled()` 控制。
- SceneView 状态文字使用 `camera->IsDlssSRActive()`、`RenderSize()`、`OutputSize()`；需要区分用户请求和 resolved 模式。
- `Camera::RenderScale()` 现有手工比例在空间放大时生效；FSR 应使用自身质量 ratio。没有完整 Camera 序列化 DLSS 状态的现有证据。
- M2 预期首次实现仅针对 Scene/Main Camera，Quality 模式已跑通。

## Target Outcome

编辑器可选择 FSR 三档；SDK 有效则显示 `FSR 3.1.5 Active: WxH -> WxH`，无效显示具体原因或 `FSR inactive; TAA/Spatial fallback`；切换设置/窗口/相机不崩溃、不泄漏，DLSS 可继续正常使用。

## Relevant Files

- `Editor/Src/Widgets/RenderView.cpp`, `Editor/Inc/Widgets/RenderView.h`
- `Engine/Inc/Render/Camera.h`, `Engine/Src/Render/Camera.cpp`, `Engine/Src/Render/RenderPipeline.cpp`
- `Engine/Src/RHI/DX12/FsrRuntime.cpp`, `FsrDx12Backend.cpp`
- `Engine/Src/RHI/DX12/D3DContext.cpp`, `D3DCommandBuffer.cpp`
- `Engine/Src/Render/Features/StreamlineSrPass.cpp` 或 M2 新 `SuperResolutionPass.cpp`
- `Engine/Src/Render/Features/TemporalAA.cpp`
- `Editor/Src/Inspector/ComponentEditors/CameraComponentEditor.cpp`（仅评估；目前没有 DLSS 开关）

## Milestones

M3（本项目最终强制阶段）。后续画质改进必须单独计划，不属于 M3 的验收前提。

## Current Milestone

**M3**。

## Implementation Steps

### S1. Quality 三档

- `Quality=1.5x` / `Balanced≈1.7x` / `Performance=2.0x` 为用户可理解的默认比例。优先从当前 SDK query 获取质量 ratio/render resolution，再根据上/下限进行 clamp 和对齐。
- 确认切换 Quality 时是在**下一次相机 PrepareRenderFrame 之前**解析新 RenderSize，并使 history reset。
- 避免两套独立有效缩放比例：手动 `RenderScale` 在 FSR/DLSS 启用时不应叠乘；在 fallback 时按原 RenderScale 生效。
- 输出大小改变时，确保 Camera Matrix、RenderGraph 分辨率、Context maxRenderSize/最大输出尺寸同步更新。

### S2. 编辑器选项和状态

- `Editor/Src/Widgets/RenderView.cpp`：Upsampling 下拉增加 `FSR`；选择 Bilinear/Point 时关闭 SR 请求；选择 DLSS/FSR 时只使相应 backend 请求有效。
- Quality 行可复用现有 DLSS UI 行并重命名为 `Super Resolution Quality`，最少地调整显隐；不要另建一个功能重复的 FSR 行。
- 显示 `requested method`, `actual resolved mode`, `render/output resolution`, `availability reason`，不能仅依据用户选择展示 Active。
- SceneView `UpdateSceneCameraSettings()` 以及 dropdown 事件必须反向同步当前 Camera，否则切换 Camera 后 UI 与实际状态分叉。
- Camera Inspector 是否同步 UI：至少保证切换 Inspector 不把 SceneView 的模式错误覆盖；大规模跨编辑器状态同步可以留 follow-up。
- 暂不强推序列化（上传版本未证明 DLSS 状态已持久化）；若工程已有必要的 Camera schema 可安全增量扩展，需兼容旧场景，**绝不为序列化顺手大改场景数据格式**。

### S3. Context 生命周期和线程安全

- Context key 建议为 `Camera stable id + generation / backend + maxRenderSize + outputSize + flags`；若默认只有 Main/Scene 两个视图，可采用简洁的 `unordered_map<camera_id, context_state>`，不必提前建通用 ContextCache 子系统。
- 同个 Context 不可在两个 in-flight frame 中进行破坏性更新；当大小/flags 改变，创建新 Context，旧 Context 在 GPU fence 安全后销毁；设备重建/引擎 Shutdown 清空。
- Camera 销毁后 stale entry 要可回收；避免仅用裸 Camera* 作为永久 key。Scene/Main 同时渲染时不能互串历史。
- 再查 `RecordNativeCallback` 的执行线程、队列和 keep-alive；不允许 UI 线程直接销毁 render thread 正在使用的 FSR Context。
- 对“一个相机刚执行 FSR，马上切回 DLSS”以及“当前 GPU 尚在执行，立刻 resize”做压力测试。

### S4. Fallback 与用户可见错误

- SDK 不支持：在 Camera 解析阶段 fallback；消息清楚地区分缺 DLL、无 provider、DX12 无法使用、输入格式不支持。
- `ffxDispatch` 失败：记录错误码，确保**下一帧**禁用错误路径并重置历史；不要在每帧无条件重新 init 失败 Context，日志去重。
- 测试将 DLL 从应用运行目录移除（进程启动前）和 Runtime 主动禁用 FSR；启动及普通渲染仍正常。
- SDK 查询成功但后续 CreateContext 失败，也必须返回 “inactive + fallback”，不能留下已解析 `kFsrSR` 却没有正常输出的连续状态。

### S5. 完整回归与最小清理

1. 在不启用 FSR 的情况下，空间放大/TAA/DLSS 行为与 M1 前一致，特别是 DLSS Quality/RenderSize/Jitter/Reset。
2. 开启 FSR 的静止/运动/透明/水体/植被边缘拍摄对比；不可把本阶段可选 Reactive Mask 当成阻塞条件，但必须记录已知鬼影。
3. 对比 native 1080p、FSR Quality、Balanced、Performance 的 GPU 时间（尽量排除 VSync/CPU 限制）；不承诺 FPS 提升比例。
4. 重复 30 次切换算法/质量和 Resize；观察 Context 创建/销毁次数、显存、GPU Debug Layer、SDK 错误和日志增长。
5. 清理不再用的重复分支、重复 UI 元素和旧命名；**不要**重构与 FSR 无关的 DX12 状态系统。
6. 测试结束更新 `progress.md`，写出已通过用例、仍存在限制及后续追踪事项。

## Acceptance Criteria

- [ ] M2 已通过，FSR Quality/Balanced/Performance 各能正常输出指定 presentation 大小的图像，渲染尺寸正确。
- [ ] UI 显示 requested / active/inactive 一致；DLL 缺失与 GPU 不支持有明确提示；选择 Spatial 后无 FSR 调用。
- [ ] TAA / DLSS / FSR 之间交替切换无历史污染；DLSS 未回归退化。
- [ ] 缩放窗口（例如 1280×720 ↔ 1920×1080）、Camera Cut、RenderScale 改变、设备生命周期/退出均无 Crash。
- [ ] GPU Debug Layer 无新增 resource state、UAV hazard 或 lifetime 错误；GPU fence 后的 Context 释放安全。
- [ ] 连续切换测试中无明显持续性资源增长；同 camera 不会每帧 Context 重建；没有错误日志刷屏。
- [ ] `progress.md` 的 Outcome、Known Follow-ups、验证证据完整，可供下次开发恢复。

## Progress

`BLOCKED BY M2`；验收结果由 `progress.md` 跟踪。

## Decision Log

编辑器沿用现有 SceneView 控件；如 SDK 版本造成 Quality mode API 差异以已锁定版本为准，不引入无关功能。

## Surprises & Discoveries

待填：记录 DX12 SDK Context 管线恢复、外部资源状态和异步生命周期的真实差异。

## Validation

| 维度 | 必测组合 |
| --- | --- |
| SDK 有无 | DLL 完整 / 缺 loader / 缺 upscaler |
| 模式切换 | Bilinear ↔ TAA ↔ DLSS ↔ FSR（每项至少 10 次） |
| FSR 质量 | Quality / Balanced / Performance |
| 分辨率 | 1280×720、1920×1080、奇数窗口尺寸（如 1377×777） |
| 相机 | 旋转 / 镜头切换 / camera reset / Scene/Main 分别使用 |
| 资源 | DX12 Debug Layer / PIX 或 RenderDoc / 显存增减 / SDK 退出 |
| 渲染效果 | 前后处理链、移动物体 MV、透明场景、阴影/天空 |

**证据要求**：每种失败路径记录实际日志；每种质量至少一张相同场景截图；模式切换后的 reset 行为可用调试计数/录屏证明；不可用 `TODO`、`未测试但应该正常` 代替通过。

## Known Follow-ups

- 透明/reactive mask 生成与 placement；Negative Mip Bias；FSR sharpening/RCAS 参数（如该版本提供）。
- 与 FSR 4/帧生成共享更高层 UI 的可能性，另起新计划。
- 相机设置持久化；超过两个 Viewport 的长期 Context 淘汰策略。

## Outcomes

待验收。验收后将总体状态标记 `COMPLETE`，但须把未实测和后续画质问题如实列出。

## Resume Protocol

阅读 README + progress + 本文件，执行 S1–S5；填完测试矩阵并通过所有强制 Acceptance Criteria 后结项。
