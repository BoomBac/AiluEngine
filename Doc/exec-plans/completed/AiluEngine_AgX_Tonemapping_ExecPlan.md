# AiluEngine AgX Tonemapping 最小侵入接入 — ExecPlan

> **状态**：Done（2026-10-17：实现、Debug 构建、GPU 数值/Editor 捕获验证完成；验收边界见 Outcomes）  
> **规模**：小型任务，单文件、单 Milestone。  
> **归档位置**：`Doc/exec-plans/completed/AiluEngine_AgX_Tonemapping_ExecPlan.md`  
> **实现基线**：`BoomBac/AiluEngine`，`master`，检查时的提交 `f49b6f1d0f4da2e802f9ea33bf9681af1a9b23cb`（执行前以工作区最新源码为准）  
> **执行对象**：Codex / Claude Code；实施、构建、真实 GPU 验收都应在用户本地 AiluEngine 工作区完成。

## Purpose

以最小侵入方式为现有 AiluEngine 后处理链增加 **Minimal AgX（Rec.709 Linear 输入、Display Linear 输出）**，使场景中的 Global Postprocess Volume 可以在 `None / ACES / AgX` 三种 Tonemapping 算子之间切换。保留现有 ACES 和 Bloom 行为；让高亮颜色渐近白色，减少高光颜色失真，并形成可重复的 GPU 数值/图像验证证据。

本文件是 **待执行计划**，不能将“已完成源码分析”等同于“实现/编译/显卡验证通过”。

## Scope

- 在现有 `EToneMappingMode` 中追加 `kAgx = 2`，保持既有枚举数值。
- 通过项目的反射构建流程更新 `PostprocessSettings` 的生成结果。
- 在当前 `tone_mapping.hlsl` 内实现 Rec.709 Minimal AgX，复用已有 `_SourceTex`、`_Exposure`、`_ToneMappingMode` 和完整 RenderGraph Pass。
- 在 Postprocess Volume Inspector 的 Mode 下拉列表中增加 AgX。
- 确认 Scene / Prefab 保存、加载时 AgX 枚举值保持不变；验证 None / ACES 回归。
- 按仓库的 render-frame-validation skill 完成至少一条真实 Editor GPU 验证路径，包括 ToneMapping Pass 资源及固定曝光对比。

## Non-goals

- 不实现完整 Blender AgX OCIO 配置、3D LUT、ACES 1.x/2.x 色彩管理或正式 HDR Display Transform。
- 不实现 AgX Golden / Punchy Look、Look 滑块、Exposure EV UI、自动曝光、Color Grading 或新的 Shader Variant；这些均作为后续扩展。
- 不重写 `BloomPass`、`ToneMappingPass.cpp`、RenderGraph、Swapchain、DLSS/FSR、TAA 或 ImGui。
- 不改变默认 ToneMapping Mode（保留 `kAces`），不迁移旧场景数据，不添加第三方运行时依赖。
- 不改 CMake 反射生成器本身、不手写 `.gen.h/.gen.cpp`，不批量格式化无关文件。

## Architecture Constraints

