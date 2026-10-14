# M4 — 正确的 Beer–Lambert / HG / Sun Shadow / 单次散射积分

> 前置：M3 PASS。这是**数值正确性核心里程碑**；不得以画面风格判断代替解析解比较。

## Purpose / Scope / Non-goals

- 目的：让 raymarch 的每个采样位置、消光、散射、日光方向和步长共享统一单位；修复现有不自洽的经验光照链。
- 范围：sigma_t/sigma_s、HG phase、向太阳的透射、直接光、解析分步散射、光学厚度的 CPU/GPU oracle。
- 非目标：Sky ambient、multiple scattering、Powder、silver lining、材质着色拓展（下一阶段）。

## Architecture Constraints / Baseline / Target Outcome

- 输入密度来自 M3，范围 `[0,1]`，世界坐标单位 `m`；`sigma_t` 单位 `1/m`，`sigma_s = albedo * sigma_t`，`albedo ∈ [0,1]`。建议优先**标量消光与散射**，保留线性 RGB 光源辐亮度。
- 修复原 `float BeerLambert(float3 sigma_a, float d)` 造成 `float3→float` 损失的问题；不准隐式截通道。
- 原 `MAIN_RAY_THINKNESS=0.07`/`LIGHT_RAY_THINKNESS=0.07` 与真实 `step_length` 无关；取消这种输入不透明的光学距离 hack。
- `LightMarch(..., dist, ...)` 原本 dist 不用于积分且与 light_dir 求交方向不一致；改为统一 Sun ray direction `to_sun_dir`。

## Relevant Files

`Compute/volumetric_cloud.hlsl`（包括 `BeerLambert/Phase/LightMarch/MultipleOctaves/CloudMain`），`atmosphere_common.hlsli`（仅读参数/可能的 LUT），`sky_atmosphere.hlsl`、`Renderer.cpp`、`VolumetricClouds.h/.cpp`（参数迁移）、CPU oracle。

## Implementation / Algorithm

### A. 数学约定与函数职责

对一路视线离散采样区间长度 `ds`：

```text
rho(x) = SampleCloudDensity(x)                    // dimensionless
sigma_t(x) = rho(x) * base_extinction             // m^-1
sigma_s(x) = rho(x) * base_extinction * albedo    // m^-1
T(ds) = exp(-sigma_t * ds)
T_view_after = T_view_before * T(ds)
```

采用稳定的局部解析积分。若源项 `Li = incident_radiance * phase` 在小步内近似常数，则：

```text
ΔL = T_view_before * Li * albedo * (1 - exp(-sigma_t*ds))
```

等价于 `T_before * (sigma_s*Li) * (1-Tstep) / sigma_t`；使用 `-expm1(-tau)`（或 float shader 对小 tau 用级数/稳定近似）避免极小光学厚度丢精度。当 `tau→0` 时 `ΔL ≈ T_before * Li * sigma_s * ds`，不应出现 0/0。

> 若将 `Li` 定义成已包含 `sigma_s` 的 source，公式必须用 `source*(1-T)/sigma_t`，**绝不能再乘 sigma_s 一次**。代码注释应写明 source 的定义与单位。

### B. HG 相函数与光照方向

标准归一化 HG：`p(g, mu)=(1-g²)/(4π*(1+g²-2g*mu)^(3/2))`，`mu ∈ [-1,1]`，`|g|<1`。

Renderer 当前 `_MainlightWorldPosition` 储存光线**传播方向** `light_travel_dir`；取 `to_sun_dir = -light_travel_dir`。`view_dir = normalize(camera→sample)`，相函数 `mu = dot(light_travel_dir, -view_dir)`；等价于 `dot(to_sun_dir, view_dir)`。禁止像当前代码那样 `(dot+1)/2` 再送 HG。

- `g>0` 时朝向太阳看（前向散射）比反太阳方向更强。
- 选择有理的两瓣混合 `w*p(g_f,mu)+(1-w)*p(g_b,mu)`；`w∈[0,1]`, 两瓣加权应归一化。
- 先用 `g=0`（各向同性）完成解析参考，再启用 g_f/g_b；`|g| <= 0.95` 防止极端尖峰（实际需质量参数控制）。

### C. SunTransmittance：光程积分

从当前 sample **沿向太阳方向** `p + to_sun_dir*s` 射线，求云壳**同向**剩余有效区间 `s∈[0,s_exit]`；在合法距离上积分：

```hlsl
float SunTransmittance(float3 sample_pos, float3 to_sun_dir, float ray_length)
{
    float tau = 0.0f;
    float step_length = ray_length / _light_step_count;
    for (uint i = 0u; i < _light_step_count; ++i)
    {
        float3 position = sample_pos + to_sun_dir * ((i + 0.5f) * step_length);
        tau += SampleCloudDensity(position, false) * _base_extinction * step_length;
    }
    return exp(-tau);
}
```

