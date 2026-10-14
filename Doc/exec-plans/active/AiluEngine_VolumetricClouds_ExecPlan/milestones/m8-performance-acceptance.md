# M8 — 性能档位、时序回归、最终交付验收

> 前置：M7 PASS。此阶段只能基于 M0-M7 通过的版本**进行有保护的性能优化**。

## Purpose / Scope / Non-goals

- 目的：确保所有形状、光照、时序算法与真实画质指标匹配；定量优化 Cloud pass 耗时而非以牺牲稳定性换速度。
- 范围：quality tiers、adaptive sampling / empty-space skip 候选、GPU timings、历史回归和最后的 Release/Debug 冒烟测试。
- 不做：无限制新增云算法、跨 RG 子系统重构、重写 FSR/DLSS/TAA。

## Architecture Constraints / Baseline / Target Outcome

- M0 的无历史 FullRes reference 为画质参考，并保留确定性锁定场景。
- 优化不改变函数的物理量含义，只可以改变求积效率和近似级别；所有优化都必须可单独关闭以作 A/B。
- 目标性能阈值不能虚构：从 M0 真机 p50/p95 与产品目标 FPS 反推预算。无法达到时记录画质/性能 Pareto 取舍，并保留正确画质模式。

## Relevant Files

全部 M0-M7 变更文件、`Test/TaaCloudAcceptance.h` / 对应 Analyze 脚本、RenderGraph 内 GPU profiling、Shader 编译日志与 ImageCapture。

## Implementation / Algorithm

### A. 质量分档（建议初稿，非硬编码的最终预算）

| 档位 | 可变参数 | 约束 |
|---|---|---|
| Reference | Full-res, no temporal, high view/light samples, no skip | 算法正确性基线；不可隐藏 |
| High | Full/half res 选择、适度采样、保守 temporal | 保留形状细节和稳定性 |
| Balanced | 较少 light shadow samples + sky ambient 近似 + temporal | 记录误差/性能，非同物理解 |
| Low | 低分辨率 + 有界历史 + 减少 detail sampling | 不牺牲基本几何、T、遮挡正确性 |

### B. 优化顺序必须按 GPU profiler 决定

1. 实测 ViewMarch、SunShadowMarch、Noise Fetch、Compose、TileReproject 每项开销。特别注意当前 High 128 view steps × 每步 8 sun steps，减一次重复 density/light shadow 的收益可能大于换 3D 格式。
2. 提前 `density <= epsilon` 跳过 SunMarch 与 source 光照，但仍按当前 `ds` 正确更新 `T`（密度零时恰为 1）。
3. 有界低频空域检测与步进：粗密度阈值不能假阴性误跳细云；在固定测试体积逐 voxel/pixel 验证跳过和 Full reference 的 false negative rate。
4. 分辨率策略：优先优化 full-res shader，再考虑半分辨率重建；Compare cloud edges 与几何遮挡位置，而非仅整体 PSNR。
5. Texture format/mip：减少带宽前比较噪声分布、三维边界 seam、T/亮度误差；对于 R16F 等数据格式先列出量化容差。
6. 所有 shader keyword 与风/云参数调整都必须 invalid history，确保新近似结果不会和旧 history 混合。`UpdateShaderParams` 当前低画质分支仅启用 `_QUALITY_LOW` 而未禁用 `_QUALITY_HIGH`，需将互斥关键字配置及实际变体哈希列入验收；不能仅观察 CPU bool。

### C. 端到端质量矩阵

必测：camera 下/内/上 × solar 高/低/背 × Coverage 0/0.5/1 × Wind 0/非零，叠加以下重点切片：

- Release/Debug、High/Low、FullRes/Tile、TAA on/off；至少 3 种分辨率（包括 317×179 和奇数宽高）。
- 静态 32 frame、相机平移/旋转 32 frame、云风动 64 frame、Camera Cut、Resize、Feature on/off。
- 记录 Depth/SceneColor/CloudRGB/T/HistoryUV/FinalHDR；特别检查低太阳高度地平线一圈和物体云层前后位置。

### D. 数值与主观指标并行

- 基础不变量：`rho ∈ [0,1]`, `T ∈ [0,1]`, `L >= 0` (finite)，单光/无光/无云极限；HG normalized 与均匀单次散射参考值。
- 对真实非均匀场景，以 Reference GPU (`Nview`/`Nlight` 高采样) 作参考；测 `ROI MAE、Relative L1、edge disocclusion failure、temporal ghost area`。
- 捕获数据量化后再设正式品质阈值；默认建议可先尝试云区域相对 L1 ≤5%、高对比边缘错位不超过 1-2 px，但**这是产品目标候选，不是数学定理或当前实测保证**。
- 性能：统一相机、天气、帧数、驱动/频率条件，记录 GPU p50/p95、分 Pass timer、峰值额外显存，比较各档位。

## Validation

| 最终 Gate | PASS 标准 |
|---|---|
| M0-M7 所有固定 tests | 全部 PASS；任意失败退回所属 M |
| Debug shader/Graphics Debug Layer | 无未初始化访问、非法 UAV、错误资源生命周期 |
| No-cloud/zero density | 最终 RGB 与无 cloud 完全一致（考虑 HDR RT 精度） |
| Constant density | Shader GPU 符合 M4 Beer/analytic reference |
| Sky/Cloud/Opaque/Fog/TAA | 各 pass 资源事件、遮挡、颜色正确，符合 M6 |
| 16 帧 Tile 更新 | 每像素被真实更新恰一次，含奇数尺寸 |
| Motion camera/wind/resize | 无大片 ghost、jitter double count、未赋值 history |
| Build | `cmake --build --preset build-debug` 及项目实际 Release preset/测试（需先发现并记录） |
| GPU perf | 真机稳定重复测量，符合**M0 测得并在 progress 明确批准**的预算；否则 FAIL/DEFERRED，禁止虚构 PASS |
| Code hygiene | 无并存未用的大型旧算法、常数单位明确，函数命名/行宽/分支清晰 |

## Acceptance Criteria

- [ ] 全部回归矩阵有脚本、结果、截图和实际测试设备信息；历史 FAIL 保留。
- [ ] Reference/High/Balanced/Low 差异、费用、限制写成用户能读懂的表。
- [ ] 画质/速度 Pareto 实测数据充足，选定默认设置并记录理由。
- [ ] final commit/review：物理量、source/phase、云壳、UV/jitter、RT alpha 都有明确契约。
- [ ] 更新 progress 全部 PASS 或明确注明没有满足的验收项；**只有全 PASS 才结束 ExecPlan**。

## Current Milestone / Progress / Decision Log / Surprises & Discoveries / Known Follow-ups / Outcomes / Resume Protocol

状态 **NOT_STARTED**。完成时归档成功和失败样本，列出仍有的透明排序/双云层/特殊日夜 edge cases 等限制；不要以打勾掩盖已知限制。Resume：README → progress → 本文件，按真实性验证每个 Milestone 已完成。
