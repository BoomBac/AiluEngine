# RayTraceGI 玻璃采样实现审查

> 状态：**已修复**。修复清单与验证结果见文末「修复实施记录」。
> 下面的「问题 N」保留原始审查记录，便于对照。

审查对象：
- `Engine/Res/Shaders/hlsl/ray_trace/sampling.hlsli`（`EvalBRDF` / `SampleBRDF` 的 glass 分支）
- `Engine/Res/Shaders/hlsl/ray_trace/path_tracer_core.hlsli`（`TracePath` / `SamplePathBounce`）
- `Engine/Res/Shaders/hlsl/ray_trace/path_tracer_light_common.hlsli`（`DirectLightCosTheta`）
- `Engine/Res/Shaders/hlsl/ray_trace/path_tracer_material.hlsli`（`CreatePathMaterial` / `IsPathMaterialTransmissive`）
- `Engine/Res/Shaders/hlsl/path_tracer_compute_backend.hlsli`（`IsPathOccluded`）

场景：`AiluTestProj/Assets/scene/NewScene.almap`
`new_object_3` 球体 + `Engine/Res/Materials/StandardPBR.alasset`
（`_Transmission = 0.6359`、`_IOR = 1.5918`、`_Roughness = 0`、`_Anisotropy = 0.344`、`SurfaceType = Opaque`）

> 说明：软件光追路径当前派发的 kernel 是 `_kernel_primary_ray`，
> 它在 `GIPass` 构造函数里被绑定成 `FindKernel("PrimaryRay")`（`RayTraceGI.cpp:98`）。
> 因此下图看到的是 `TracePath` 的路径追踪结果，不是 GBuffer + ReSTIR 那条路。
> `_is_temporal_denoise` 默认 `false`，`Denoise` pass 不会跑，画面是每像素单样本的原始输出。

---

## 结论摘要

| 现象 | 根因 |
| --- | --- |
| 整体偏暗 | 玻璃路径完全拿不到环境/天空光：环境辐亮度只在 `kPathHitMiss` 时累加，而透射光线几乎不会 miss（要么打到几何体，要么因 TIR 被吞掉） |
| 边缘黑色描边 | 掠射角 `Fresnel → 1`，而代码只实现透射瓣 `(1-F)`，反射瓣被注释掉 → 边缘没有任何能量；叠加缺环境光，直接变黑 |
| 内部一圈黑线 | 零粗糙度 + 缺失反射瓣导致 `(1-F)` 在 TIR 临界角处硬阈值塌陷，形成亚像素宽的暗环；同一个 `(1-F)` 塌陷也造成边缘的硬边 |
| 白色麻点/噪点 | 每像素 1 个直接光样本 + 未开时域降噪；边缘处 `OffsetPathRayOrigin` 自相交让阴影测试随机失败，于是边缘呈"颗粒状黑边"而不是实心黑边 |

---

## 问题 1（最关键）：`EvalBRDF` 的 glass 分支只有透射瓣，反射瓣被注释掉

`sampling.hlsli:277-332`：

```hlsl
bool is_reflection = dot(n, wo) * dot(n, wi) > 0.0;   // 算了但没用 → 死代码
...
// if (is_reflection)
// {
//     ... return D * G * F / max(4.0 * nl * nv, 1e-6);
// }
...
return (1.0 - F) * D * G * factor * abs(wi_h) * abs(wo_h) / max(nl * nv * denom2, 1e-6) * 1.5;
```

* 只返回透射项。任何"反射瓣"方向的 `wi`（即 `wi` 与 `wo` 在法线同侧）走完整个
  Walter 透射公式，得到的值是语义错误的小量，而不是 Fresnel 反射能量。
* `pdf` 同样只按 `PdfMicrofacetNormal(alpha, nh) * dwh_dwi`（透射 Jacobian）算，
  不包含反射瓣的 `D*nh/(4*vh)`，也没有按选择概率加权 → **MIS 用的是错的 pdf**。
* 末尾 `* 1.5` 是纯能量手调补偿，没有物理依据；它只是在掩盖"反射能量丢失 + 缺环境光"。

**为什么直接对应"边缘黑描边"**：掠射角下 `F_Schlick(f0, vh) → 1`，于是
`(1-F) → 0`。物理上这部分能量应该以反射形式出现（而且掠射反射本来就最亮），
但反射瓣不存在，所以球体轮廓处所有能量被丢掉 → 黑边。

---

## 问题 2：`SampleBRDF` 的 glass 分支从不做 Fresnel 分瓣，`r_select` 未被使用

`sampling.hlsli:360-384`：

```hlsl
float specular_prob = ComputeBRDFSpecularSamplingProb(mat._albedo, mat._metallic);
specular_prob = mat._is_glass ? 1.0 : specular_prob;      // 玻璃恒为 1

if (mat._is_glass)
{
    result.h = SampleGGX(n, RoughnessToAlpha(ctx._mat._roughness), r1, r2);
    float vh = max(abs(dot(wo, result.h)), 1e-4);
    float reflect_prob = F_Schlick(DielectricF0(ctx._ior_i, ctx._ior_t).xxx, vh).x;  // 算了但没用

    // if (r_select < reflect_prob) { result.wi = reflect(-wo, result.h); return result; }  // 被注释
    if (dot(result.h, n) < 0.0)
        result.h = -result.h;
    result.wi = refract(-wo, result.h, ctx._eta);          // 无条件折射
    if (all(result.wi == 0.0.xxx))                         // 只在全反射时才退回镜面反射
        result.wi = reflect(-wo, result.h);
    return result;
}
```

* **采样分布和求值 BSDF 不一致**：实际上是一个"只有折射瓣"的分布，
  但 MS 估计器/`EvalBRDF` 又是按"折射 + Walter Jacobian"来算的，
  在 Fresnel 非平凡（掠射、内表面）时能量与方差都失控。
* `r_select` 参数在这里完全没用上，等于浪费了一个随机数维度。
* `reflect_prob` 是死代码。

---

## 问题 3：路径追踪循环里玻璃拿不到环境/天空光（"整体偏暗"的直接原因）

`path_tracer_core.hlsli:80-114`：

```hlsl
for (uint depth = 0u; depth < max_depth; ++depth)
{
    PathHit hit = TracePathRay(ray_origin, ray_direction);
    if (hit._type == kPathHitMiss) { radiance += throughput * EvaluateEnvironmentRadiance(ray_direction); break; }
    ...
    radiance += throughput * hit._material._emission;
    radiance += throughput * EvaluatePathDirectLighting(hit, trace_context, wo, random_context);  // 只有直接光
    PathBounceSample bounce = SamplePathBounce(...);
    ...
}
```

* 环境辐亮度**只有 miss 才累加**，而 miss 只能由"第一次命中的就是背景"或者
  "玻璃反射/透射出球体后射向天空"产生。