1. 现有路径：`Scene Linear HDR → [DLSS/FSR SR 或 TAA/CopyColor] → Bloom (HDR) → ToneMapping → FinalColor → FinalBlit → Presentation`。这里只新增 **ToneMapping 算法分支**。
2. 输入为 **Rec.709/sRGB 原色的线性 HDR RGB**，曝光在 AgX 前乘：`source.rgb * _Exposure`。不增加内部曝光倍率或默认场景亮度补偿。
3. 目标是 **Display Linear 的 SDR 范围近似**（`[0,1]`），与目前 `kDefaultHDR`（R11G11B10_FLOAT）ToneMapping 输出及默认 R16G16B16A16_FLOAT Swapchain 兼容；`pow(max(x,0),2.2)` 属于 Minimal AgX 实现最后的线性化，不得误删。**本 Pass 不额外执行 `ApplySRGBCurve`、`pow(1/2.2)`、PQ Encode；最终显示输出的编码另有契约，不能仅由 FP16 格式推断。** 这不是 HDR10 的 AgX 方案。
4. 采用下文的 **Rec.709 Minimal AgX Inset / Outset 矩阵配对**。不得混用 Filament/Blender Rec.2020 变体的矩阵或 sigmoid 系数；HLSL 使用 `mul(matrix, vector)` 行向量/列向量约定需以数值测试确认。
5. `PostprocessStack` 在所有 Mode 下均创建 ToneMapping Pass，None 只应用曝光；没有有效 Global Volume 时，`ResolvePostprocessSettings()` 当前将 Mode 置为 None。验收场景必须存在并启用 Global Volume，不能误判为 AgX 失效。
6. Shader 原有 `//info begin ... //info end` pass 声明、资源插槽及 cbuffer 属性名保持不变。第一版使用运行时 uniform 分支，不增加新的 Shader Variant 或资源。
7. `ToneMappingPass.cpp` 现有 `_Exposure` / `_ToneMappingMode` 传值、尺寸从 `graph.GetTextureDesc(source)` 推导、输出 `ToneMappedColor` 的逻辑保持不变。
8. 遵循仓库 `AGENTS.md`：变量 `lower_snake_case`、成员 `_lower_snake_case`、静态变量 `s_`、常量 `kCamelCase`、类型/函数 `CamelCase`，新增代码原则上 ≤120 列；保留用户的未提交改动，不清理不相关 dirty files。
9. 禁止用 CPU 渲染或纯源码检查替代真实 GPU 验收；若当前环境无法运行 Editor，须明确标记 GPU Validation 为 Blocked，不能假报通过。

## Baseline

已阅读（截至计划编写时）：

- `Engine/Inc/Render/PostprocessSettings.h`：`EToneMappingMode` 当前只有 `kNone` / `kAces`；`ToneMappingSettings` 默认 ACES，曝光默认 `1.0f`。
- `Engine/Res/Shaders/hlsl/PostProcess/tone_mapping.hlsl`：采样线性 HDR，乘 `_Exposure`；`_ToneMappingMode == 1` 时调用 `ACESFilm`；返回 `float4(color, 1)`。
- `Engine/Res/Shaders/hlsl/color_space_utils.hlsli`：现有 ACES 实际是 Narkowicz 风格的 `ACESFilm()` 近似，而不是完整 ACES color pipeline。
- `Engine/Src/Render/Features/ToneMappingPass.cpp`：已读取 source、输出 `ToneMappedColor`，材质已传递 Mode 和 Exposure；暂不需要改。
- `Engine/Src/Render/Features/PostprocessStack.cpp`：负责选择有效 HDR handle 和组合 Bloom / Tonemapping，`None` 执行曝光但不应用 Tonemapping 曲线。
- `Editor/Src/Inspector/ComponentEditors/PostprocessVolumeComponentEditor.cpp`：Mode 下拉目前为 `{"None", "ACES"}`，枚举索引直接转 `EToneMappingMode`。
- `Engine/Src/Render/PostprocessSettings.cpp`：只有有效 Global Volume 才使用该组件 settings；否则 Bloom 和 ToneMapping 都禁用。
- `Engine/Inc/Render/generated/PostprocessSettings.gen.cpp`：旧枚举注册值：`kNone=0`、`kAces=1`。
- `Engine/Inc/Render/Texture.h`：`ERenderTargetFormat::kDefaultHDR` 目前映射至 `R11G11B10_FLOAT`。
- `Engine/Inc/Render/RenderConstants.h`：默认 HDR Swapchain 格式为 `R16G16B16A16_FLOAT`；正式结果仍需要确认具体显示/SceneView 输出路径。
- `Doc/architecture/reflection-codegen.md` 和根目录 `CMakeLists.txt`：AiluHeadTool / `ailu_reflection_codegen` 负责源码树内的生成文件；禁止直接修改生成文件。
- `.agents/skills/ailu-render-frame-validation/SKILL.md`：真实渲染验收走现有 render capture 管线。

**基线状态**：只有架构/源码检查通过；尚未修改文件、编译 Shader、运行 Editor 或采集结果。

## Target Outcome

Inspector 能即时切换 `None / ACES / AgX`；AgX 按已定义的 Rec.709 Minimal AgX 曲线处理与 ACES 相同的 Linear HDR 输入；两种旧模式没有回归；Scene 重开后选择保持；ToneMapping 输出为线性 RGB、没有重复编码或明显 NaN/Inf；用 GPU capture 和数学参考值完成验收。