这里的 `ray_length` **必须是实际裁剪云段长度**，不能像旧代码固定用 1600m 代替。若要主动上限 `max_sun_shadow_distance`，将其定义为**有损性能优化**，Debug 记录截断比例。`SampleCloudDensity(...false)` 的降细节是近似，必须与 Detail on 的 oracle 路径量化误差。

### D. 太阳能量与 LUT

- Sun light from engine 需要约定：`_DirectionalLights[0]._LightColor` 的意义是否已经是 irradiance、`_MainlightColor` 是否乘 π（当前 Renderer 会乘 π）；**只能选择一条经过确认的能量链**。
- 取消 `SUN_LIGHT_POWER=300` 的双重放大，提供明确且单一的 exposure/irradiance scale。要先验证昼夜方向、光强随太阳高度变化。
- `getValFromSkyLUT()` 返回方向性天空辐亮度，不等价于阳光沿 sample→sun 方向的透射率；正确的 direct sun 路径应复用或采样 Atmosphere Transmittance LUT，并以**云样本的径向高度、太阳天顶角**定位；明确大气坐标与渲染器世界坐标换算。
- **常量 sun irradiance 仅用于隔离式数学参考测试**；M4 集成验收必须实现正确 Atmosphere Transmittance LUT 采样，或显式定义在本引擎无大气模式下的恒定太阳边界条件并使非大气场景验证通过。带大气天空模式不可把 SkyView 伪装成太阳直射透射率后判定 PASS。

### E. 不允许重叠源项 / 早停

- 将旧 `MultipleOctaves()` 暂时禁用，单次散射中只乘一次 phase；环境光 0；Powder off；Linear HDR，Exposure=1。
- `T_view` 早停用 scalar、适当阈值（示例 0.005），若模拟黑云则验证提前停止前后的 `L` 差是否在误差阈内。

## Validation — 数学基准必须用 CPU double oracle 独立计算

| 用例 | 解析期望 | 验收 |
|---|---|---|
| `rho=0`, 任意长度 | `T=1, L=0` | 测 8/32/128 步，误差<1e-6 |
| `rho=1`, `sigma_t=0.002/m`, `L=1000m` | `T=exp(-2)=0.1353352832` | GPU float source RT 相对误差 ≤0.2%；如 half 捕获按实测量化放宽并证明合理 |
| `rho=1`, `sigma_t=0.002/m`, `albedo=0.8`, `Li=1`（phase 已折入 Li） | `L=0.8*(1-exp(-2))=0.69173177` | 同上；`N=8,32,128` 不依赖步数 |
| `g=0`，任意 mu | `p=1/(4π)` | 10k 输入，最大绝对误差 ≤1e-6 |
| `g=0.7` 和 `g=-0.3` | `2π∫p(g,mu)dmu=1` | CPU 数值积分相对误差 ≤5e-4；GPU 离散散射对照 |
| 固定密度日光路径 | `T_sun=exp(-sigma_t*length)` | CPU/GPU 长度相同、不同采样数结果趋同 |
| 朝/背太阳同密度 | `p(g>0,mu=1)>p(g,mu=-1)` | 亮暗方向与定义一致，太阳方向切换后对称 |
| `sigma_t→0`、`albedo→0` | 有限、单调，零散射无亮度 | NaN/Inf=0 |
| 强云/弱云 | `T∈[0,1]`, `L>=0` | 任何执行路径不违反 |

详见 `validation/gpu_reference_vectors.json`（从项目根目录打开）作为 GPU 比较输入；CPU `cloud_reference_tests.py` 的结果只是**参考公式自测**；必须增加 Shader 特定 debug 模式，将与 CPU 一模一样的确定性样本送进 GPU，读回线性 `R32F`/`RGBA32F` 后比较，才能判 M4 PASS。

## Acceptance Criteria

- [ ] CloudMain 不再采用与实际 ds 无关的 `MAIN_RAY_THINKNESS` 或错误矢量标量截断。
- [ ] Beer 法则 / HG 相函数 / SunTransmittance / homogeneous closed-form 单元与 GPU 对照全部通过。
- [ ] Sun direction 与传播方向经 Renderer 实码和朝/背太阳测试一致，太阳光强唯一来源有明文单位。
- [ ] 移除单散射路径重复 phase/powder，多次散射不污染 reference。
- [ ] 强度不再通过 `SUN_LIGHT_POWER`/`pow(exposure)` 强行补偿；可解释光学系数支持独立调节。
- [ ] Full-res 32/128 采样随步数收敛，恒密度解析场景即使 8 步也符合参考；登记实测误差。
- [ ] 经过 Shader validation，无 NaN/Inf，更新 progress 后方可进入 M5。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。若单位迁移导致旧场景亮度剧变，这是预期的物理尺度变更，先通过参考公式重标定，严禁回插硬编码 `*300`。记录选择 scalar/RGB extinction 的原因。Resume 顺序 README → progress → 本文件；只有硬数值验收通过才算 Outcome。
