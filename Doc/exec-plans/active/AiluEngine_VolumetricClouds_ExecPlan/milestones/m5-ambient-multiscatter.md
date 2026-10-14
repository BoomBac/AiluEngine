# M5 — 天空环境光、云层暗部与有界多次散射近似

> 前置：M4 PASS。M4 单次散射必须始终能独立切回，作为本阶段所有实验的硬基线。

## Purpose / Scope / Non-goals

- 目的：修复 `ambient=0` 导致云底和背光云体不自然黑暗的问题，并引入可独立关闭、物理上有界的多次散射近似。
- 范围：天空环境光注入、空间/高度方向差异、间接源项、多次散射近似、昼夜和云层内外光照校验。
- 非目标：全局 3D 多次散射求解器、全局照明探针复用、大气 LUT 重写、云地面投影阴影。

## Architecture Constraints / Baseline / Target Outcome

- 现有 `float ambient=0.0.xxx`，`ambient_intensity=0.25` 无效；旧 `MultipleOctaves()` 通过调 extinction/phase 粗略补亮，与实际步长不一致。
- 必须将 `L_direct`、`L_sky_ambient`、`L_multi_approx` 分开计算、调试与输出，任何一个分项都不得重复乘 `sigma_s`。
- 多次散射模块可近似，但要求：**非负、有限、对零入射光输出零、随光学厚度变化有合理范围、能单独关闭**；不能声称其数学严格等于真实高阶 RTE。

## Relevant Files

`Compute/volumetric_cloud.hlsl`，`atmosphere_common.hlsli`，`Environment.cpp`、`sky_atmosphere.hlsl`（只读确认资源），`VolumetricClouds.cpp`（绑定 sky LUT），M0 Debug。

## Implementation / Algorithm

### A. 分清入射天空辐亮度与场景天空背景

真正的 ambient in-scattering 在介质点 `x` 的视线方向 `omega_o` 是方向积分：

`Li_ambient(x, omega_o) = ∫_sphere L_sky_in(x, omega_i) * p(omega_i, omega_o) dω_i`。

实时近似方案按复杂度从小到大：

1. **第一版**：从引擎现有天空环境/IBL 读取一条近似各向同性的“天空平均辐亮度”作为 ambient，**仅用于排除暗部错误**，参数单位在 M4 同体系；天空背景本身并不自动等于 cloud ambient。
2. **推荐版**：以 6 个方向（上、下、前、后、左、右）或 8 个方向拟合各向异性的 incident sky；正权重 `w_i≥0`、`sum w_i=4π`（方向积分）或归一化权重用于均值。方向性 HG phase 归一化，取样天空辐亮度。
3. 近似地平线/地表贡献：下方可取地面反照率/IBL，不能直接把向下采样误当和天空一样亮；先让该项可以单独关掉。

太阳到云的直接光仍来自 M4 的 `L_direct`。天空 ambient 不能乘 `SUN_LIGHT_POWER` 来补亮，也不能把 SkyView LUT 返回值直接作为太阳照度。

### B. 云内太阳遮挡与 ambient 分层

- `Li_direct = SunIrradianceAtCloud * T_cloud_to_sun * HG(mu)`。
- `Li_ambient = ApproxSkyIncidentIntegral(x, view_dir)`；可按环境曝光和太阳高度变化，但必须有明确来源/开关。
- `Li_total = Li_direct + Li_ambient + Li_multi_approx`。
- 对每步用 M4 的光学分部积分，只将 `Li_total` 放入单一 Source 入口：

```text
ΔL = T_view * albedo * (1 - T_step) * Li_total
```

其中 `Li_total` 是**不含 sigma_s** 的入射强度（以及相函数已并入相应子项），避免双重 sigma_s。

### C. 多次散射的渐进增强方案

**第一选项**：有限阶经验近似，通过光学深度和反照率调衰减/相函数，让云内部变亮。

- 明确每阶 `order_i` 的能量权重 `w_i`、有效 extinction scaling `a_i`、phase anisotropy `g_i`；参数不得使任意单个散射概率大于 1。
- 代码记录此方案是经验式，此时“有界”指 finite 与在定义域内经过校验的能量增益上界，**并不自动保证严格能量守恒**；必须以 CPU 高采样双向 reference 或受控 slab 结果审查。
- `albedo→0` 时所有阶散射趋 0；没有外部光时多次散射光源为 0；`L_approx≥0`，无负贡献。

**第二选项（只有第一选项验证不达标时）**：使用保守的 sky light volume/低阶多次散射 LUT，根据 height / optical depth / sun angle 估计光场，不要求当期引入 3D radiance cache。

### D. Powder / silver lining 放在最后

Powder 是视觉修饰，不应改 `sigma_t` 的 Beer 计算；可在 direct lighting 部分增加可控边缘修饰项，但先以 `powder=0` 通过本阶段基础验收，再单独开放弱修饰。

## Validation

| Test fixture | 量化或视觉验收 |
|---|---|
| 所有入射光=0 | 所有散射输出严格 0（允许浮点零误差）；T 与光无关 |
| ambient=0, multi=0 | 与 M4 GPU HDR 输出数值一致（ROI abs/rel 门槛按 source 格式设定） |
| isotropic sky, `g=0` | 环境角积分精确退化为该常量天空辐亮度（数值 quadrature 或权重保证） |
| sky intensity 0→0.5→1 | `Li_ambient` 单调非减；无无缘故自发光 |
| `albedo=0`、`rho=0` | in-scattering 为零（包括 multiple） |
| sun altitude 10°/45°/80° | 云顶/云底光照渐变、方向无逆转，记录 ROI 亮暗比 |
| 高云量/厚度变化 | 多次散射效果有界、连续、无 threshold 闪亮、无 NaN/Inf |
| multi on/off | 同一 camera/time 的 HDR ROI 差异仅在 cloud 内，天空晴空区域无值变化 |
| 单色白天与日落 | 符合光源变化，暗部颜色来自天空入射、不是后期统一染色 |

> 视觉验收需配备至少一组**高亮边缘 + 暗部云底**的同姿态对照；先证明云不是发光烟雾，再调参数审美。

## Acceptance Criteria

- [ ] `ambient` 不再硬编码为零，环境光 source 有单位、路径和 Debug View。
- [ ] M4 单散射结果一键恢复且像素对照通过。
- [ ] 两阶段 ambient（各向同性 reference/方向性 sky）有数值验收；环境光关闭时输出精确归零相应分项。
- [ ] Multi 是可关闭的独立项；零光、零密度、零 albedo 等极限通过，测试了能量上界/参考趋势。
- [ ] 昼夜/逆光/高云量 HDR 捕获、GPU validation 及性能成本存入 progress。
- [ ] PASS 后进入 M6；不得把尚未验证的 Powder 当作核心光照正确性的补救。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。多次散射的拟合误差属于**算法近似质量**，不能和单次散射数学 correctness 混为一谈：需写“近似与哪个 reference 的差”及可接受原因。Resume：README → progress → 本文件；若 M4 回归失败先回到 M4。