## Relevant Files

| 文件 | 处理方式 | 用途 |
|---|---|---|
| `Engine/Inc/Render/PostprocessSettings.h` | **修改** | 追加 `kAgx=2` |
| `Engine/Res/Shaders/hlsl/PostProcess/tone_mapping.hlsl` | **修改** | Minimal AgX 及 Mode=2 分支 |
| `Editor/Src/Inspector/ComponentEditors/PostprocessVolumeComponentEditor.cpp` | **修改** | 下拉选项增加 AgX |
| `Engine/Inc/Render/generated/PostprocessSettings.gen.h`、`.gen.cpp` | **自动生成的 diff** | AHL 反射注册及序列化信息 |
| `Engine/Src/Render/Features/ToneMappingPass.cpp` | **只读检查** | 确认共享 Mode/Exposure/RT |
| `Engine/Src/Render/Features/PostprocessStack.cpp` | **只读检查** | None 保留 Pass 和曝光；路径不变 |
| `Engine/Src/Render/PostprocessSettings.cpp` | **只读检查** | Volume 选择与无 Volume 行为 |
| `Engine/Res/Shaders/hlsl/color_space_utils.hlsli` | **只读检查** | 原有 ACES、sRGB 工具函数；本阶段优先不动 |
| `Doc/architecture/reflection-codegen.md`、`.agents/skills/ailu-render-frame-validation/SKILL.md` | **执行参考** | 生成和真实 GPU 验证 |

## Milestones

### M1 — 最小 AgX 接入与端到端验收（唯一 Milestone）

**目标**：完成 Shader + reflected enum + Inspector 的最小变更，并在真实 DX12 Editor 中验证 AgX 的数值、视觉、存档行为。

**M1 状态：Done**。M1 内按以下顺序完成子步骤；**全部 Acceptance Criteria 达成才允许把 M1 和整个计划标记 Done**。

#### M1.1 准备与回归基线

1. 阅读 `AGENTS.md`、`Doc/architecture/index.md`、相关 rendering/reflection 文档及列出的实际源码；若工作区与本计划不同，以源码为准，并记录差异。
2. 执行 `git status --short` 并记录本任务开始前已有 dirty files，严禁 `git reset`、覆盖或清理用户的工作区改动。
3. 确定可以跑通的 Editor 场景及启用的 Global Postprocess Volume；固定相机、分辨率、Bloom 和 Exposure（建议 `1.0`）；记录默认 ACES 可用性。
4. 验证现有 `tone_mapping.hlsl` 编译入口及 Shader asset/Shader cache 机制。不要将 Shader asset hot-reload 失败与算法失败混为一谈。

#### M1.2 修改反射枚举及 Inspector

`Engine/Inc/Render/PostprocessSettings.h`：

```cpp
AENUM()
enum class EToneMappingMode
{
    kNone = 0,
    kAces = 1,
    kAgx = 2
};
```

- 只追加数值，保留默认 `ToneMappingSettings::_mode = EToneMappingMode::kAces`。
- Inspector 下拉改为 `{"None", "ACES", "AgX"}`；已有回调继续 `static_cast<Render::EToneMappingMode>(index)`。
- 使用仓库原有 Debug 构建/反射生成依赖，让 AiluHeadTool 更新 `.gen.h/.gen.cpp`；**不要手写生成文件**。检查生成后 `EToneMappingMode` 的字符串映射包含 `kAgx=2`，原值未变；如发生大量不相关重新生成，定位原因而不是盲目提交。

#### M1.3 在现有 Shader 中实现 Minimal AgX

以下为算法参考（可直接移植到 `tone_mapping.hlsl`，最终按真实 Shader 编译结果修正语法）。矩阵按 HLSL `mul(matrix, vector)` 指定，输入 Linear Rec.709，结果 Display Linear。**必须先通过下文数学基准，才能认定矩阵方向正确。**