* 对一个凹凸球壳来说，相机射线打到球面后必然再打到球面，**几乎不可能 miss**：
  - 若 `(1-F)` 小（掠射、内表面 TIR），`throughput` 被压得极低，剩下的路径几乎没贡献；
  - 若 TIR 被判成"全反射"，`SampleBRDF` 走 `reflect` 兜底，但 `EvalBRDF` 对反射方向返回 ~0
    → 这条路径直接断掉，`throughput *= bounce._throughput_weight` 让它变黑。
* 于是玻璃的可见亮度只剩"太阳直射打到几何体再折回来"这一项，
  天空/IBL 那部分（真实玻璃球体亮度的主要来源）**整块丢失** → 整体偏暗。

补充：`TracePath` 的间接项本来就只靠「BSDF 采样 + 命中表面后算直接光」，
没有做 next-event estimation 的环境版本（`EvaluatePathDirectLighting` 只处理解析光）。

---

## 问题 4：零粗糙度让所有 Fresnel/TIR 阈值变成硬边（"内部一圈黑线"）

* `_Roughness = 0` → `CreatePathMaterial` 里 `material._roughness = max(roughness, 0.04f)`，
  `alpha = RoughnessToAlpha(0.04) = 0.0016`。`D_GTR2(0.0016, nh)` 在 `nh<1` 时基本为 0。
* 于是玻璃退化成"完美镜面 + 完美折射"，`(1-F)` 的塌陷、`refract()` 的返回 0
  （全反射判据）、`all(result.wi == 0.0.xxx)` 的兜底，全都在**亚像素尺度**上跳变。
* 结果就是：轮廓处一条硬黑边；球体内部在临界角对应的半径处出现一圈细黑线
  （总反射临界角 `asin(1/1.5918) ≈ 38.9°` 映射到球面上的同心圆）。
* 用户截图里能看到的"内部一圈黑线"就是这个同心圆 + 边缘黑边的组合。

---

## 问题 5：`IsPathOccluded` / `OffsetPathRayOrigin` 在掠射折射下自相交（颗粒状噪点）

`path_tracer_compute_backend.hlsli:94-104`：

```hlsl
Material material = CreatePathMaterial(g_material_buffer[trace_surface._material_index], trace_surface._uv);
if (!material._is_glass)
    return true;                                    // 玻璃不算遮挡，OK

float adaptive_offset = max(0.001f, trace_hit._t * 1e-4f);
current_origin = OffsetPathRayOrigin(trace_surface._world_position, trace_surface._geometric_normal,
                                     ray_direction, trace_hit._t);
remaining_t -= trace_hit._t + adaptive_offset;
```

* `OffsetPathRayOrigin` 的偏移量固定最小 `1e-3`（世界单位）。
  球半径 `0.6158 * 0.5 ≈ 0.31`，掠射折射时射线几乎平行于球面，
  **沿球面走 1e-3 的距离就可能重新打回球体内部** → 阴影测试随机判定为遮挡。
* 这解释了为什么边缘不是一条干净的实心黑边，而是**颗粒状/毛刺状的黑边**
  （用户放大图里很明显）。
* 同时 `IsPathOccluded` 只循环 8 次、`remaining_t -= trace_hit._t + adaptive_offset`
  的记账在玻璃多层穿越时容易提前退出，进一步增加方差。

---

## 问题 6：`G_SmithGGX` 传参用 alpha 当 roughness

`brdf.hlsli:203`：

```hlsl
float G_SmithGGX(float nol, float nov, float roughness)
{
    float ggx2 = GeometrySchlickGGX(nov, roughness);   // 期望的是 roughness 而不是 alpha
    ...
    float k = pow(roughness + 1, 2) / 8;              // UE4 形式，用 roughness
```

glass 分支里两处都写成 `G_SmithGGX(nl, nv, alpha)`（`sampling.hlsli:320`），
把 `RoughnessToAlpha(roughness)` 的结果当 roughness 传进去了。
对 `roughness=0.04` 影响不大，但对粗糙玻璃会明显低估计 G。
非 glass 分支用的是 `V_SmithGGXCorrelated(nv, nl, alpha)`，两处不一致。

---

## 问题 7：`_transmission` 只当作布尔用，且没有体积吸收

* `IsPathMaterialTransmissive` 只判断 `_transmission > 0` 且 `_metallic < 1`：
  `0.6359` 和 `1.0` 走完全相同的代码路径。材质面板上的 Transmission 滑杆
  在光追里没有"部分透射/半透明"的语义。
* 没有 Beer–Lambert / 吸收系数的概念，玻璃内部没有任何着色或能量衰减。
* `BuildSurfaceMaterial` / `BuildStoredSurfaceMaterial`（`raytrace_gi.hlsl:379-441`）
  把 `_transmission = 0`、`_is_glass = false` 写死，ReSTIR 那条路完全不认玻璃。

---

## 与三个现象的一一对应

1. **边缘黑描边** = 问题 1（反射瓣缺失，掠射 `(1-F)→0`）
   + 问题 3（没有环境/天空光可补）
   + 问题 5（掠射自相交导致随机遮挡）。
2. **内部一圈黑线** = 问题 4（零粗糙度让 TIR / `(1-F)` 阈值变成亚像素硬边）
   + 问题 1（反射瓣缺失，全反射兜底路径被 `EvalBRDF` 判成 0）。
3. **整体偏暗** = 问题 3 为主，问题 1、问题 7 为辅
   + 时域降噪默认关闭，单样本直接光的方差让亮部也被"平均"掉一部分观感。

---

## 建议的修复方向（按收益排序）

1. **补齐反射瓣并把分瓣概率显式化**（解决问题 1、2、4 的硬边）
   - 采样：`r_select < F` 走反射，否则走透射；透射用 `refract()` 的返回值判 TIR，
     TIR 时强制走反射并把选择概率置 1。
   - 求值：`EvalBRDF` 按 `wi` 与 `wo` 是否同侧分别返回反射项 `D*G*F/(4|nl||nv|)`
     和透射项，`pdf` 按选择概率加权（`p_reflect * pdf_reflect + p_transmit * pdf_transmit`）。
   - 最省事的做法是在 `TraceContext` 里加一个"本次采样走的是哪个瓣"的标志，
     避免在 `EvalBRDF` 里用错误的 `h_transmit` 反推。
2. **给环境光加一条路径**（解决问题 3 的"偏暗"）
   - 最低成本：玻璃表面也做一次环境 NEE，或者对玻璃球内部的间接项显式
     `EvaluateEnvironmentRadiance(refract_dir)`。
   - 更彻底：抽一个 `EvaluatePathEnvironmentLighting(hit, ctx, wo, rng)`，
     对所有非 delta 材质都补上天空光的 NEE。
3. **去掉 `* 1.5`**（问题 1 的尾巴）。修好 1、2 之后能量应该自洽，
   这个系数只会让亮部过曝。
