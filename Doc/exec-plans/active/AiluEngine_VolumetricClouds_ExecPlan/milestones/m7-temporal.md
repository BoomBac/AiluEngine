# M7 — 运动云的 History、Reprojection 与 4×4 Tile 更新

> 前置：M6 PASS。全分辨率无历史始终是画质参考路径，不允许为历史速度而降质参考。

## Purpose / Scope / Non-goals

- 目的：避免场景 MotionVector 与云深度/风场不一致引起拖影、模糊、错位与历史失效。
- 范围：可靠云重投影、cloud depth/velocity proxy、rejection、checkerboard/tile 更新、风动和 TAA 兼容。
- 不做：修改全局 Motion Vector 物理意义；用时间累积掩盖 M1-M6 数值问题；直接引入 DLSS/FSR SDK。

## Architecture Constraints / Baseline / Target Outcome

- 旧 `CloudReprojection` 从 `_MotionVectorTexture[pixel_index].xy` 得到静态场景 UV shift，再使用 `_JitterPrev-_JitterCur`。这不是云自己的运动场；云没有显式代表深度时静态天幕 MV 无法表达云的平移视差。
- Current RG path 在历史无效时整帧 seed，再按 16 frame 进行 4×4 逐像素更新；保留 feature flag，但**静态与动态质量验收分开**。
- `CloudMain` 必须导出与历史一致的代表云深度或可重投影的云采样位置，建议单独 R16F/R32F CloudDepth 作为可选，或一个可信的云层近似距离（需误差验证）；不要在 RGB Alpha 中偷偷塞 depth。

## Relevant Files

`VolumetricClouds.cpp/.h`, `Compute/volumetric_cloud.hlsl`, `Camera.cpp`（当前/前帧矩阵获取），`Test/TaaCloudAcceptance.h`, `Test/AnalyzeTaaCloudAcceptance.py`。

## Implementation / Algorithm

### A. 先定义运动的几何语义

若 cloud density 按 `rho(x,t)=rho0(x-wind_velocity*t)` 平移，则当前云特征点位于 `x_cur`，历史时刻同一特征点为：

`x_prev = x_cur - wind_velocity * Δt`。

通过 previous **jittered/no-jitter** VP 的明确约定求 `uv_prev = Project(previous_view_projection, x_prev)`。**不要**同时使用已经包含 camera motion 的 MV 再重新施加相机矩阵移位，避免双计；选择一种清晰的重投影路径。若矩阵是 no-jitter，则在规范化的位置加一次 previous jitter，不能再手工加 `_JitterPrev-_JitterCur` 两次。

### B. 云不是固体表面：代表距离及可信度

- 从 M4 raymarch 中按 `contribution_weight`/光学厚度等导出 `cloud_reprojection_depth`，例如首次明显密度、加权期望距离（两种需测试）；与 RGB/T 分开。
- 密度非常稀疏、双层重叠、薄云和云边缘没有唯一代表深度；因此 history **只是一种近似**。为它们提供 `confidence` 与更强的历史拒绝，不强制稳定 16 帧。
- 对没有云的射线 `T≈1`，无效 depth 并直接使用当前真实天空背景，勿用固定任意深度进行 motion。
- 调整风场与 Detail flow 的关系：主风场可精确补偿；细节噪声独立滚动属于形变，新/旧差异应由拒绝和更新率吸收，不把错误投影当精确运动。

### C. 有效历史检查

采样 history 前检查：UV 在 `[0,1]`、前帧 cloud depth 有效、深度与当前估计一致、当前/历史的 T 差异、参数 hash 未变、相机未切换/投影未突变、资源尺寸一致、风场时间连续。

- `history_valid=false` 的第一个 frame 先全分辨率 CloudMain；**不得在计算 1/16 更新时沿用未初始化的历史**。
- 历史无效 pixel 必须**直接重算当前像素**或显式使用经过验证的近邻空间补洞；不可默认输出透明 `(0,0,0,1)`，否则会形成黑洞/闪烁。
- 减少历史 ghosting 可用 T/clamp 与有限窗统计；不允许对 HDR 亮度进行无解释 clip 导致云边永久损失。
- history ping-pong 在真正 Dispatch/Compose 完成后提交，与原 RG guard 相容。

### D. Tile 调度的整数边界

现有 4×4 tile 更新中 `CalculateDispatchNum(..., width >> 2, height >> 2, ...)` 对非 4 倍数尺寸存在边缘遗漏风险；应采用 `ceil_div(width,4)` 和 `ceil_div(height,4)`，并依赖 shader 内 `pixel_index < dims` 剔除越界线程。

- 16 种 offset table 必须覆盖 `(x mod 4, y mod 4)` 全组合且无重复。
- 测试奇数分辨率 317×179、321×181、101×37；每 16 帧每个有效 pixel 被真实更新**恰好一次**（不计 invalidation 全帧 seed），不可用整帧均值替代 coverage 验收。
- 缺历史时 tile keyword 必须关闭，避免无效像素读取未写 CloudTex。

### E. 新旧模式分开标定

先实现 FullRes+History off，继而 FullRes+Temporal（若有），最后 Tile+History。对同一移动云 64 帧记录：`T`、cloud RGB、cloud depth、mv_debug、reprojection uv、history_reject mask。

## Validation

| 条件 | 必须达到的结果 |
|---|---|
| 冻结 camera/wind/time | History UV 与同像素一致（只出现定义内的 jitter，像素误差按 float32 容差） |
| 只有 camera rotation | 旧帧采样对应云层位置；无天幕 MotionVector 式错位 |
| 只有 camera translation | 云深度导致合理视差；与 FullRes reference 结构对齐 |
| 只有 wind +v | 前一帧位置 = 当前同云特征点 `-v*dt`；方向不反转 |
| 独立 detail motion | 有序减低 history confidence，而不是固定沿用错误纹理 |
| 16 offset | 单独 16 帧 update mask 累加，每像素恰 1 次 |
| resize 317×179 / 321×181 | 最外侧像素会刷新，边缘不出现固定透明/旧值 |
| Camera cut / parameter mutation | 全像素 seed，不读未知 history |
| 64 帧移动场景，Tile on/off | 测 MAE/SSIM/边缘 ROI（先采集并设门槛），不能出现稳定高对比 ghost/banding |
| TAA on/off | 双重重投影不造成明显额外拖尾，报告二者差值 |

## Acceptance Criteria

- [ ] history reproject 使用云自己的代表深度/风场，或提供有误差上界的简化方案并通过对照。
- [ ] UV 与 jitter 只有一种清晰数学路径，运动/静止/相机移动测试全部有真实捕获。
- [ ] 4×4 tile 16 offset 完整覆盖；奇数尺寸边界没有固定旧像素。
- [ ] HistoryValid、Resize、Camera Cut、Quality Switch 重置时无未初始化读取。
- [ ] 与 FullRes reference 的时序差异、视觉拖影、性能可量化；GPU validation/TAA acceptance 无回归。
- [ ] 更新 progress 后通过 M7 才进入 M8。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。云的近似 reprojection 是质量工程，不是能对所有体积云保证“像素精确”的几何算法；必须明示当前适用范围与误差。Resume：README → progress → 本文件。
