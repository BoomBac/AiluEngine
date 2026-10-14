# M2 — Weather Map / Coverage / 云型高度曲线

> 前置：M1 PASS。**仅解决哪里有云、云应处于什么高度**；detail/noise generator/光照不改。

## Purpose / Scope / Non-goals

- 目的：将低频天气组织、局部云型和高度梯度从纯噪声中解耦，修复 `cloud_coverage=1` 与两次 `lerp` 覆盖的错误。
- 范围：WeatherMap R/G/B 语义、world-space UV、云型曲线、覆盖率映射、密度上下界、参数化与 Debug Slice。
- 不做：更换 Noise 纹理、改变 HG phase、重新调整 CloudLight、雨滴/降水渲染。

## Architecture Constraints / Baseline / Target Outcome

- 原代码通过 `SAMPLE_TEXTURE2D_LOD(_WeatherMap, pos.xz * CLOUD_BASE_FREQ)` 获取 RGB；R 注释为覆盖率，G 为降雨概率，B 为云类型 (stratus→cumulus→cumulonimbus)，但 R 未应用。
- `_CloudZone` 中云底、厚度与 M1 的径向高度保持一致；云类型代表**垂直轮廓**而不是 CloudDensity 全局开关。
- 目标：`coverage=0 → density=0`（在任何时间/云型成立）；`coverage=1` 不人为丢失有效低频云团；三种轮廓端点/中值有效。

## Relevant Files

`Compute/volumetric_cloud.hlsl`；`VolumetricClouds.h/.cpp`（新参数、材质序列化/反射）；`Engine/Res/Textures/weather_map.alasset`（只读或诊断）；M0 Debug 和 CPU oracle。

## Implementation / Algorithm

### A. Weather Map 的稳定空间

定义 `weather_uv = (cloud_pos.xz - world_anchor.xz) * _weather_frequency + _weather_scroll`，与 `_shape_frequency` 完全独立，避免大尺度天气变化受低频体积噪声平铺大小影响。

- 稳定的 world_anchor 只负责浮点精度/美术定位，不得使用“随相机每 1000 单位 snap 并让 UV 跳变”的坐标中心。
- Weather map 资产纹理真实 R/G/B 范围先抓 histogram + channel visualization；不可基于注释推断该 PNG 的真实通道分布。
- `weather_data.r` 为局部 coverage，`weather_data.b` 为 cloud type；`weather_data.g` 可留作未来降雨强度影响，但不得与 coverage 无文档地混用。
- 为测试覆盖率，应允许 `weather_override` 使常量 R=0/0.5/1、B=0/0.5/1，且不改原 PNG。

### B. 三种垂直高度轮廓正确插值

现有 `HeightDensityCurve()` 可暂保留并数值验证三条曲线；旧代码：

```hlsl
float final_density = lerp(stratus, cumulus, t1);
final_density = lerp(cumulus, cumulonimbus, t2); // 覆盖前一项，必须删除
```

改为明确分段：

```hlsl
float GetCloudTypeProfile(float height_fraction, float cloud_type)
{
    if (height_fraction <= 0.0f || height_fraction >= 1.0f)
        return 0.0f;
    float stratus = EvaluateStratusProfile(height_fraction);
    float cumulus = EvaluateCumulusProfile(height_fraction);
    float cumulonimbus = EvaluateCumulonimbusProfile(height_fraction);
    float type = saturate(cloud_type + _type_offset);
    return type < 0.5f ? lerp(stratus, cumulus, smoothstep(0.0f, 0.5f, type))
                       : lerp(cumulus, cumulonimbus, smoothstep(0.5f, 1.0f, type));
}
```

保证 `type=0,0.5,1` 恰好回到三条基函数；关注旧 `HeightDensityCurve` 参数在高度端点是否非零，若原 curve 末端非零必须最终强制 0。

### C. Coverage 和 Base Shape 互相独立

定义 base shape `s ∈ [0,1]`，coverage `c∈[0,1]`。先用单调的 `CoverageRemap`（留可调 curve，仅增加一个参数，避免不透明经验公式）：

```hlsl
float ApplyCoverage(float shape, float coverage)
{
    if (coverage <= 0.0f) return 0.0f;
    return saturate((shape - (1.0f - coverage)) / max(coverage, 1.0e-4f));
}
```

这是**可解释的可调阈值模型**，不是“业界唯一正确公式”。`coverage→0` 接近无云，`coverage→1` 回到 shape；对固定 shape，结果应随 coverage 单调不减。对高噪声阈值，可能发生大面积空云，需在 Debug 中调节 `shape` 分布、coverage 曲线，不得在末端乘超大亮度掩盖。

### D. 最终 density 统一范围

```hlsl
float height_fraction = GetCloudHeightFraction(sample_pos);
if (height_fraction <= 0.0f || height_fraction >= 1.0f) return 0.0f;
float density = ApplyCoverage(base_shape, weather_coverage) * GetCloudTypeProfile(height_fraction, weather_type);
return saturate(max(0.0f, density * _density_multiplier));
```

`_density_multiplier` 表示无量纲密度倍率；消光系数与 m 单位光学标定在 M4；不要再允许 `Remap(_Threshold, 1, 0, _DensityMultply)` 返回负值。保留现有面板兼容迁移说明（`_density_multiplier` 已在 C++ 被平方，需要明确迁移和值变化）。

## Validation

| 输入变化 | 理论预期 | 抓帧与指标 |
|---|---|---|
| Coverage R=0 | 全局 density 恒 0 | 采样所有高度/位置，`max_density==0`（允许 debug float tolerance `1e-6`） |
| Coverage R=1 | 不施加额外 coverage 阈值 | 与基础 `base_shape*profile` 差 ≤1e-5 |
| Coverage R=0.2,0.5,0.8 | 固定 shape 上密度非递减 | 10k 随机独立输入，violations=0 |
| CloudType B=0,0.5,1 | 完全对应三个高度曲线 | 256 层高度切片对照 reference，最大误差 ≤1e-5 |
| CloudType 0.49/0.5/0.51 | 连续 | 中点两侧无突变；差按曲线斜率解释 |
| Height outside `[0,1]` | 无云 | 地球径向高度 ±epsilon、远高于顶部，density=0 |
| Shape 输入越界、NaN 诊断 | 正式生产输入范围明确 | 不允许 NaN 漂入 RGB；Shader debug 计数为 0 |
| World origin / far XZ | 天气 UV 空间连续 | 平移相机而世界绝对位置不动，天气图与云锚点不跳动 |

> 视觉要求：固定太阳、全分辨率、仅显示灰度 density，能看到成群/断开的云区；云型 0/0.5/1 各自具有可辨的高度分布。仅“感觉好看”不替代上述数值门槛。

## Acceptance Criteria

- [ ] Weather Map 通道已核对，R 已真实决定 Coverage，B 已正确控制三曲线。
- [ ] Coverage=0/1 与中间值单调性测试 PASS；Height 区域外无密度。
- [ ] Type 切片与 CPU reference 一致，结果不会被第二次 lerp 覆盖。
- [ ] Camera 大范围移动、Weather UV 平铺边界无突然跳变或精度失稳。
- [ ] 原关卡参数映射有迁移记录；编译/Debug Layer/多相机烟测 PASS。
- [ ] `progress.md` 包含曲线图、hogram、截图和独立结论，方可进入 M3。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**；记录选用 coverage curve 公式与不可兼容参数的理由。后续 M3 才处理 detail erosion；本阶段的成果必须是在**纯 Base Density** 就能看到宏观云群轮廓。恢复顺序 README → progress → 本文件 → 源码；M2 未通过禁止进入 M3。
