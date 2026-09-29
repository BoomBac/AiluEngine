# AiluEngine PathTracer 收敛 / Temporal 优化开发任务包

> 目标：解决当前 PathTracer 收敛慢、Temporal 对旧历史过度依赖、动态场景响应迟缓，以及部分高方差采样问题。  
> 执行方式：**严格阶段式开发。上一阶段全部验收通过后，才能进入下一阶段。**  
> 禁止跨阶段顺手重构无关模块。每一阶段都要保持工程可编译、可运行、可独立回退。

---

## 0. 当前问题摘要

当前主要问题：

1. `raytrace_gi.hlsl::Denoise()` 在简单 history running-average 后立即 `return`。
   - 后面的 3×3 neighborhood clamp；
   - motion 自适应；
   - luminance delta 自适应；
   - 全部属于死代码。

2. Temporal history 最长可积累到 `4096` 帧。
   - 成熟 history 的当前帧权重最低只有 `1 / 4096`；
   - 材质、灯光、环境、物体变化后，旧历史可能需要非常久才能淡出。

3. History reset 条件不足。
   当前 CPU 端只在以下情况 reset：
   - 相机 VP 变化；
   - temporal 刚开启；
   - history target 重建。

   当前未覆盖：
   - 材质变化；
   - 灯光变化；
   - Environment 变化；
   - Transform / Geometry 变化；
   - PathTracer 参数变化；
   - MNEE 等算法开关变化。

4. Temporal 当前没有完整的 surface validation。
   - 只有 motion vector reprojection；
   - 没有可靠的 depth / normal / position rejection；
   - disocclusion 与运动区域容易复用错误 history。

5. `PrimaryRay -> TracePath()` 当前本质仍是 **1 spp / pixel / frame**。
   - `PER_FRAME_SAMPLES = 4` 属于另一条 ReSTIR/RIS 路径；
   - 不等于 PathTracer 每帧 4 spp。

6. PathTracer 当前使用 PCG 白噪声序列。
   - 没有 Sobol / Owen 等低差异时间序列；
   - 静态 progressive accumulation 收敛效率一般。

7. Environment 目前没有 Importance Sampling / NEE。
   - 环境光只有 path miss 时才被采样；
   - HDRI、小太阳、高动态环境尤其容易产生高方差。

8. Unified Light 当前按 `1 / light_count` 均匀选灯。
   - 亮度差别大时方差高；
   - emissive mesh 每个 triangle 都可能成为一个 UnifiedLight，进一步稀释有效采样概率。

9. GGX VNDF 已有实现，但没有启用。
   - 当前普通 GGX NDF sampling 在 grazing / rough specular 下存在更多无效样本。

10. `kMaxPathDepth = 3`。
    - 这是偏差问题，不是 temporal 收敛问题；
    - 高阶间接光可能永远缺失。

---

# 总体执行规则

## 强制规则

每个阶段完成后必须：

- [ ] 工程完整编译通过；
- [ ] Shader 编译通过；
- [ ] Editor 可正常启动；
- [ ] PathTracer 功能可正常开启；
- [ ] 本阶段的专项验收全部通过；
- [ ] 没有引入明显新 artifact；
- [ ] 没有破坏 DXR / Compute 两条已有 PathTracer 路径；
- [ ] 输出一份阶段测试记录。

**只有全部通过后才能进入下一阶段。**

如果任一验收失败：

1. 不允许继续做下一阶段；
2. 优先修复当前阶段；
3. 若修复复杂度明显扩大，应回退当前阶段改动；
4. 禁止用“后续阶段会解决”作为当前阶段验收通过的理由。

---

# 阶段 1：拆分 Raw PathTrace 与 Temporal History

## 目标

先修正当前 Temporal 的数据流，避免 `_CurrentTarget` 同时作为：

- raw PathTracer 输入；
- temporal neighborhood 输入；
- temporal 输出；

从而产生 UAV 同 dispatch 读写不确定性。

本阶段**只改数据流，不改变 Temporal 算法本身**。

---

## 涉及文件

重点：

- `Engine/Src/Render/Features/RayTraceGI.cpp`
- `Engine/Inc/Render/Features/RayTraceGI.h`
- `Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl`