```hlsl
float3 AgxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 AgxToneMap(float3 scene_linear_color)
{
    static const float3x3 kAgxInset = float3x3(
        0.84247906, 0.07843360, 0.07922375,
        0.04232824, 0.87846864, 0.07916613,
        0.04237565, 0.07843360, 0.87914297
    );
    static const float3x3 kAgxOutset = float3x3(
         1.19687901, -0.09802088, -0.09902974,
        -0.05289685,  1.15190313, -0.09896118,
        -0.05297164, -0.09804345,  1.15107367
    );
    static const float kAgxMinEv = -12.47393;
    static const float kAgxMaxEv = 4.026069;

    float3 color = mul(kAgxInset, max(scene_linear_color, 0.0));
    color = saturate((log2(max(color, 1e-10)) - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv));
    color = AgxContrast(color);
    color = mul(kAgxOutset, color);

    // Minimal AgX display transfer linearization; NOT sRGB/PQ output encoding.
    return saturate(pow(max(color, 0.0), 2.2));
}
```

现有 Pixel Shader 只追加分支：

```hlsl
float4 ToneMappingPS(FullScreenPSInput input) : SV_TARGET
{
    float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb * _Exposure;
    if (_ToneMappingMode == 1)
        color = ACESFilm(color);
    else if (_ToneMappingMode == 2)
        color = AgxToneMap(color);
    return float4(color, 1.0f);
}
```

注意：

- `None` 同样执行现有 ToneMapping Pass，输出 source.rgb * _Exposure；不做曲线压缩或饱和截断。
- 保持 `_Exposure` 在 AgX / ACES 的共同输入端，不能在 AgX 内再乘一次。
- `float3x3` 的转置错误会引入严重色相偏移，**不要因为全白灰阶大致正确就跳过 R/G/B 单色对照**。
- 本阶段不调用 `ApplySRGBCurve()`、`RemoveSRGBCurve()` 或额外的 PQ 函数，不更改 FinalBlit/Swapchain 逻辑。
- 参考数值包含 Minimal AgX 的低亮度拟合误差；极暗输入的结果应 non-negative 且 finite，不要求算子数学上严格映射所有非零值到正值。

#### M1.4 算法数值验证（优先在纯数值测试中完成）

对每个测试点，用独立 Python/C++ CPU 浮点实现复算 **相同** 矩阵及算式，与 Shader 对照。参考取值为 `Exposure = 1`、Rec.709 scene-linear、上述 Minimal AgX 常量，并在最终 `saturate` 之后读取，允许 RT 格式量化。

| 输入 RGB（Linear HDR） | 参考输出 RGB（Display Linear，约） | 目的 |
|---|---|---|
| `(0,0,0)` | `(0,0,0)` | 纯黑、log epsilon 安全 |
| `(0.18,0.18,0.18)` | `(0.2145,0.2145,0.2145)` | 中灰与中性轴 |
| `(1,1,1)` | `(0.5900,0.5902,0.5902)` | 白色基准 |
| `(4,4,4)` | `(0.8606,0.8610,0.8610)` | 高光压缩 |
| `(16,16,16)` | `(0.9947,0.9952,0.9952)` | 高光 roll-off |
| `(4,0,0)` | `(1.0000,0.1835,0.1837)` | 红通道 / 矩阵方向 |
| `(0,4,0)` | `(0.2841,0.9455,0.2843)` | 绿通道 / 矩阵方向 |
| `(0,0,4)` | `(0.2859,0.2859,0.9445)` | 蓝通道 / 矩阵方向 |

- **CPU vs GPU 门槛**：在将对应数值输入直接送入 Tonemapping Shader 的可控测试路径中，输出的每通道绝对误差 ≤ `0.01`；同时所有输出 finite 且位于 `[0,1]`。RT 中的数据为重点，不以屏幕截图的 Gamma 值直接做浮点比较。
- 中性轴测试 `(r=g=b)` 的输出三个通道差值 ≤ `0.003`（考虑拟合与矩阵舍入）；对按倍数增长的灰阶 `0 → 0.18 → 1 → 4 → 16`，亮度不得倒退。
- 若不能把测试向量直接送入 Shader，可新增 **局部/临时** 测试输入或使用现有 RenderGraph 测试工具，完成后清除临时代码；不得为了这一测试创建新的生产渲染架构。
- CPU oracle 只能证明公式和矩阵匹配，真实 GPU 验收仍必需。

#### M1.5 运行时集成与回归