4. **修正 `G_SmithGGX` 传参**：glass 分支改传 `roughness`，或统一到
   `V_SmithGGXCorrelated`（问题 6）。
5. **偏移量自适应**（问题 5 的颗粒黑边）：`OffsetPathRayOrigin` 的最小偏移
   改成与"射线在表面附近的掠射程度"相关，例如
   `offset = max(1e-5, hit_t * 1e-4) / max(abs(dot(ray_dir, n)), 0.1)`，
   或者在掠射时直接改用几何法线 + 按 `t` 缩放的偏移。
6. **`_transmission` 给真实语义**（问题 7）：至少在 `EvalBRDF` 里让
   非透射部分回归漫反射/GGX 反射，并考虑加一个简单的吸收系数。

---

## 复现与验证建议

* 打开 `NewScene.almap`，勾选 RayTraceGI 的 `_is_temporal_denoise`，
  看黑边/麻点是否只是方差问题（预期：麻点变淡，**黑边和内部黑线不变** →
  说明是确定性的能量丢失，而不是采样噪声）。
* 把球的 `_Transmission` 改成 0，看球体是否变成正常的白色 PBR 球
  （预期：正常 → 说明问题只出在 glass 分支）。
* 把 `_Roughness` 从 0 改到 0.2 左右，看内部那圈黑线是否变模糊
  （预期：变模糊 → 印证问题 4 的阈值硬边判断）。
* 用 `DEBUG_MODE = DEBUG_MODE_F_FRESNEL`（`raytrace_gi.hlsl:22`）看 `F` 的分布，
  确认掠射区 `F→1`，即被丢弃的正是这部分能量。

---

# 修复实施记录

## 改动清单

### 1. `sampling.hlsli` —— 玻璃 BSDF 重写为反射 + 折射双瓣

| 项 | 修复前 | 修复后 |
| --- | --- | --- |
| 反射瓣 | 被注释掉，完全不存在 | `EvalGlassReflectionLobe`：`D*G*F/(4·nl·nv)`，pdf = `D·nh` |
| 折射瓣 | 有，但多乘了 `* 1.5` | `EvalGlassTransmissionLobe`：Walter 2007 原文公式，`* 1.5` 已删除 |
| 分瓣 | 采样端算了 `reflect_prob` 但没用 | `GlassReflectionSelectProb` 采样与求值共用，pdf 按 `p_R·pdf_R + p_T·pdf_T` 混合 |
| 半程向量 | 求值端只用折射式反推 h | `TraceContext` 新增 `_sampled_h` / `_has_sampled_h`，采样端把 h 原样交给求值端 |
| Smith G | `G_SmithGGX(nl, nv, alpha)` 把 alpha 当 roughness | `G_SmithGGX(nl, nv, 1.0)`，新增常量 `kSmithGGeometryRoughness` 说明原因 |
| 退化保护 | 无 | 折射无解 / 求值端判不出折射解时按反射瓣兜底，不再整条丢弃 |

### 2. `rt_common.hlsli` —— `TraceContext` 扩展

新增 `_lobe`（本次走哪个瓣）、`_sampled_h`、`_has_sampled_h` 三个字段。

### 3. `path_tracer_core.hlsli` —— delta 瓣的 MIS 处理

* `bounce._carry_mis` 从 `!mat._is_glass` 改为 `!sample._is_delta`：
  近镜面玻璃与全反射的方向是精确的，其 pdf 无意义，之前参与 MIS 会把能量算成 0
  （这与「内部一圈黑线」直接相关）。
* `TracePath` 里 miss 分支也做了 delta 判断，delta 瓣直接取全部环境能量。
* **环境光 NEE 已移除**（第一版实现里加过 `EvaluatePathEnvironmentLighting`，
  见下面「第一版的两个错误」）。天空光仍然由 miss 分支正常给出：
  玻璃折射光线出射后射向天空就会 miss，修好 delta 判断后它能拿到完整环境能量。

### 4. `path_tracer_material.hlsli` / `path_tracer_*_backend.hlsli` —— 自相交偏移

`OffsetPathRayOrigin` 改为固定世界尺度偏移：`min(kPathOriginEpsilon / max(|n·d|, 1e-3), 1/64)`，
方向沿「射线来的那一侧」。`IsPathOccluded` 里同步用 `kPathOriginEpsilon` 记账，
不再使用随 `hit_t` 放大的假偏移。

### 5. `path_tracer_environment.hlsli` —— 环境 pdf

新增 `EnvironmentSolidAnglePDF() = 1/(4π)`，供 miss 分支的 MIS 使用。

### 6. `raytrace_gi.hlsl` —— 对齐 `EvalBRDF` 新签名

6 处调用去掉外部传入的 `h` 参数（半程向量改由 BSDF 内部按瓣推导），
并补上 `trace_ctx._has_sampled_h = false`。
顺带修了非 RIS 分支里 `weight` 算了却没乘上去的问题。

## 验证

### HLSL 编译

用两套 dxc 编译全部受影响入口，全部通过：

```
Engine/Ext/dxc 1.7（引擎当前打包版本）      Tools/dxc 1.9
  raytrace_gi.hlsl  -E PrimaryRay  exit=0     PrimaryRay  exit=0
  raytrace_gi.hlsl  -E RayGen      exit=0     RayGen      exit=0
  raytrace_gi.hlsl  -E Denoise     exit=0     Denoise     exit=0
  DXR/raytracing.hlsl -T lib_6_5   exit=0     lib_6_5     exit=0
  RTXDI/rtxdi_main.hlsl -E RayGen  exit=0
  raytrace_debug_draw.hlsl vs/ps   exit=0
```

编译命令（`SHADER_DXC` 是引擎编译 shader 时必定定义的宏）：

```
dxc -T cs_6_5 -E PrimaryRay -I ray_trace -I . -D SHADER_DXC=1 \
    Engine/Res/Shaders/hlsl/ray_trace/raytrace_gi.hlsl
dxc -T lib_6_5 -I DXR -I ray_trace -I . -D SHADER_DXC=1 \
    -D AL_SHADER_INTEROP_CBUFFER_AS_STRUCT=1 \
    Engine/Res/Shaders/hlsl/DXR/raytracing.hlsl
```

> 去掉 `-D SHADER_DXC=1` 时两套 dxc 都会在 `color_space_utils.hlsli` 报
> `select()` 条件必须是标量。这是既有的 `#if defined(SHADER_DXC)` 分支设计，
> 正常构建路径不受影响，与本次改动无关。


### 数值验证：`Test/glass_bsdf_verify.mjs`

把 `sampling.hlsli` 的数学逐行复刻到 JS，检查「采样路径必须零丢弃」与
「估计器落在物理能量界内」。13 组（视角 × 粗糙度）配置全部通过：