---

## 要求

增加独立的：

```text
PathTraceRaw
HistoryPrev
HistoryCurr
```

推荐数据流：

```text
PrimaryRay
    ↓
PathTraceRaw
    ↓
TemporalResolve
    ├── Read PathTraceRaw
    ├── Read HistoryPrev
    └── Write HistoryCurr
         ↓
      Final GI
```

禁止：

```text
TemporalResolve:
    同时读写同一张 CurrentTarget
```

---

## 实现建议

`GIPass` 至少维护：

```cpp
Ref<Texture2D> _raw_path_trace_texture;
Ref<Texture2D> _history_texture_a;
Ref<Texture2D> _history_texture_b;
bool _is_history_a = true;
```

RenderGraph：

```text
Pass 1: PathTrace
    Write RawPathTrace

Pass 2: Temporal
    Read RawPathTrace
    Read PrevHistory
    Write CurrHistory

Pass 3: Blit / Debug
    Read CurrHistory
```

Temporal 关闭时：

```text
Final = RawPathTrace
```

---

## 本阶段禁止事项

本阶段禁止：

- 不要加入 depth/normal reject；
- 不要加入 variance estimation；
- 不要加 spatial filter；
- 不要做 Environment Importance Sampling；
- 不要改 light selection；
- 不要改 RNG；
- 不要改 Path Depth。

---

## 验收项

### A. 资源正确性

- [ ] Raw PathTrace 纹理与 History 纹理完全独立；
- [ ] Temporal pass 不再对同一纹理做 neighborhood read + output write；
- [ ] history A/B ping-pong 正确；
- [ ] resize 后所有纹理尺寸正确重建；
- [ ] temporal 关闭时输出 raw PathTracer。

### B. 静态画面测试

固定相机、固定场景：

- [ ] 第 1 帧可看到正常 raw noise；
- [ ] 开 temporal 后，画面逐帧稳定；
- [ ] history ping-pong 不出现每两帧闪烁；
- [ ] 不出现全黑 / NaN / 爆亮。

### C. Debug 验证

增加临时调试模式，可显示：

```text
RawPathTrace
PrevHistory
CurrHistory
```

必须确认三者确实为独立资源。

---

## 阶段 1 通过条件

**所有 A/B/C 项全部通过。**

否则禁止进入阶段 2。

---

# 阶段 2：实现正确的 Temporal Surface Validation

## 目标

解决 history 只靠 motion vector 重投影的问题。

增加：

- UV validity；
- depth rejection；
- normal rejection；
- 可选 position rejection；
- disocclusion rejection。

---

## 推荐输入

Temporal pass 增加读取：

```text
MotionVector
Depth
Normal
PrevDepth / PrevSurface
```

如果已有 `_surface_buffer_a/b` 可复用，则优先复用，不额外创建冗余 GBuffer history。

---

## 推荐验证逻辑

伪代码：

```hlsl
bool ValidateHistory(...)
{
    if (!inside_screen)
        return false;

    if (abs(current_depth - history_depth) > depth_threshold)
        return false;

    if (dot(current_normal, history_normal) < normal_threshold)
        return false;

    return true;
}
```

初始建议：

```text
normal_threshold = 0.90 ~ 0.95
depth_threshold  = 根据 linear depth 做相对阈值
```

不要直接长期使用固定 `0.02` 的 normalized-depth 阈值。

更推荐：

```hlsl
float depth_error = abs(history_depth - expected_depth);
float relative_error = depth_error / max(current_depth, 1e-3);
```

---

## History invalid 时

必须：

```hlsl
output = current;
history_length = 1;
```

不能继续混入旧 history。

---

## 本阶段建议加入 History Length

为后续 temporal 使用独立 per-pixel history length：

```text
R8_UINT / R16_UINT
```

或打包进额外 texture channel。

推荐：

```hlsl
uint history_length;
```

不要继续只依赖全局 `_frame_index` 表示所有像素的 history 长度。

---

## 验收项

### A. 静态相机

- [ ] 静态场景下绝大多数像素 history validation 成功；
- [ ] accumulation 可以持续增长；
- [ ] 不出现随机 history reset。