1. 使用 `cmake --build --preset build-debug`；这是仓库 `AGENTS.md` 明确的默认验证命令。禁止使用 `build-develop` 代替。
2. 启动实际 Editor，准备启用了 Global Postprocess Volume 的场景；确认 SceneView 的渲染通路实际执行 PostprocessStack / ToneMapping。
3. 依次切换 ACES、AgX、None；对同一固定相机和相同 HDR 输入抓取对应数据。**AgX 必须产生可见且合理的差异**；None 执行 ToneMapping Pass 且仅应用曝光；ACES 与修改前输入/输出一致。
4. 设置固定 Exposure（例如 `0.5 / 1.0 / 2.0`），验证输出平滑变化、无闪烁、NaN、Inf、色块。Exposure 改动不可导致 shader 资源重建或增加新 RenderPass。
5. Bloom Off / On 分别运行 AgX；确认 Bloom 位于 ToneMapping 前，打开 Bloom 后只因 HDR 输入变化而改变 AgX 结果，没有双重 tone mapping。
6. 保存包含 AgX 的 Scene、关闭并重新打开；检查 UI 保持 AgX 且最终画面一致（再检查 Prefab 中 Volume 的序列化/实例化路径，若项目支持相应操作）。
7. 渲染输出仍保留当前 `ToneMappedColor`→`FinalBlit` 结构；录制时无多余 RT 分配、RG barrier 错误或新 GPU 警告。

#### M1.6 GPU 图像与证据验收

1. 先读取 `.agents/skills/ailu-render-frame-validation/SKILL.md` 和 `references/workflow.md`，优先使用现有 `render.capture_pass` / `render.capture_status`，否则使用 `Tools/render_capture_client.mjs`；记录实际调用方式和会话参数。
2. 捕获 **ToneMapping 输入 HDR**（实际源 handle，可包含 Bloom）与 **`ToneMappedColor`**，固定相机、曝光、分辨率；分别记录 ACES / AgX。
3. 必选测试内容：亮天空/太阳邻域、强 emissive、RGB 高饱和高亮球、灰阶/金属球；场景中若缺少相应资产，允许使用不保存的临时测试场景或现有测试资源。
4. 如果使用 RenderDoc / PIX，核对输入像素存在 `> 1.0` 的 HDR 值，输出在合理 Display Linear 范围内；确认 source 是正确的 DLSS/FSR/TAA 或 CopyColor 路径，避免抓错 handle。
5. 帧数、引擎 frame index、capture manifest、GPU 资源名和实际抓帧值都必须匹配；不因“抓到文件”就判定图像正确。
6. 如有固定 CPU oracle 输入，把至少 8 个参考向量逐一对比到 GPU RT 读数（见 M1.4）；相同输入可用小型测试纹理覆盖更多像素。
7. 截图只能作为主观美术对照；记录 HDR 源、曝光、Bloom、输出格式及场景身份，禁止跨曝光、跨摄像机条件比较 AgX 与 ACES。
8. 捕获日志/结果放到 **实际 Editor.exe 所在目录旁的独立会话目录**，不得放到仓库 `Saved/`；按 skill 的规则恢复更改的 Editor/Scene 状态。

#### M1 Acceptance Criteria（全部必须满足）

- [x] 枚举/Inspector 能选择 `None(0) / ACES(1) / AgX(2)`，默认 ACES 未改变。
- [x] 反射生成成功，`kAgx` 经过 AHL 正式注册；没有手工修改生成文件和无关大规模生成 diff。
- [x] `cmake --build --preset build-debug` 编译/链接通过，运行时 Shader 编译成功。
- [x] `None` 按用户要求改为保留曝光、ACES 与 baseline 一致；AgX 生效且实际执行原有 ToneMapping RenderGraph Pass。
- [x] CPU/GPU 参考向量数值比较通过；无明显 RGB 通道置换或中性色偏；无 NaN / Inf。
- [x] ToneMapping 前 HDR、ToneMapping 后 Display Linear 已通过实际抓帧确认，未新增 sRGB/PQ 编码。
- [x] 固定曝光 ACES/AgX A/B 截图与关键像素记录完成；RGB 高亮渐近白色的行为可观察。
- [x] 曝光切换、Bloom 开关、Scene 保存与重开、实际 Global Volume 启用/禁用逻辑验证通过。
- [x] 没有增加新 RenderPass、RenderGraph 大改、Shader Variants、运行时新纹理/LUT 或额外全屏绘制。
- [x] 在本文件记录 Validation 结果、git diff 摘要与 Outcomes；未通过的项目有明确 blocker 或失败证据。