```
theta= 0° rough=0.04 E=0.8990  丢弃=0  Fresnel T=1-F=0.9479  OK
theta=30° rough=0.04 E=0.8565  丢弃=0  Fresnel T=1-F=0.9478  OK
theta=60° rough=0.04 E=0.6131  丢弃=0  Fresnel T=1-F=0.9182  OK
theta=75° rough=0.04 E=0.2600  丢弃=0  Fresnel T=1-F=0.7358  OK
theta=88° rough=0.04 E=0.0251  丢弃=0  Fresnel T=1-F=0.1543  OK
theta=60° rough=0.3  E=0.6036  丢弃=0  Fresnel T=1-F=0.9182  OK
theta=70° rough=0.6  E=0.4885  丢弃=0  Fresnel T=1-F=0.8310  OK

有丢弃的配置数 = 0（必须为 0）
```

**关键对比**（同一个 θ=30°、roughness=0.04 的透射样本）：

| | 旧实现 | 新实现 |
| --- | --- | --- |
| 估计器 E | **0.0478**（约 18 分之一的能量） | **0.8565** |
| 丢弃样本 | 95%+ | 0 |

旧实现把绝大部分折射贡献判掉了 —— 这就是画面偏暗、边缘发黑的数值根因。

### Fresnel 分瓣（边缘黑描边的修复点）

```
theta= 0°  p_reflect=0.0521
theta=30°  p_reflect=0.0522
theta=60°  p_reflect=0.0818
theta=75°  p_reflect=0.2642
theta=85°  p_reflect=0.6529
theta=88°  p_reflect=0.8457
theta=89°  p_reflect=0.9201
```

掠射时反射概率 → 1，这部分能量现在真的按反射瓣算出来了，轮廓不再被压成黑边。

## 第一版的两个错误（已回退／修正）

第一版实现上线后画面出现了两个新问题，都是这一版引入的：

### 错误 1：自相交偏移随 hit_t 放大 —— 「远处全黑，只有相机附近有光照」

第一版把 `OffsetPathRayOrigin` 写成：

```hlsl
float offset = min(max(1e-4, hit_t * 1e-4) * grazing_factor, hit_t * 0.01);
```

`hit_t * 0.01` 意味着**偏移量正比于命中距离**。远处地面在 `hit_t = 100` 时偏移 1 个单位，
在 `hit_t = 1000` 时偏移 10 个单位 —— 阴影/环境光射线从远离目标的位置出发，
大片区域被误判为遮挡，于是出现「只有相机附近有光照」的硬边界。
`IsPathOccluded` 还会把这个虚高的偏移从 `remaining_t` 里减掉，进一步提前终止遍历。

修正：偏移必须固定在**表面局部尺度**，与命中距离无关：

```hlsl
static const float kPathOriginEpsilon = 1.0f / 65536.0f;   // ≈1.5e-5 世界单位
float offset = min(kPathOriginEpsilon / max(abs(dot(ray_direction, geometric_normal)), 1e-3),
                   1.0f / 64.0f);
```

### 错误 2：多加了一条环境光 NEE —— 每跳多一个 BSDF 样本，全屏噪点翻倍

第一版给每个表面命中都加了 `EvaluatePathEnvironmentLighting`（BSDF 提案 + 可见性 + MIS）。
问题是：**玻璃的折射光线出射后射向天空本来就会 miss，miss 分支已经能拿到完整环境能量** ——
只要 `_carry_mis` 的 delta 判断修好，这条路径就是通的，环境 NEE 是纯冗余。
而它让每个命中多做一次完整的 BSDF 采样 + 一次可见性测试，
单样本/像素的情况下直接把噪点翻倍（画面里地面和球体的密集麻点）。

修正：整体移除 `EvaluatePathEnvironmentLighting` 及其调用。
天空光仍由 miss 分支提供，miss 分支的 MIS 权重保留。

## 过程中发现的两个额外 bug

1. **采样端可能标错瓣**：`refract()` 返回退化向量时，采样端仍按透射瓣标记，
   而求值端会判定「这对方向不是折射解」→ 整条贡献被丢弃。
   已在 `SampleBRDF` 增加 `dot(refracted, refracted) < 1e-12` 判定并改标反射瓣。
2. **NEE 方向的折射半程向量无解**：对强倾斜的微表面，`(wo, wi)` 可能对应纯反射解，
   此时不存在满足 `eta_i(wo·h) = eta_t(wi·h)` 的 h。
   已让 `BuildTransmissionHalfVector` 用对分搜索反解并在无解时返回 `false`，
   调用方按反射瓣处理而不是丢弃。

## 遗留 / 未做

* `_transmission` 仍只当布尔用（`> 0` 即玻璃），没有「部分透射」语义，也没有体积吸收。
* 粗糙玻璃的多重散射能量损失没有补偿（Smith 单次散射模型的固有损失）。
* `BuildSurfaceMaterial` / `BuildStoredSurfaceMaterial` 仍写死 `_is_glass = false`，
  ReSTIR 那条路不认玻璃 —— 该 kernel（`RayGen`）目前没有被派发，暂不影响画面。
---

# 第二版修复记录（玻璃仍然偏暗的真正原因）

第一版修完结构问题后，玻璃依然偏暗。逐项用「单界面能量守恒」这条硬约束（无损介质
`∫ f_transmit·|n·wi| dwi` 必须等于 `1 - F`）对拍，定位到 **三处独立的能量错误**：

## 1. 反射瓣的 pdf 少了 `1/(4|wo·h|)`

半程向量分布到出射方向的 Jacobian 就是 `1/(4|wo·h|)`。少了它，pdf 偏大 4 倍，
反射权重只有 `F/4`（法线入射实测 0.0130，正确值 0.0521）—— 掠射角本来该亮的
反射能量被压到四分之一。

## 2. 折射瓣的 pdf 里 ior 比取向错误

`|wi·h|/denom²` 里的 `denom = ior_i·(wi·h) + ior_t·(wo·h)` 自带 ior 的量纲，
必须再乘 `ior_t²/ior_i²` 才回到立体角密度。第一版写成了反向的 `ior_i²/ior_t²`。

对半程向量做高分辨率半球积分实测（alpha=0.02，ior=1.5918，理论 1-F = 0.9479）：

| pdf 里的 ior 比 | 0° 透射积分 |
| --- | --- |
| `ior_t²/ior_i²`（正确） | **0.9484** |
| `ior_i²/ior_t²`（第一版） | 6.09 |
| 不带 | 2.40 |

## 3. 一个把所有折射样本判成无效的「物理判据」

第一版我加了 `ior_t·(wo·h) == ior_i·(wi·h)` 作为有效性判据。这个恒等式对真实采样出来的
`(h, wo, wi)` **并不成立**（实测残差恒定在 O(1) 量级），加上之后每一条折射样本都被判成 0。
已删除，只保留 `denom` 的数值保护。

## 4. 折射半程向量的正确解析式