### B. 相机移动

慢速平移相机：

- [ ] 静态物体表面 history 可被正常 reprojection；
- [ ] 新露出的区域立即使用 current sample；
- [ ] disocclusion 区域不会拖着旧画面。

### C. 法线边界

测试球体、墙角：

- [ ] 不允许墙面 history 泄漏到相邻面；
- [ ] silhouette 周围没有明显黑/亮拖影。

### D. Debug View

增加 history validity debug：

```text
绿色 = accepted
红色 = rejected
```

另一个 debug：

```text
history length heatmap
```

必须能观察到：

- 静止区域 history length 增长；
- 新露出区域重新从 1 开始。

---

## 阶段 2 通过条件

所有 A/B/C/D 项全部通过。

否则禁止进入阶段 3。

---

# 阶段 3：建立 Scene / Lighting / Material History Revision

## 目标

解决场景改变后仍然继续积累旧 history 的问题。

---

## 核心设计

PathTracer Temporal 不应该只看 Camera。

增加：

```cpp
struct PathTracerHistoryRevision
{
    u64 _camera_revision;
    u64 _scene_revision;
    u64 _material_revision;
    u64 _lighting_revision;
    u64 _environment_revision;
    u64 _settings_revision;
};
```

也可以使用统一：

```cpp
u64 _history_signature;
```

由多个 revision/hash 合成。

---

## 至少要覆盖的变化

### Camera

- camera transform；
- projection；
- near/far；
- output size。

### Scene Geometry

- object transform；
- mesh replacement；
- skinned mesh relevant geometry change；
- entity enable/disable。

### Material

- base color；
- roughness；
- metallic；
- emission；
- transmission；
- IOR；
- absorption / attenuation；
- material texture。

### Lighting

- light add/remove；
- position；
- direction；
- intensity；
- color；
- shape/radius/range。

### Environment

- cubemap；
- rotation；
- intensity。

### PathTracer Settings

至少：

- MNEE；
- max depth；
- temporal mode；
- environment MIS enable；
- light sampler mode；
- spp。

---

## Reset 策略

任何影响 unbiased target 的全局场景变化：

```text
ResetAllHistory()
```

不要试图在本阶段做局部 invalidation。

简单可靠优先。

---

## MAX_ACCUMULATED_FRAMES 调整

本阶段将实时 temporal 与 reference accumulation 开始区分。

建议：

```text
Realtime Temporal:
max_history = 32 ~ 64

Reference Progressive:
max_history = 很大 / unlimited running average
```

不能再让实时 temporal 默认 4096。

---

## 验收项

逐项修改并观察 history reset：

- [ ] 改 BaseColor -> reset；
- [ ] 改 Roughness -> reset；
- [ ] 改 Metallic -> reset；
- [ ] 改 Emission -> reset；
- [ ] 改灯光强度 -> reset；
- [ ] 移动灯光 -> reset；
- [ ] 环境旋转 -> reset；
- [ ] 环境强度 -> reset；
- [ ] 移动物体 -> reset；
- [ ] 切换 MNEE -> reset。

同时：

- [ ] 什么都不改时不得误 reset；
- [ ] Editor UI hover / unrelated property change 不得 reset；
- [ ] static scene 下 history 可持续增长。

---

## 关键验收测试

让 history 积累至稳定后：

1. 将白色材质改成红色；
2. 观察下一帧。

必须：

```text
1~2 帧内明显切换为新的 noisy 红色结果；
随后重新收敛。
```

不能出现：

```text
旧白色结果几十秒后才慢慢消失。
```

---

## 阶段 3 通过条件

所有修改项能正确 reset，且静态场景无误 reset。

否则禁止进入阶段 4。

---

# 阶段 4：启用 Temporal Neighborhood Clamp

## 目标

抑制 firefly 和高方差 sample 长时间污染 history。

---

## 数据流要求

必须基于阶段 1 的：

```text
RawPathTrace -> Temporal
```

进行。

Neighborhood 必须从：

```text
RawPathTrace
```

读取。

不能读取正在写入的 temporal output。

---

## 第一版实现

