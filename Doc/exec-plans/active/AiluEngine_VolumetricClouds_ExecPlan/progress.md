# Progress — Volumetric Clouds ExecPlan

> 该文件是运行事实账本。文档生成不等于已修改仓库或执行验收。所有状态初始化为 NOT_STARTED。

## Current Milestone

M0 — Baseline and Debug Infrastructure（**NOT_STARTED**）

## Progress

| ID | 状态 | 开始 | 完成 | 证据 |
|---|---|---|---|---|
| M0 | NOT_STARTED | — | — | — |
| M1 | NOT_STARTED | — | — | — |
| M2 | NOT_STARTED | — | — | — |
| M3 | NOT_STARTED | — | — | — |
| M4 | NOT_STARTED | — | — | — |
| M5 | NOT_STARTED | — | — | — |
| M6 | NOT_STARTED | — | — | — |
| M7 | NOT_STARTED | — | — | — |
| M8 | NOT_STARTED | — | — | — |

## Decision Log

- 2026-10-14：采用按子系统拆分的九阶段计划；首先用固定场景与独立 reference 测试隔离数学错误。
- 2026-10-14：计算与合成阶段优先维持 `RGB=散射辐亮度`、`A=transmittance`；对彩色吸收的支持不得依赖单个 alpha 近似。
- 2026-10-14：历史回归留到 M7，但从 M0 开始保存当前时间行为的基线。

## Surprises & Discoveries

- 静态检查：云壳外半径重复计入 `_height`、density 函数与球壳高度模型不一致、weather coverage 不生效、云型曲线结果被覆盖。
- 静态检查：光程与单位不自洽、云环境光为零、相位余弦经过不正确的 `[-1,1]→[0,1]` 映射。
- **待确认**：GPU 最终输出路径、Depth SRV 数据约定、浮点半精度误差、时序渲染路径是否均启用。

## Validation

**还没有执行任何仓库构建、GPU 截图、运行基准或 Shader/GPU 数值测试。** 本任务包附带的纯 Python CPU 数学 oracle 在文档生成环境中已独立执行（10/10 PASS），但这不构成 M0 的引擎验收。

每完成一项，追加原样信息：

```text
Milestone:
Date/Commit:
Configuration/GPU/Driver:
Command:
Inputs (camera/sun/time/seed/resolution/settings):
Expected equation / reference hash:
Observed numeric result (min/max/mean/error):
Capture/log path:
Status: PASS | FAIL | BLOCKED
Failure and corrective action:
```

## Blockers

- 尚无当前工作站的固定测试场景、实测 RGB/Depth/History 抓帧；M0 负责建立。
- 尚无 GPU 端均匀介质 test mode；M4 才有光照数值比对资格。

## Next Step

打开 `milestones/m0-baseline-debug.md`，冻结当前高质量全分辨率输出与 GPU timing；先用纯 CPU reference tests 核对基准实现，然后建立 GPU capture/debug 路径。

## Known Follow-ups

- 云对地形投影阴影、局部 3D 云、重度动态天气模拟，暂不进入本轮。
- 若 M6 发现需要 RGB transmittance，应另开决策项评估双 RT，而不是悄悄修改 Alpha 语义。

## Outcomes

- 预期：九阶段逐项独立验收，最终 cloud shape/light/temporal 有可回归证据。
- 当前真实结果：规划文件已生成，**尚未实现**。

## Resume Protocol

按 `README.md` 的 Resume Protocol 读取本文件与唯一当前 Milestone；禁止直接把里程碑状态向前跳。