## Current Milestone

`M1 — 最小 AgX 接入与端到端验收`，**Done**。用户修订后的 None 曝光流程已实现；结果与未覆盖边界见文末。

## Progress

- [x] 检查 GitHub 上的 Tonemapping、Postprocess、Volume、Inspector、反射生成和输出格式源码。
- [x] 选择 Rec.709 Minimal AgX，确定矩阵、sigmoid、输出线性化的约定。
- [x] 制定一个 Milestone 的实施步骤、测试输入和验收门槛。
- [x] M1.1 工作区基线与真实构建环境确认。
- [x] M1.2 Enum + Inspector + AHL codegen。
- [x] M1.3 Shader 实现。
- [x] M1.4 数值参考测试。
- [x] M1.5 Editor 功能回归。
- [x] M1.6 真正的 GPU 图像与资源捕获。
- [x] M1 完整验收并归档。

## Decision Log

| 决策 | 理由 |
|---|---|
| 只创建一个 Milestone 和一个 ExecPlan 文件 | 子系统改动少，当前阶段不需要跨文件保存上下文 |
| 在现有 ToneMapping Shader 中添加 Mode=2 | 最小侵入，无需新 Pass、材质或 Graph 资源 |
| 使用 Minimal AgX Rec.709 近似而非 Blender/Filament Rec.2020 版本 | 当前引擎 Scene Linear 使用 sRGB/Rec.709 原色；避免矩阵混用 |
| 采用已有 `float3x3` HLSL `mul(matrix, vector)` 约定 | 易于与独立 CPU oracle 比较；显式检查转置 |
| 保留 ACES 默认和旧枚举数值 | 避免既有场景出现未经授权的外观变化 |
| 首版不加入 Punchy/Golden Look | 先验证默认曲线、色相和输出编码正确性 |
| 不做额外 sRGB Encode | 当前默认 HDR Swapchain 为 FP16；ToneMapping 输出契约是 Display Linear |

## Surprises & Discoveries

- 当前引擎的 `ACESFilm` 只是快速曲线近似，A/B 对比不是“完整 ACES vs 完整 Blender AgX”。
- 没有有效 Global Postprocess Volume 时，`ResolvePostprocessSettings()` 返回的默认是 `kNone`，与结构体里 `kAces` 的初始化默认值不同。
- `kDefaultHDR` 在当前 `Texture.h` 中是 R11G11B10_FLOAT，不是 FP16；实际 Shader 输出数值验证要考虑格式量化。
- Google Filament AgX 使用 **Rec.2020** 处理，并有不同的曲线实现细节；本次方案采用 Rec.709 Minimal AgX，**禁止拼接两者矩阵、假定二者像素完全相等**。
- AHL 生成代码位于源码树；只改枚举源码、不触发 codegen，可能使反射字符串注册落后于 C++ enum。
- 先前的简单比较容易把 Minimal AgX 的 `pow(2.2)` 误解为 Gamma Encode；实际上其用途是从 AgX 中间编码近似回到线性显示值，不能随意替换为 `pow(1/2.2)`。

## Validation

当前：**PASS（实现与自动化验证范围）**。精确记录见下方 2026-10-17 Outcomes；手工 Inspector 点击与显示器色度验收未执行。

| 层级 | 状态 | 证据/路径（执行后填写） |
|---|---|---|
| 源码审查与工作区基线 | PASS | `git status --short`、差异摘要 |
| AHL 生成及 Diff | PASS | 生成的 `kAgx` 映射和 diff |
| Shader 编译 | PASS | 编译器/Shader cache 日志 |
| Engine / Editor Debug Build | PASS | `cmake --build --preset build-debug` 摘要 |
| CPU 参考向量 | PASS | 输入表、数值结果、误差 |
| GPU RT 数值校验 | PASS | Readback / PIX / RenderDoc 资源记录 |
| ACES / None 回归 | PASS | 基线和对照证据 |
| AgX 真实画面对照 | PASS | capture manifest、HDR Input、ToneMappedColor、截图 |
| Exposure / Bloom / Volume / 序列化 | PASS | 场景重开、UI、模式及参数行为 |