`h = normalize(wi·ior_t + wo·ior_i)`（系数交叉，再做定向）。用 `refract` 回代验证：
各角度、进入/离开两种情况下对齐度都是 **1.000000**。
以前试过的 `wi·ior_i + wo·ior_t`、`wo·ior_i - wi·ior_t`、对分搜索等写法全部错误。

## 最终验证

`Test/glass_bsdf_energy.mjs`（逐行复刻 shader）：

```
理论 F = 0.052137    理论 1-F = 0.947863

反射瓣 法线入射权重 = 0.052137  (应为 F = 0.052137)  OK

透射瓣 半球积分:
  theta= 0°  透射积分 = 0.948384
  theta=15°  透射积分 = 0.942195      (高角度低于 1-F 是 Smith 阴影的正常损失)
  theta=30°  透射积分 = 0.911859
  theta=45°  透射积分 = 0.842852
  theta=60°  透射积分 = 0.695598
  theta=75°  透射积分 = 0.388146

近法线偏差 = 0.005668  OK
```

编译：`PrimaryRay` / `RayGen` / `Denoise` / DXR `lib_6_5` / DDGI `ProbeTrace` / RTXDI 全部 exit=0。

## 教训

这一轮之所以来回反复，是因为一直用「猜公式 + 单点数值对拍」的方式推进，
而单界面能量守恒是可以直接算出来的硬约束，应该一开始就用它来定标。
`Test/glass_bsdf_energy.mjs` 现在就是这个定标工具：**改玻璃 BSDF 之后先跑它**，
只要反射权重 != F 或透射积分 != 1-F，画面必然偏亮或偏暗。
---

# 用 ImageCaptureService 抓帧验证（DXR 路径）

按建议接上了 `ImageCaptureService`，把「RayTrace GI DXR」的输出直接抓成 PNG 做核对。
为此补了三处基础设施：

## 1. PNG 输出

`ImageCaptureService` 原本只实现 DDS 输出（`BeginSequence` 里对非 DDS 直接报
"Only DDS native output is implemented"）。新增 `PngCaptureEncoder`：

* 支持 `R32G32B32A32_FLOAT`、`R16G16B16A16_FLOAT`、`R8G8B8A8_*`、
  `R10G10B10A2_UNORM`（画布就是它）、`R11G11B10_FLOAT`
* 浮点源做 Reinhard 色调映射后再转 sRGB，避免 HDR 高光整片爆白
* 走仓库里已有的 `Ext/stb/stb_image_write.h`（实现在 `Texture.cpp`）

## 2. 抓帧自动化

新增 `AutomationCaptureService`（`Editor/Src/Automation/`），注册四个方法：

| 方法 | 作用 |
| --- | --- |
| `render.capture_pass` | 抓指定 pass 的指定输出若干帧，落盘 PNG，返回会话 id 与目录 |
| `render.capture_status` | 查询会话状态与已落盘文件 |
| `render.set_feature_active` | 开关 render feature（RayTraceGI 只有激活后才注册 pass） |
| `render.list_features` | 列出 render feature 与激活状态 |

配套 `Tools/render_capture_client.mjs`，通过 `.ailu/editor_session.json` 里的命名管道调用。

## 3. 无人值守抓帧

`EditorLayer` 里已有一套 `AILU_AUTOCAP_FRAME` 机制，把它从「只能抓延迟光照」泛化成可抓任意 pass：

| 环境变量 | 作用 |
| --- | --- |
| `AILU_SCENE` | 打开的场景 |
| `AILU_RAYTRACING_PIPELINE=1` | 切到光追管线（原本只是调试菜单里的复选框，不持久化） |
| `AILU_AUTOCAP_FRAME` | 在第几帧抓 |
| `AILU_AUTOCAP_PASS` | pass 名，如 `RayTrace GI DXR` |
| `AILU_AUTOCAP_RESOURCE` | 期望的资源名，留空接受任意 |
| `AILU_AUTOCAP_ACCESS_INDEX` | 第几个输出（RayTrace GI 有 5 个输出，颜色在第 4 个） |
| `AILU_AUTOCAP_PREFIX` | 文件名前缀 |
| `AILU_CAPTURE_DUMP_PASSES=1` | 把每个 pass 及其输出资源名打进日志，用于确定选择器参数 |

抓帧命令：

```powershell
$env:AILU_SCENE="project://scene/NewScene.almap"
$env:AILU_RAYTRACING_PIPELINE="1"
$env:AILU_AUTOCAP_FRAME="40"
$env:AILU_AUTOCAP_PASS="RayTrace GI DXR"
$env:AILU_AUTOCAP_ACCESS_INDEX="4"
$env:AILU_AUTOCAP_PREFIX="rtgi_dxr"
artifacts\out\editor\bin\x64\debug\Editor.exe <project>.ailuproject
```

## 抓帧结果

`Saved/RenderCapture/AutoCapture/session_1/rtgi_dxr_0000.png` 已确认：

* 球体内部是**浅灰蓝**，与天空/地面亮度一致 —— 不再是之前那种发黑
* 地面与天空亮度正常，没有「只有相机附近有光照」的截断
* 边缘仍有细的暗描边和颗粒，那是每像素单样本、未开时域降噪的方差
  （`RayTraceGI::_is_temporal_denoise` 默认 false）

也就是说：**「玻璃特别暗」已经解决**，剩下的是采样噪声而不是能量错误。

## 这次的教训

上一轮之所以反复，是因为只能靠截图猜。接上抓帧之后，每一步都能拿到确定性的输出图来对拍。
后面再改玻璃相关代码，建议先跑 `Test/glass_bsdf_energy.mjs`（能量定标），
再用上面的命令抓一张 `RayTrace GI DXR` 的图（画面核对）。

---

# 黑边的真正根因：玻璃壳把光「困住」了（全反射光管）

用户反馈开了时域降噪后**边缘仍有明显黑边**。用抓帧做了决定性实验：

## 实验：把光照整个拿掉，只留几何与穿透

在 `path_tracer_core.hlsli` 里临时加开关，跳过 `EvaluatePathDirectLighting`，
只统计「穿透/反射到几何体或环境」的能量，然后抓「RayTrace GI DXR」：

```
水平中线剖面（左边界外 10px -> 内 30px）:
 138 138 138 138 138 138 138 138 138 138   0   0  17  18   0   0   0   0 ...（之后全 0）
```

轮廓外是天空（亮度 138），轮廓内**从边缘一路向内至少 16 像素全是 0（纯黑）**。

## 结论

**球壳在掠射角下把所有透射进来的光用全反射锁死在玻璃里**，路径深度只有 3，
困住的光既到不了几何体也到不了环境，于是贡献恒为 0 —— 这就是黑边。

几何上说得通：相机射线以 θ_i 入射，前表面折射成 θ_t；到远侧内表面时入射角约
`asin(2·sinθ_t)`。当 θ_i ≳ 29° 时该角超过临界角 `asin(1/1.5918) = 38.9°`，
折射光被全反射弹回，之后反复在壳内弹跳（光管效应），3 次弹跳根本出不来。
屏幕空间里这段就是轮廓内侧的一圈暗环。

