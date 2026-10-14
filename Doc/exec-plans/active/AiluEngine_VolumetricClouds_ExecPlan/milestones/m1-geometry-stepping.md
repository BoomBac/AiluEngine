# M1 — 球形云壳、径向高度、稳定 Ray March 区间

> 前置：M0 PASS。目标是修正几何，不改变噪声与光照参数；用固定密度 test mode 证明。

## Purpose / Scope / Non-goals

- 目的：同一空间模型用于云壳相交、高度密度和太阳光求交，消除半径双加、未命中使用脏值、可变步长错误。
- 范围：cloud-local ray/shell intersection、径向高度、相机在云上下内、光程上下界、Jittered midpoint sampling。
- 非目标：编辑通用 `geometry.hlsli` 的其他系统、重构摄像机投影、调 Noise/Light、引入复杂 adaptive march。

## Architecture Constraints / Baseline / Target Outcome

- 摄像机偏移到 `cam_pos.y += EARTH_RADIUS`，地球球心原点；**云下边界半径** `r_inner=R+height`，**云上边界半径** `r_outer=r_inner+thickness`。
- 旧代码把外半径算成 `R+height+(height+thickness)`，而 `GetHeightFrac` 根据 y 求高度；必须统一。
- 球壳可能沿单条 ray 出现多个不相交区间；视线从地面、云内和云上都应正确返回可见部分。记录 planet occlusion 策略（优先裁掉地球球体遮挡）。
- 最终 Shader 移除对未检查成功的 `RaySphereIntersection` 的依赖；不修改共用库，新增 `IntersectCloudShell()`。

## Relevant Files

`Compute/volumetric_cloud.hlsl`, `geometry.hlsli`（只读）, `VolumetricClouds.cpp`（参数与 Debug），`validation/cloud_reference_tests.py`。

## Implementation / Algorithm

**A. 每个球体返回两个带符号的射线参数根。** 采用单位 ray direction：

```text
oc = ray_origin - earth_center
b = dot(oc, ray_dir)
c = dot(oc, oc) - radius*radius
d = b*b - c
if d < 0: no interval
roots = (-b - sqrt(d), -b + sqrt(d))
```

不要只返回一个交点；不要把负根在函数内任意替换，否则无法准确求云壳区间。

**B. 云壳 = 外球区间 ∩ [0, t_scene] − 内球区间。**

- 外球交区间 `[outer_enter, outer_exit]` 与 `[0, min(t_scene, max_distance)]` 相交。
- 内球交区间 `[inner_enter, inner_exit]` 是**无云的中空区**，从外区间中裁掉。
- 可能形成 0、1、2 个云壳区间；先保证回传第一个可见区间，后续云层追踪若允许第二段，需明确行星/地表遮挡。对真实地球实体应 clip ground intersection。
- 对射线平行/擦切边界、刚好在壳上、相机处在壳内等情况用 `epsilon` 处理，判定 epsilon 取**长度空间**且不应依赖随意硬编码。

**C. 高度归一化统一：**

```hlsl
float GetCloudHeightFraction(float3 world_pos)
{
    float radial_height = length(world_pos - _earth_center) - _earth_radius;
    return (radial_height - _height) / max(_thickness, 1.0e-6f);
}
```

若已有世界→地球球心的 `cam_pos.y += R` 转换，**不能再次加 R**；存储和采样位置必须遵循同一空间。云层外 `[0,1]` 之外直接返回 0（在 M2 修改）。

**D. 主射线步进**：对裁剪后的 interval `[t0,t1]`，定义 `step_length = (t1-t0)/N`，第 i 个 sample 使用 `t0+(i+xi)*step_length`，`xi∈[0,1]`（reference 固定 0.5，时序可以蓝噪 jitter）。**不把 sample jitter 加到 t0 导致总区间遗漏**。

**E. 变化步长只能用累加** `t += step_length`，不可继续使用 `step * step_size` 同时逐步修改 `step_size`。先采用固定 N；Adaptive 作为 M8 评估项。

**F. 场景遮挡**：M1 固定背景（`t_scene=+inf`）证明几何；场景深度 clip 的实际接入留 M6，避免交叉变量。

## Precision and boundary rules

- CPU oracle 用 double，GPU 用 float，地球半径 `6.371e6` 加米级小高度可能有精度风险；**差分高度与相机参考系须评估浮点误差**，若 mm/m 精度不足采用局部坐标或相机相对计算，禁止只提高 epsilon 掩盖问题。
- `max_distance` 是工程可见范围，不是云层厚度；只沿 ray 的 t 裁剪，不得影响 `GetCloudHeightFraction`。
- `r_outer>r_inner>R`, `N>0`, normalizing ray before intersection 为输入前提；校验材质参数。
- 无命中时 Cloud RT = `(0,0,0,1)`，且不得出现随机 NaN/Inf。

## Validation

| CPU/GPU 向量（示意球 `R=100`, `height=10`, `thickness=10`） | 预期 |
|---|---|
| origin `(0,100,0)`，方向 `+Y` | `[10,20]` |
| origin `(0,115,0)`，方向 `+Y` | `[0,5]` |
| origin `(0,115,0)`，方向 `-Y` | `[0,5]`（先到内球），之后内球是空区 |
| origin `(0,130,0)`，方向 `-Y` | 第一段 `[10,20]`；允许对第二段单独定义遮挡政策 |
| origin `(0,130,0)`，方向 `+Y` | 无命中 |
| 切线、近切线、不相交、相机位于球壳边界 | 有限值；区间单调，长度非负 |
| 10000 组随机射线 | CPU double 的第一个可见壳区间与 GPU float 差（接收阈值按输入量级设定） |

重要的不变量：`∑step_length = t1-t0`（无 early termination 时）；恒定密度采样步数 8/32/128 不改变光学累计长度；径向高度对位于内外球面点分别接近 `0/1`。

## Acceptance Criteria

- [ ] `r_outer` 计算修正，有文档化球心/坐标空间。
- [ ] CPU double test 对命中区间和高度 PASS；GPU 导出同一向量的误差符合规定容差。
- [ ] 覆盖 above/inside/below + horizon 的 Debug Interval 截图，不再出现云壳向下缺失或突然突变。
- [ ] Ray March 区间不会重复、跳跃或越界；未命中不使用未定义的交点。
- [ ] 32/128 步的固定介质积分长度一致，所有 debug 输出有限。
- [ ] 编译及 GPU validation 无错误，更新 `progress.md` 后才开 M2。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。记录浮点精度和实际 `EarthRadius` 取值，记录场景深度裁剪政策。成果应是可复用的 `IntersectCloudShell` 与正确 sample interval；光照正确性留 M4。恢复只读 `README.md` / `progress.md` / 本文件；PASS 前不进入下一阶段。