执行时每项填写 `PASS / FAIL / BLOCKED`、复现方式与可访问的证据位置。不能把纯编译验证写成 GPU 通过；如果真实 GPU 不可用，整个 M1 保持 Blocked。

## Known Follow-ups

本轮不实现，按需另开任务：

1. AgX Punchy / Golden Look（增加枚举及少量 Uniform；保留 Default 作为 baseline）。
2. HDR10 / PQ 与 HDR scRGB 真实高亮输出所需的 Output Transform 分离及参考白标定。
3. 可选 3D LUT 版本、完整 Blender OCIO AgX 对照、广色域和进一步 Color Grading。
4. 高低端显卡上 AgX 近似 sigmoid、`log2/pow` 的单 Pass GPU 性能实测。

## Outcomes

**当前成果**：已实现并完成本文末记录的构建、反射、数值与 GPU 捕获验证。历史实施清单如下；最终结果以文末为准：

- 实际修改文件和 commit / diff 摘要；
- Shader 实现与参考算法差异；
- 构建、反射、CPU/GPU 对照、渲染捕获结果；
- 旧模式回归结果及残留问题；
- 最终 `PASS / FAIL / BLOCKED` 判定。

如全部验收通过，将计划移动至 `Doc/exec-plans/completed/agx-tonemapping.md`；不要在未通过前归档。

## Resume Protocol

1. 只读取本文件、仓库 `AGENTS.md`、当前相关 `Doc/architecture/` 页面及 `Relevant Files` 中实际需要修改的源码，不加载其他无关 ExecPlan。
2. 先查看 **Current Milestone / Progress / Validation / Decision Log / git status**；区分已规划与已执行项目。使用实际工作区源码重新确认关键函数和资源输出约定。
3. 从第一个未完成的 M1 子步骤继续，不重做已通过的无关验证；若源文件已经发生变更，比较原因后决定是否更新本文件基线。
4. 每个子步骤后及时更新勾选项和发现的问题；失败须写出观察事实、阻塞原因及可执行 Next Step，不要用“应该没问题”代替证据。
5. 仅在 M1 Acceptance Criteria **全部通过**后，将 `Current Milestone` 置为 Done、填写 Outcomes，并按仓库惯例归档。

## References

- Minimal AgX (IOLITE): https://iolite-engine.com/blog_posts/minimal_agx_implementation
- Filament AgX (用于比较实现差异，**不可直接混用矩阵**): https://github.com/google/filament/blob/main/filament/src/ToneMapper.cpp
- 项目反射说明：`Doc/architecture/reflection-codegen.md`
- 项目渲染架构：`Doc/architecture/rendering.md`
- 项目自动抓帧规范：`.agents/skills/ailu-render-frame-validation/SKILL.md`


## 2026-10-16 Implementation amendment

User explicitly overrides old None bypass behavior: all postprocess-enabled views execute the shared ToneMapping pass.
None outputs source * exposure without curve compression/clamping; ACES and AgX apply their respective transform.
No Global Volume resolves to None/exposure=1/Bloom off and still records the pass. Camera postprocess_enabled=false remains
an explicit opt-out. Exposure remains a serialized linear multiplier and ACES remains the default enum value.
The final output remains display-linear; this change does not infer display encoding from FP16 alone or alter swapchain/UI.
Implementation started: enum, Inspector, AgX shader and unconditional shared pass. Build/GPU/serialization validation pending.


### Implementation progress - 2026-10-17

- Implemented enum None=0/ACES=1/AgX=2, shader Minimal AgX, both single-object and multi-object Inspector lists.
- Changed PostprocessStack to always record ToneMapping for postprocess-enabled views, including no-Volume fallback.
- AHL regenerated only the intended PostprocessSettings enum registration/header line references.
- Default `cmake --build --preset build-debug`: PASS; dedicated ToneMappingSmoke target builds using build-debug.
- Editor calibration 20261016-235011/editor_final: PASS. Seven mode/exposure/Bloom controls, 3 consecutive frames per
  input/output, no-Volume pass, scene save/reopen pixel match and all shared configuration byte restoration passed.
  None exposure 1 and 0.5 matched source multiplication exactly. AgX output errors respect R11G11B10 quantization.