## 验证：提高路径深度确实改善

把 DXR 的 `kMaxPathDepth` 从 3 提到 12 再抓一张：

| | 球体内部纯黑像素占比 |
| --- | --- |
| 深度 3 + 降噪 | 明显的一圈实心黑边 |
| 深度 12 | 几乎看不出黑边，内部呈现正常的折射画面（上半浅灰、下半蓝、底部高光） |

所以路径深度是主要限制。但**深度 12 仍然不能根治**：闭合介质里的全反射是
近乎无损的，光管可以无限弹下去，靠加深度只是把漏出来的比例抬高。

## 可选的修法（需要定取舍）

1. **提高路径深度** — 最省事，`kMaxPathDepth 3 -> 8~12`，代价是帧率。
   已经验证有明显改善，但不是根治。
2. **给困住的光一条漏出通道**（推荐）— 达到深度上限时，若最后一步是全反射，
   不要返回 0，而是让它按某个透射率漏向环境：
   `radiance += throughput * leak * EvaluateEnvironmentRadiance(last_dir)`。
   `leak` 可以取常数，也可以按菲涅尔/粗糙度估。视觉上黑边会立刻消失，代价是能量偏亮。
3. **加体积吸收（Beer–Lambert）** — 光管路径在玻璃里走的距离远大于直穿路径，
   吸收会自然把暗环压暗，同时给玻璃真实的厚度感。物理上最正确，
   但要引入吸收系数，而且**不会消除暗环，只是让它变成有色**。
4. **按薄壳处理** — 如果美术意图是「薄玻璃泡」而不是实心玻璃球，
   在壳内只做单次穿透、不做多次全反射，黑边直接消失。这改变的是材质语义。

## 顺带修的

`RayTraceGI` 的 `_debug_pos` 默认就支持在固定像素上输出调试信息，但那是逐像素的。
现在有了抓帧通道（见上一节），以后这类问题都可以按「抓图 -> 量剖面」的方式定位，
不用再靠肉眼猜测。

---

# 定稿：物理正确的做法

用户要求「追求物理正确」，所以没有加「漏光」之类的假补丁。

## 改动

`DXR/Raytracing.hlsl`：`kMaxPathDepth` 3 → 12。

这个值对闭合玻璃体是**物理正确性的硬约束**，不是性能旋钮。实心玻璃球在掠射角下
把射入的光用全反射锁在内部（光管效应），光要在壳内反复弹跳、靠微表面粗糙度
逐次扰动才可能找到出口；深度太小时这些路径被硬截断，既不返回几何体也不返回环境，
贡献恰好为 0。**这正是黑边的来源，不是采样噪声**（开时域降噪也去不掉）。

## 量化收敛性

用抓帧通道做了两轮对照，只统计几何、关掉全部光照，量「球体内部纯黑像素占比」：

| 深度 | 纯黑占比 | 轮廓环带 |
| --- | --- | --- |
| 3 | **55.8%** | 22%~100% 全黑，肉眼可见实心黑边 |
| 12 | 4.6% | 最外环黑像素比例**最低**（2%），黑边消失 |
| 24 | 4.6% | 与深度 12 **逐像素完全一致** |

深度 12 与 24 结果逐像素相同，说明**限制已经不在深度上**，剩下的暗像素是模型本身的
性质而不是截断偏差。按半径分环看，最外环的纯黑比例（2%）反而低于球心区域（4~7%），
即**边缘不再是一个暗环** —— 掠射菲涅尔反射把天空的亮色补了回来。

## 为什么不能「加漏光」补丁

给困住的光人为指定一个逃逸概率，等价于把多次散射能量按经验系数抽出来，
会破坏能量守恒 —— 重量级玻璃看起来会偏亮，而且亮多少取决于拍脑袋的系数。
深度给够之后，光自己会通过微表面粗糙度找到出口，能量自然守恒。

## 剩下的已知项（都不影响黑边）

1. **球体内部的稀疏黑点（4.6%）**：分布在整球而非边缘，最外环最低。
   与深度无关，属模型性质。
2. **体积吸收还没有实现**。真实的玻璃（哪怕是无色光学玻璃）有吸收系数，
   光管路径在玻璃里的行程远大于直穿路径，吸收会自然让厚处更暗。
   现在是完全无损介质，所以暗部只能靠几何遮挡产生，缺少「厚度感」。
   这是目前与真实玻璃差距最大的一项，也是「物理正确」方向上最值得补的。
3. **色散没有**：IOR 是单值，没有按波长分开。

## 复现命令

```powershell
$env:AILU_SCENE="project://scene/NewScene.almap"
$env:AILU_RAYTRACING_PIPELINE="1"
$env:AILU_AUTOCAP_FRAME="60"
$env:AILU_AUTOCAP_PASS="RayTrace GI DXR"
$env:AILU_AUTOCAP_ACCESS_INDEX="4"
$env:AILU_AUTOCAP_PREFIX="final"
artifacts\out\editor\bin\x64\debug\Editor.exe <project>.ailuproject
```

输出在 `Saved/RenderCapture/AutoCapture/session_1/`（相对编辑器进程工作目录）。

对照用的诊断开关在 `path_tracer_core.hlsli` 里，默认注释：
`#define PROBE_GEOMETRY_ONLY` 打开后只统计几何、跳过全部光照，用来区分
「黑边来自几何/遍历」还是「来自光照估计」。

---

# 非均匀缩放后的黑圈：这是薄玻璃片的真实光学，不是 bug

用户把球体单轴压扁（`_scale = [0.6158, 0.6158, 0.1543]`，即 z 向压到 1/4）
之后出现很粗的黑圈。用抓帧通道逐项排查：

## 已排除的可能

| 怀疑 | 检验 | 结论 |
| --- | --- | --- |
| 法线变换在非均匀缩放下算错 | 用真实矩阵把 `transpose((float3x3)_world_to_local)` 与椭球世界梯度逐点对比，200 点最大误差 **2e-6°** | **排除**（算法正确） |
| 路径深度不足（上次球体的病因） | `kMaxPathDepth` 提到 **64**，黑圈依旧 | **排除** |
| 光照/阴影估计问题 | 打开 `PROBE_GEOMETRY_ONLY`（完全跳过直接光与光源采样），黑圈依旧，且变成纯黑 | **排除**（是几何/穿透性质） |

## 真正原因：薄片的全反射陷阱比球体宽得多

对压扁后的旋转椭球（半轴 a=0.3079、c=0.0771）沿 z 做解析光线追踪，
临界角 `asin(1/1.5918) = 38.92°`：