先实现简单 3×3 RGB min/max clamp：

```hlsl
history = clamp(history, neighborhood_min, neighborhood_max);
```

随后建议升级到 luminance/chroma 或 YCoCg clamp。

第一版优先保证正确，不要求马上做到最优视觉。

---

## Current Weight

Realtime Temporal：

```hlsl
current_weight = max(
    1.0 / history_length,
    adaptive_weight
);
```

adaptive 可以考虑：

```text
motion magnitude
luminance difference
history validity
```

参考范围：

```text
静止稳定区域：0.03 ~ 0.08
轻微运动：0.10 ~ 0.20
明显运动 / lighting mismatch：直接 reject
```

Reference Progressive 模式禁止使用这种 biased clamp。

---

## 必须区分两种模式

### 模式 A：Reference

```text
严格 progressive accumulation
1/N
无 history clamp 或只提供 debug 可选开关
目标：物理参考
```

### 模式 B：Realtime

```text
finite history
history reject
neighborhood clamp
adaptive blend
目标：快速稳定
```

两者不能继续混为一个 `_is_temporal_denoise`。

建议枚举：

```cpp
enum class EPathTracerAccumulationMode
{
    kDisabled,
    kReference,
    kRealtime
};
```

---

## 验收项

### Firefly 测试

构造：

- 低 roughness；
- 小而亮 Area Light；
- 或 emissive triangle。

要求：

- [ ] Realtime 模式中单个 firefly 不应污染几十帧；
- [ ] 1~5 帧内明显被 clamp/稀释。

### Reference 对照

- [ ] Reference 模式不能偷偷启用 biased temporal clamp；
- [ ] Reference 模式结果长期平均应保持稳定。

### Motion

- [ ] Realtime 模式相机运动不产生明显长尾拖影；
- [ ] 停止运动后可以较快恢复稳定。

---

## 阶段 4 通过条件

Realtime 与 Reference 两种模式行为明确区分，并通过上述测试。

否则禁止进入阶段 5。

---

# 阶段 5：Environment Importance Sampling + MIS

## 目标

降低 Environment Light 场景的主要方差。

当前 Environment 只有 path miss estimator，需要加入显式采样。

---

## 第一版范围

只支持静态 Environment Cubemap。

需要：

1. 根据 cubemap luminance 构建采样分布；
2. 考虑 cubemap texel solid angle；
3. 支持从 distribution 采样方向；
4. 返回正确 environment PDF；
5. 在 path surface 上加入 Environment NEE；
6. 与 BSDF sampling 做 MIS；
7. BSDF ray miss environment 时使用对应 MIS 权重。

---

## 推荐架构

CPU 或 GPU 预构建：

```text
EnvironmentDistribution
```

可选：

```text
Alias Table
Hierarchical CDF
```

轻量实现优先 Alias/CDF。

权重必须包含：

```text
luminance * solid_angle
```

不能只按 texture luminance。

---

## 新接口建议

```hlsl
struct EnvironmentSample
{
    float3 _direction;
    float3 _radiance;
    float _pdf;
};
```

接口：

```hlsl
EnvironmentSample SampleEnvironment(inout RandomCtx random_ctx);
float PdfEnvironment(float3 direction);
```

---

## MIS

Light-sampled environment：

```text
p_light = environment_pdf
p_bsdf  = EvalBRDF pdf

weight = PowerHeuristic(p_light, p_bsdf)
```

BSDF miss environment：

```text
weight = PowerHeuristic(p_bsdf, p_environment)
```

Delta BSDF 例外。

---

## 本阶段禁止事项

- 不做 ReSTIR GI；
- 不做 Environment Reservoir；
- 不做 path guiding；
- 不做 neural denoise；
- 不重构整个 EnvironmentSystem。

---

## 验收项

### A. 均匀天空

使用近似均匀 environment：

- [ ] 新旧结果平均亮度基本一致；
- [ ] 不允许明显 energy bias。

### B. 小太阳 HDRI

构造：

```text
暗天空 + 小面积高亮区域
```

比较相同帧数：

```text
旧版：BSDF-only environment
新版：Env NEE + MIS
```

要求：