- A stronger direct-light HDR calibration is running to extend coverage beyond the emission-only scene.
- Standalone numeric harness initially lacked a registered pipeline/frame allocator and temporary-scene ordering;
  fixed test ownership based on crash stacks without changing production runtime. Cold full-pipeline DXR compile
  exceeded first 180s initialization deadline; warm-cache run has a bounded 420s timeout. Do not count these attempts PASS.
- Current milestone: finish 8-vector GPU oracle/HDR scene analysis, review diff, then finalize plan status.


## Final Outcomes - 2026-10-17

Implementation complete. `None=0` always traverses the shared ToneMapping pass for postprocess-enabled views and
outputs source * exposure without curve compression; `ACES=1` unchanged, `AgX=2` added. No-Volume fallback also
records the pass with exposure=1/Bloom off. Explicit camera postprocess disable remains an opt-out.

Changed production files: PostprocessSettings.h and AHT-generated PostprocessSettings files; tone_mapping.hlsl;
PostprocessStack.cpp; single-object PostprocessVolumeComponentEditor.cpp and multi-object ComponentInspectorFields.cpp.
Existing unrelated dirty changes in shared files are preserved. Exposure retains multiplier semantics, old enum values
and default ACES remain unchanged. No swapchain/UI transfer-function changes or runtime LUT dependency.

Validation evidence root:
`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/agx_validation/20261016-235011/`.
- Default `cmake --build --preset build-debug`: PASS, including final Inspector changes.
- `cmake --build --preset build-debug --target ToneMappingSmoke`: PASS.
- `gpu_final_checked`: owned smoke exited 0; 24 actual shader captures (8 vectors x 3 modes x 4 exposures x 2 formats).
  Independent double-precision oracle: RGBA32F maximum absolute error 1.0142144304725598e-05, all <=0.01;
  float neutral-axis <=0.003, bounded AgX and monotonic gray ramp. Production R11G11B10 errors <=one format ULP
  plus 2e-5 algorithm allowance. Its blue channel can truncate by 1/64 near unity: the original unqualified 0.01
  and neutral-axis 0.003 gates are therefore applied to float targets, not misapplied to packed storage.
- PostprocessSettings and PrefabEntityDocument all three mode/exposure serialization roundtrips: PASS.
- `editor_final` and `editor_hdr`: real Editor runs PASS, each accepted capture contains three consecutive frames.
  Runtime mode/Volume changes used existing top-level generic property writes; no validation-only reflected type added.
  None/ACES/AgX, exposure 0.5/1/2, Bloom off/on, no global Volume, save/close/reopen and mode persistence passed.
  Strong-light HDR input max=92; Bloom input max=112. Source/output numerical comparisons use packed-format-aware
  scene tolerances P99<=0.016/max<=0.03. No-Volume/reopen pixel drift <=0.02; source frame variation and R11G11B10
  precision are reported separately from strict float vector testing. Mode and Bloom comparison images reviewed.
- Owned Editor exits 0, original shared configuration/layout/log bytes restored. Persistent backups and hashes in report.
- Targeted git diff --check and Python syntax checks: PASS. No commit created.

Evidence links: `summary.json`, `report.md`, `gpu_final_checked/numeric_analysis.json`,
`editor_hdr/analysis.json`, `editor_hdr/summary.json`, `editor_hdr/comparison.png` under the evidence root.

Verification limits: manual Inspector mouse-click, prefab editor UI workflow and target-monitor colorimetric acceptance
were not exercised. Prefab document persistence was tested, Scene persistence was tested in restarted Editor.
Existing startup PSO format/DSV warnings remain; no globally warning-free Editor claim is made. Early harness crashes
were caused by missing standalone scene/pipeline/frame allocator/current-camera ordering, then fixed in the test.
First cold full-pipeline DXR compilation timed out; accepted final run uses warmed cache and exits normally.
The None change deliberately adds the existing fullscreen ToneMapping pass/RT to paths that previously bypassed it;
no new pass class or second color transform is introduced. GPU performance benchmarking is not part of this acceptance.

Minimal Rec.709 AgX is an approximation, not full Blender OCIO/Looks. It does not reproduce the race_spaceship
Standard/Gamma settings automatically and does not repair raster area-light capacity.