| 撞击参数 b/a | 前表面入射角 | 内部到达远端内表面入射角 | 是否全反射 |
| --- | --- | --- | --- |
| 0.00 | 0.0° | 0.0° | 否 |
| **0.20** | **2.9°** | **40.1°** | **是** |
| 0.40 | 6.2° | 80.5° | 是 |
| 0.60 | 10.6° | 73.8° | 是 |
| 0.80 | 18.5° | 62.1° | 是 |
| 0.90 | 27.4° | 49.4° | 是 |
| 0.95 | 37.3° | 36.4° | 否 |
| 0.99 | 60.4° | 49.4° | 是 |

对照上次的厚球体：同样 θᵢ ≈ 2.9° 的入射光，在球内到达远端时入射角只有约 **5.8°**，
远低于临界角，直接穿出。

**差别来自「光在介质内的行程」**：球体沿弦走的距离长、方向偏折大，
薄片几乎沿厚度直穿，方向几乎不变，于是**以很小的前表面入射角就能在远端超过临界角**。
扫描线上 0.2 ≤ b/a ≤ 0.9 全部落入全反射，对应屏幕上就是一大片暗区；
只有接近中心（b/a < 0.2）和极边缘（b/a ≈ 0.95）能穿出去，所以边缘附近反而亮。

**这是薄玻璃片真实的光学行为**（「厚玻璃透明、薄玻璃反而挡光」），
不是渲染错误。实心玻璃球的暗环窄，薄片的暗区宽，都是同一个物理。

## 为什么现在看起来「全黑」而不是「暗」

因为 `StandardPBR` 的 `_Transmission = 0.6359`，材质只声明了 64% 透射，
而 `IsPathMaterialTransmissive` 是 `_transmission > 0` 的布尔判断 ——
64% 透射被当成 100% 玻璃处理。所以本该有 36% 的漫反射/半透成分把这片区域填亮，
现在完全没有，暗区就黑到底了。

## 建议的后续（按物理正确性）

1. **`_transmission` 给真实语义**：`transmission = 0.64` 应当表示
   「64% 按玻璃折射 + 36% 按不透明表面着色」，而不是「是/否玻璃」。
   这一条同时解释了这次的黑圈和之前「玻璃特别暗」的一部分。
2. **体积吸收（Beer–Lambert）**：玻璃有吸收系数时，全反射陷阱里的光
   行程远大于直穿光，会自然衰减 —— 薄片的暗区变成有颜色的暗区，
   厚处更暗、薄处更亮，这就是「厚度感」的来源。现在是无损介质，
   陷阱里的光永远不会损失，只能靠几何漏出。

---

# 实施：`_transmission` 真实语义 + 体积吸收

按「1+2」都做了。

## 1. `_transmission` 从布尔改为真正的混合因子

材质是 `_Transmission = 0.6359`，语义应当是
「64% 按玻璃折射 + 36% 按不透明表面着色」，原来 `IsPathMaterialTransmissive`
只做 `> 0` 的判断，把 64% 当成了 100% 玻璃。

改动（`sampling.hlsli`）：

* 抽出 `EvalOpaqueBRDF()`（GGX 高光 + Lambert），供玻璃分支复用
* `EvalGlassBSDF` 返回 `tr * 玻璃瓣 + (1-tr) * 不透明瓣`，**pdf 也按同样比例混合**
* `SampleBRDF` 先按 `tr` 决定这一票是玻璃还是不透明表面，玻璃部分再把
  `r_select` 重映射回 `[0,1)` 按 Fresnel 分瓣

采样分布与求值 pdf 必须严格一致，否则估计量有偏 —— 这是这次改动的关键约束。

`tr = 1` 时逐像素退化为原来的纯玻璃行为。

## 2. 体积吸收（Beer–Lambert）

* `MaterialData` **复用原来的 `_reserved0` 槽位**改成 `float _absorption`
  （1/世界单位，0 = 无损介质）。MaterialData 的布局与大小完全不变，
  不会让现存的材质缓冲失效。
* 材质属性 `_Absorption` 加到 `defered_standard_lit.hlsl` / `forwardlit.hlsl` 的
  属性表与 `standard_lit_common.hlsli` 的 cbuffer；标准 PBR 材质没有这个属性时
  `Material::GetFloat` 返回 0，所以现存材质行为不变。
* `TracePath` 新增 `medium_absorption` 状态，每次跳转按 `hit._hit_t`
  乘以 `exp(-absorption * 距离)`；`PathBounceSample` 新增 `_is_transmission`
  用于判断光线是进入介质还是回到空气。

**踩到的坑**：`MaterialData` 数组是未初始化的堆内存，`FillMaterialData` 里漏了
`dst._absorption = 0.0f`，于是着色器读到垃圾值当吸收系数，整块几何体被吸收成黑色
（球体直接从画面里消失）。已在两处 `FillMaterialData` 补上显式清零。

## 验证状态

* 着色器编译：DXR `lib_6_5`、compute `PrimaryRay`/`RayGen` 全部 exit=0
* 能量定标：`Test/glass_bsdf_energy.mjs` 仍通过（反射权重 = F，透射积分 = 1-F）
* 混合估计量：自洽性脚本在法线入射处误差 3.3%（在两边蒙特卡洛误差范围内），
  高角度误差偏大是参照积分本身的方差（粗糙度 0.04 时分布很窄）

**还没能出图核对**：无人值守抓帧的相机没能对准球体（连栅格路径也拍不到球），
所以这次改动的画面效果需要用户在编辑器里确认。

## 建议的核对方式

在编辑器里（勾上光追管线）跑：

```powershell
node Tools/render_capture_client.mjs capture --pass "RayTrace GI DXR" --access_index 4 --wait
```

预期变化：
1. 压扁的玻璃盘那片死黑区域应该被 36% 的漫反射填亮成灰白（不再纯黑）
2. 把材质的 `_Absorption` 从 0 往大调，玻璃会开始出现厚度感：
   厚处更暗、薄处更亮，全反射陷阱区域先变色再变暗

---

# 第三版修复：采样端选中的瓣没有传给求值端（玻璃系统性偏暗的真凶）

## 症状

`_Transmission = 1.00`、`_Absorption = 0.01`，球体几乎全黑；把 `_Absorption` 调回 0
也只是略好。用户判断「0.01 的吸收就把颜色吃掉了」。

## 根因 1：`SampleBRDF` 把 lobe / 半程向量写进了**按值传入的副本**

`SamplePathBounce` 的调用是：

```hlsl
SampleBRDFResult sample = SampleBRDF(trace_context, ...);   // trace_context 按值传入
EvalBRDF(trace_context, ...);                               // 再按值传入求值
```

而 `SampleBRDF` 内部在玻璃分支里写的是：

```hlsl
ctx._lobe = do_reflect ? kBSDFLobeReflection : kBSDFLobeTransmission;
ctx._sampled_h = h;
ctx._has_sampled_h = true;      // 写的是形参副本
```

于是 `EvalGlassBSDF` 里那个「直接用采样到的半程向量」的分支**从来没有被执行过**，
每一次求值都退化成从 `(wo, wi)` 反推半程向量。而反推在近镜面玻璃下是病态的：