- [ ] 新版噪声肉眼明显降低；
- [ ] 不出现新的亮度偏差；
- [ ] 不出现太阳贡献 double count。

### C. Reference

建议 512~2048 spp 比较：

- [ ] 新旧均值应趋于一致；
- [ ] 新版方差明显更低。

---

## 阶段 5 通过条件

必须确认“降低方差”而不是“简单压暗高亮”。

否则禁止进入阶段 6。

---

# 阶段 6：Unified Light Power-Weighted Sampling

## 目标

替代：

```text
light_index ~ Uniform(0, light_count)
```

降低多灯场景方差。

---

## 第一版权重

Analytical light 可使用近似：

```text
estimated_power =
    luminance(radiance)
    * shape_area_or_solid_angle_factor
```

不要求绝对物理精确，但必须：

- 始终非负；
- 与真实贡献大致正相关；
- PMF 与 sampler 完全一致。

---

## 需要新增

```text
UnifiedLightSamplingTable
```

支持：

```hlsl
uint SampleUnifiedLightIndex(float random_value, out float selection_pmf);
float GetUnifiedLightSelectionPMF(uint light_index);
```

注意：

当前接口：

```hlsl
GetUnifiedLightSelectionPMF(light_count)
```

只能返回 uniform PMF，需要改成与 `light_index` 相关。

---

## MIS 必须同步

所有原本使用：

```hlsl
1 / light_count
```

的地方都必须检查。

包括但不限于：

- NEE；
- hit light MIS；
- MNEE；
- mesh light；
- 其他使用 UnifiedLight 的 PDF 逻辑。

---

## 验收项

### A. 单灯场景

- [ ] 与旧版结果一致；
- [ ] PMF = 1。

### B. 两灯亮度差测试

例如：

```text
Light A intensity = 1
Light B intensity = 100
```

要求：

- [ ] B 被明显更高概率采样；
- [ ] 最终平均亮度与 uniform sampler 一致；
- [ ] 同 spp 下噪声明显降低。

### C. Debug

提供采样频率统计：

```text
light_index
expected_pmf
actual_frequency
```

跑足够多 sample 后实际频率应接近期望 PMF。

---

## 阶段 6 通过条件

采样频率、PMF、MIS 三者一致，结果无明显 bias。

否则禁止进入阶段 7。

---

# 阶段 7：Emissive Mesh 二级采样

## 目标

避免 emissive mesh 每个 triangle 都直接占一个顶层 UnifiedLight 槽位，导致大型 emissive mesh 污染 top-level light selection。

---

## 新采样层级

推荐：

```text
Top Level:
    Analytical Light
    Emissive Mesh A
    Emissive Mesh B
    Environment

Emissive Mesh:
    Triangle 0
    Triangle 1
    Triangle 2
    ...
```

---

## Triangle 权重

建议：

```text
triangle_weight =
    area
    * average_emission_luminance
```

第一版可以：

```text
area * material_emission_luminance
```

暂时不用对 emissive texture 每个 triangle 做精确积分。

---

## PDF

最终必须满足：

```text
p(light sample)
=
p(select emissive mesh)
*
p(select triangle | mesh)
*
p(sample point | triangle)
```

MIS 必须使用完整 PDF。

---

## 验收项

### A. 高面数 emissive mesh

创建：

```text
1 个 5000+ triangles emissive mesh
1 个 analytical light
```

要求：

- [ ] analytical light 不再只有约 `1 / 5001` 的顶层采样概率；
- [ ] emissive mesh 只占一个 top-level light item；
- [ ] triangle 选择频率符合 triangle PMF。

### B. Energy

- [ ] 重构前后长期平均结果一致；
- [ ] 不允许 emissive mesh 变亮/变暗。

### C. Variance

相同 spp：

- [ ] 新版明显减少 direct-light noise。

---

## 阶段 7 通过条件

二级 PMF 与最终 PDF 正确，长期均值无明显变化。

否则禁止进入阶段 8。

---

# 阶段 8：GGX VNDF Sampling

## 目标

降低 glossy / grazing angle 下的无效采样和高方差。

---

## 当前已有

代码中已有：

