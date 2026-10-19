# M5 — DLSS, FSR and Fallback Exposure Domain

## Purpose
避免 DLSS/FSR 内部自适应与 AiluEngine 的曝光/Pre-Exposure 不一致，并维持超分路径作为后处理输入的确定域。

## Scope
- FSR SDK preExposure 传入真实 `P_frame`，scene-linear 输入显式传 1。
- DLSS Streamline `useAutoExposure=true`：GPU 自动曝光输入先反预曝光到 scene-linear，vendor 输出再编码回相机的预曝光域。
- 非 RG 渲染保持 `P=1` 的既有兼容合同；DLSS/FSR RenderGraph pass 不进入该 legacy 路径。
- FXAA/TAA/No AA、RenderScale、动态分辨率、Editor SceneView/GameView 的行为空间一致。
- vendor runtime 无法验证时明确 fallback：额外标准化到 unpreexposed HDR 输入，返回时恢复原 P，避免曝光倍增；它会增加成本，但强于不正确。

## Non-goals
不在此阶段升级 DLSS/FSR SDK，不实现 Frame Generation 或替换 SR 源码。

## Architecture Constraints
- 所有 `PostprocessStack` 输入看起来必须处于相同颜色域；不能因为 `_upscaled_color` 来源不同就有不同的隐含曝光。
- FSR `dispatch.preExposure` 当前固定 1.0，必须修正；DLSS `options.useAutoExposure` 是否保留依据 SDK 证据与 GPU 实测，不能想当然置 false。
- 每相机 frame token、曝光资源 lifetime、resize/reset revision 必须与超分历史对应；在同帧 Mode switch 上强制 reset。
- 非 RG 若本项目主要依赖 RG 则可设计安全旧路径 `P=1` + CPU Manual scalar；不能悄悄让非 RG 的前后空间不一致。

## Baseline
`FsrRuntime.cpp` 当前 preExposure=1.0；`StreamlineDx12Bridge.cpp` 当前 DLSS `useAutoExposure=true`；`PostprocessStack` 先选 SR color 再 TAA color。

## Target Outcome
不同 AA/SR 模式，在相同机位/同一场景/同样当前 `E_display` 下测得目标亮度一致；无算法切换闪光与时域泵动。

## Relevant Files
`RHI/DX12/{FsrRuntime,StreamlineDx12Bridge}.cpp`, `Features/{StreamlineSrPass,FsrSrPass,TemporalAA,PostprocessStack}.cpp`, `Renderer.cpp`, `Camera.cpp`。

## Implementation
1. [x] SR frame data 明确记录输入 color domain、P 和真实引擎 frame index。
2. [x] FSR dispatch 接收 P；GPU Auto 输入先转 scene-linear 时传 1，否则传当前 CPU P。
3. [x] DLSS 保持 `useAutoExposure=true`；Manual/Legacy 传递已知 P，GPU Auto 使用 shader 反预曝光/输出恢复，不增加 CPU readback。
4. [x] 曝光模式切换增加 camera temporal reset；现有 temporal mode resolver 覆盖 DLSS/FSR/TAA/spatial 切换、尺寸变化和可用性变化。
5. [x] SR 输出先用域匹配的输入缩放结果初始化，SDK evaluate 失败时保留有效 fallback；自动曝光路径恢复相机预曝光域。
6. [ ] GPU runtime、SDK 资源格式支持和切换/失败画面验收待用户执行。

## Acceptance Criteria & Validation
- [ ] FSR exposure 参数不再恒等于 1；正确设置 P=0.1/1/10 等测试值且结果按域预期。
- [ ] DLSS 与 FSR 各对照 TAA/native，同一HDR灰卡及高动态场景的最终亮度误差在定义阈值内；快速切换不亮度泵动。
- [ ] 预曝光由非 1 回退到 Legacy 路径、DLSS 失效或 resize 不会产生一帧亮度异常或黑帧。
- [ ] 没有双重自动曝光：测光显示值、最终 ToneMap 倍率与 vendor SR 输入/输出量纲都可由抓帧解释。
- [ ] SDK 文档/头文件引用、具体 shader/resource Tag 配置写 progress；无法实测的供应商路径不能声明 PASS。

## Current Milestone
M5
## Progress
RenderGraph SR 域转换、FSR P 传递、DLSS preExposure metadata、camera reset 和安全 seed fallback 已实现并通过 Debug build。M4 GPU acceptance 与 M5 vendor runtime/image acceptance 仍待用户验收。
## Decision Log
先统一颜色域，再选择 vendor 自适应设置。DLSS 使用 SDK 的 auto exposure 时，GPU Auto 输入归一化到 scene-linear（P=1）；结果在交还 PostprocessStack 前恢复到本帧场景预曝光域。FSR 没有 CPU readback，GPU Auto 同样走 GPU 域转换并将 dispatch P 设为 1。
## Surprises & Discoveries
FSR preExposure=1、DLSS auto=true 是已知集成风险。
## Known Follow-ups
Frame Generation、Ray Reconstruction 等升级另立计划。
## Outcomes
实现已完成，构建验证通过；SDK shader/runtime/GPU 数值与视觉验收未执行，不能标记 M5 acceptance 通过。
## Resume Protocol
通过全部验收后进入 M6；记录 SDK 版本、GPU 型号、效果和错误路径。