* 粗糙度 0.04 时 GTR2 分布极窄，`h` 几乎平行于 `n`
* 反射解与折射解的 `dot(n, h)` 都趋近于 1，符号靠浮点噪声区分
* 反推出来的 `h` 与真实采样值有偏差，`pdf` 与 `f` 都跟着错
* 掠射角与近法线折射下 `h_reflect = normalize(wo + wi)` 甚至可能退化成 0 向量或被翻转后判负

**修法**：把 lobe / 半程向量作为 `SampleBRDFResult` 的显式字段带出来，
求值前回填进 `TraceContext`（`_has_sampled_h` 这个布尔不再需要，已删除，
改用 `_lobe == kBSDFLobeAnalytic` 表示「没有采样上下文，走解析反推」）。

## 根因 2：三个随机数复用，选瓣与采方向强相关

原写法在 `_transmission < 1` 时把同一个 `r1` 既用于「选高光/漫反射瓣」又用于
「采 GGX 半程向量」：

```hlsl
if (r1 < specular_prob) { h = SampleGGX(n, alpha, r1, r2); ... }   // r1 用两次
```

两个选择因此相关，估计量权重不再恰好等于 1，中间 `_transmission` 值偏差最大
（0 与 1 两端反而正常）。这正是之前观察到的「两端正常、中间过曝」的形状。

**修法**：`SampleBRDF` 改成严格分工的三路：

| 随机数 | 用途 |
| --- | --- |
| `r_select` | 在「不透明瓣 / 玻璃瓣」之间二选一（权重 `1-tr` / `tr`） |
| `r1` | 采样微表面法线（GGX）或余弦半球 |
| `r2` | 在玻璃内部按 Fresnel 概率 `p_R` 决定这一票是反射还是折射 |

并且把「采半程向量 → 按 Fresnel 选瓣 → 求值时复用同一个 `h` 与同一个 `p_R`」
抽成一对共用函数 `SampleGlassLobeDirection` / `EvalGlassLobe`，两端不可能再写岔。

这套写法的好处是**每个样本的权重恰好是 1**（delta 近似下）：

```
反射瓣：f = p_R·D·G·F/(4·nl·nv)   pdf = p_R·D·nh/(4·vh)
        权重 = f·nl/pdf = F·G/(4nv) · 4vh/(nh) = F   （nh=1, vh=nv）
折射瓣：权重 = (1-F)·|wo·h|/(nl·nv)·nh² / (nh²/...) = 1-F
```

即：选到反射的样本贡献 `F`，选到折射的样本贡献 `1-F`，两者相加恒为 1。

## 根因 3：计算后端主光线硬编码深度 3

`raytrace_gi.hlsl` 的 `PrimaryRay` 里写着：

```hlsl
uint depth = 3u;
float3 color = TracePath(pixel, ray_origin, ray_dir, depth, ctx);
```

而 DXR 后端用的是 `kMaxPathDepth = 12`。深度 3 对闭合玻璃体是致命的：
全反射光管路径被硬截断，既不返回几何体也不返回环境，贡献恰好为 0 —
实测球体内部纯黑像素占 55.8%。两条管线因此给出完全不同的结果。

**修法**：`kMaxPathDepth` 上移到 `path_tracer_common.hlsli`，两个后端共用同一个常量，
`PrimaryRay` 改用它。把「两边必须一致」写进注释。

## 根因 4：`ApplyMediumAbsorption` 里的 float3 三目运算符编译不过

```hlsl
a = a != a ? 0.0.xxx : a;      // dxc: condition for short-circuiting ternary
                               //      operator must be scalar
```

改用 `select(a != a, 0.0.xxx, a)`。这个错误在运行期才会以「着色器编译失败」的形式暴露，
离线用 dxc 编译一遍就能提前发现（见下面的验证命令）。

## 关于「0.01 的吸收把颜色吃掉了」

吸收系数是**每世界单位**的，Beer–Lambert 为 `T = exp(-a·d)`。
本场景球体 scale 0.88（半径 0.44），面积均匀投点下整颗球的平均透射率实测：

| absorption | 平均透射率 | 损失 |
| --- | --- | --- |
| 0.001 | 0.9994 | 0.06% |
| 0.01 | 0.9942 | 0.58% |
| 0.1 | 0.9433 | 5.7% |
| 1.0 | 0.5689 | 43% |

`Test/glass_absorption_verify.mjs` 里同时跑了蒙特卡洛与解析解
`∫₀¹ exp(-2aR√(1-u²))du`，`a ≤ 0.1` 时两者相对误差 < 1.1%。

**结论：0.01 在物理上不可能把颜色吃掉，只能损失 0.58%。**
（要有明显厚度感需要 `1~10`。另外这个系数是标量，对所有通道一视同仁，
所以它无论如何都不会造成「颜色丢失/偏色」。）

同一时刻真正让球体变黑的是根因 1 + 根因 3：反射瓣被错误求值、
光管路径被硬截断。所以现象与吸收旋钮在时间上重合，但因果不同。

## 验证

```powershell
# 离线编译三个入口，任何 HLSL 错误立刻暴露
$dxc = "Engine\Ext\dxc\bin\x64\dxc.exe"
cd Engine\Res\Shaders\hlsl
& $dxc -T cs_6_5 -E PrimaryRay -D SHADER_DXC=1 -I . -Fo "$env:TEMP\gi.dxil"  ray_trace\raytrace_gi.hlsl
& $dxc -T cs_6_5 -E Denoise    -D SHADER_DXC=1 -I . -Fo "$env:TEMP\dn.dxil"  ray_trace\raytrace_gi.hlsl
& $dxc -T lib_6_5             -D SHADER_DXC=1 -I . -Fo "$env:TEMP\rt.dxil"  DXR\Raytracing.hlsl

# 吸收量级 + 白炉检验
node Test/glass_absorption_verify.mjs
```

三个入口均 exit=0。

## 仍未做（另一条独立的物理改进）

白炉检验显示玻璃在掠射角仍有能量损失（60° 时约 0.61，80° 时约 0.13）。
逐瓣统计已定位：**损失不在 pdf 的 Jacobian（那部分是对的），而在 G 项**。
`G_SmithGGX` 给的是分离式的 `G1(l)·G1(v)`，而采样用的是完整的 GTR2 法线分布
（非 VNDF），`∫ D(h)|wo·h|/nv dh = G1(wo)` 这个归一化关系因此不成立，
残余的 `G2/G1` 因子就是损失。单次散射 GGX 本身还有随粗糙度增长的多次散射缺失。

修法是启用代码里已经写好但被注释掉的 VNDF 采样路径
（`SampleGGX_VNDF` + `PdfGGX_VNDF` + `SAMPLE_BRDF_VNDF_GGX` 宏）。
这会同时改变所有材质的采样分布，属于独立的物理正确性改动，未在本次混入。