```hlsl
SampleGGX_VNDF(...)
PdfGGX_VNDF(...)
```

但宏：

```hlsl
//#define SAMPLE_BRDF_VNDF_GGX 1
```

当前未启用。

---

## 实施要求

不能只打开宏。

必须逐项检查：

- Sample direction；
- BRDF eval；
- PDF；
- MIS；
- glass reflection；
- opaque specular；
- transmission 是否应该独立处理。

如果 glass rough transmission 尚未完整支持 VNDF，本阶段可以只对 opaque reflection 启用。

---

## 验收项

### A. 白炉测试 / Energy Test

至少覆盖：

```text
roughness = 0.05
roughness = 0.2
roughness = 0.5
roughness = 1.0

metallic = 0 / 1
```

要求：

- [ ] 无明显 energy gain；
- [ ] 无明显 energy loss；
- [ ] 不出现 NaN。

### B. Grazing Test

低 roughness 球体 + 强光：

- [ ] 无效 sample 数量降低；
- [ ] 同 spp 噪声明显改善或至少不恶化。

### C. MIS

- [ ] NEE + BSDF MIS 不出现亮度变化；
- [ ] hit light MIS 不出现双计数。

---

## 阶段 8 通过条件

VNDF 路径在能量和 PDF 上正确。

否则禁止进入阶段 9。

---

# 阶段 9：低差异 Temporal RNG

## 目标

提升静态 progressive accumulation 的 sample efficiency。

---

## 不建议

不要简单把现有 `blue_noise.jpg` 当成完整 path RNG。

已有 RTXDI 注释本身也指出：

```text
table-based blue noise 不足以提供完整高维 path sampling entropy
```

---

## 推荐方向

优先：

```text
Sobol + Owen Scrambling
```

或者：

```text
R2 / PMJ / scrambled low-discrepancy
```

至少保证维度按：

```text
pixel
frame/sample_index
bounce
sample_dimension
```

区分。

不要继续只依赖：

```text
PCG(seed++)
```

生成整个 path 的所有维度。

---

## RandomCtx 建议

演进为：

```hlsl
struct RandomCtx
{
    uint2 _pixel;
    uint _sample_index;
    uint _dimension;
};
```

提供：

```hlsl
float Rand(inout RandomCtx ctx);
float2 Rand2(inout RandomCtx ctx);
```

内部根据 dimension 读取低差异序列。

---

## 验收项

固定静态场景，对比：

```text
PCG
vs
Low Discrepancy
```

在：

```text
16 spp
32 spp
64 spp
128 spp
```

截图比较。

要求：

- [ ] 同 spp 下空间噪声肉眼可见降低；
- [ ] 长期平均结果一致；
- [ ] 不出现明显固定 pattern；
- [ ] 不出现跨像素规则条纹；
- [ ] 不出现重复周期。

---

## 阶段 9 通过条件

低差异序列确实改善 sample efficiency，且不引入明显 pattern。

否则不进入最终阶段。

---

# 阶段 10：SPP / Path Depth / Quality Preset

## 目标

最后才增加纯算力换质量的控制项。

---

## 新增配置

推荐：

```cpp
u32 _samples_per_pixel = 1;
u32 _max_path_depth = 3;
```

Preset：

```text
Realtime:
    spp = 1
    depth = 2~3
    realtime temporal

High:
    spp = 2~4
    depth = 4~6

Reference:
    spp = 1~8
    depth = 8~12
    reference accumulation
```

---

## PrimaryRay

支持：

```hlsl
float3 sum = 0;

for sample_index:
    sum += TracePath(...);

color = sum / spp;
```

每个 spp 必须使用不同：

```text
sample_index / RNG dimension
```

不能重复同一序列。

---

## Russian Roulette

保留现有 RR，但需要检查：

```text
depth >= 3 后开始
```

以及 survival probability 是否需要限制：

```text
0.05 <= p <= 0.95
```

这一项以测试结果决定，不强制修改。

---

## 验收项

### SPP

- [ ] 1/2/4 spp 输出长期均值一致；
- [ ] 2 spp 噪声低于 1 spp；
- [ ] 4 spp 噪声低于 2 spp；
- [ ] GPU 时间大致随 spp 合理增长。

### Depth

Cornell Box / 多次反射场景：

- [ ] depth 3 / 6 / 12 能观察到逐步增加的高阶间接光；
- [ ] depth 6 与 12 差异明显小于 depth 3 与 6；
- [ ] 不出现能量爆炸。

### Reference

- [ ] 静态 reference 可以持续 progressive accumulation；
- [ ] 修改场景后会立即 reset；
- [ ] 不使用 realtime temporal 的 biased clamp。

---

# 最终验收矩阵

所有阶段完成后，必须通过以下最终测试。

| 场景 | 重点 | 必须结果 |
|---|---|---|
| Cornell Box | 多次 diffuse bounce | 稳定，无明显 bias |
| Glossy Sphere | GGX / VNDF | firefly 显著减少 |
| Mirror / Low Roughness | delta/specular | 无异常黑点 |
| Glass Sphere | transmission/MIS | 不出现系统性黑边 |
| HDRI Small Sun | Environment MIS | 收敛明显快于旧版 |
| Many Analytical Lights | Light PMF | power sampling 有效 |
| Large Emissive Mesh | Hierarchical light sampling | 不再被 triangle 数量稀释 |
| Moving Camera | Reprojection | 无严重拖影 |
| Moving Object | History reject | 不残留旧物体 |
| Material Edit | Revision reset | 下一帧立即重新积累 |
| Light Edit | Revision reset | 下一帧立即重新积累 |
| Environment Edit | Revision reset | 下一帧立即重新积累 |

---

# 必须保留的 Debug 能力

最终建议保留：

```text
PathTracer Debug View:
    RawRadiance
    TemporalResult
    History
    HistoryValidity
    HistoryLength
    MotionVector
    ReprojectedUV
    LightSelectionPMF
    EnvironmentPDF
    BSDFPDF
    MISWeight
    PathDepth
```

这些 debug view 对后续继续开发 ReSTIR GI / Path Guiding 非常重要。

---

# 推荐提交拆分

严格按阶段提交：

```text
PT-01 Split raw path trace and temporal history
PT-02 Add temporal surface validation
PT-03 Add path tracer history revision/reset
PT-04 Add realtime temporal clamp
PT-05 Add environment importance sampling and MIS
PT-06 Add power weighted light sampling
PT-07 Add hierarchical emissive mesh sampling
PT-08 Enable and validate GGX VNDF
PT-09 Add low discrepancy path sampling
PT-10 Add SPP/depth quality presets
```

每个 commit 必须：

- 可单独编译；
- 可单独运行；
- 有对应验收记录；
- 不依赖下一阶段才能“变正确”。

---

# 轻量模型执行要求

实现过程中必须遵守：

1. 不允许一次完成多个阶段。
2. 每次只实现当前阶段。
3. 当前阶段完成后，先输出：
   - 修改文件；
   - 核心改动；
   - 验收步骤；
   - 已验证结果；
   - 未通过项。
4. 只有所有验收项为 Pass，才能开始下一阶段。
5. 如果验收失败，必须继续修复当前阶段。
6. 不允许把失败项标记为“后续处理”然后进入下一阶段。
7. 不允许重构无关 RenderGraph、Material、Scene、RTXDI、DDGI、Surfel GI 模块。
8. 对 PDF / MIS / sampler 的任何修改必须同步验证：
   ```text
   sampling distribution
   evaluation pdf
   MIS pdf
   ```
9. 任何为了“去噪”而直接 clamp 最终 radiance 的方案，都不能作为 Reference 模式的默认行为。
10. 优先正确性，其次才是性能优化。

---

# 推荐第一轮实际执行范围

不要一次把 10 个阶段全部交给模型实现。

第一轮只执行：

```text
阶段 1
阶段 2
阶段 3
阶段 4
```

也就是先把：

```text
PathTracer Temporal 基础设施
```

彻底修正确。

完成并验收后，再进行：

```text
阶段 5~9：降低 estimator variance
```

最后：

```text
阶段 10：质量档位 / spp / depth
```

这样问题定位最清晰，也最容易判断某一项修改到底有没有真正改善收敛速度。
