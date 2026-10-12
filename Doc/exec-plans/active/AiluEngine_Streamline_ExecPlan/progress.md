# Streamline ExecPlan · Progress

> 这是跨 context 的唯一进度事实来源。只有在测试真正发生之后才改为 Pass。当前文件由规划阶段生成，未进行工程修改。

## Current Milestone

**M0 · SDK/Runtime 最小引导**。入口：`m0-sdk-runtime.md`。

## Progress

| 项目 | 状态 | 证据 |
|---|---|---|
| 源码基线识别 | Done（静态阅读上传快照） | `AiluEngine_Source(20261012-111424).zip` |
| 官方 SDK 版本核对 | Done（网页确认） | Streamline SDK v2.14.1 |
| SDK 安装 / 编译配置 | Pending | |
| M0 初始化、hook 与能力查询 | Pending | |
| M0 验收 | Pending | |
| M1 验收 | Pending | |
| M2 验收 | Pending | |
| M3 验收 | Pending | |

### 环境与复现信息（由执行者填写）

- 工作树路径 / commit：未记录
- MSVC / CMake / VS 版本及编译配置：未记录
- Windows / GPU / RTX 驱动版本：未记录
- SDK 路径及发行版本：未安装/未记录
- Feature plugins/runtime DLL 路径：未记录
- 编译命令：未记录
- 运行命令/测试场景：未记录

## Decision Log

- P001（2026-10-12）：计划分四阶段；M0 仅 SDK + hooking/capability，M1 仅桥接，M2 SR，M3 稳定性。
- P002（2026-10-12）：默认缺失 SDK 时支持无 SL 的编译；SDK 二进制和 plugin 版本必须保持一致。
- P003（2026-10-12）：当前仅产出执行计划，不伪造编译/帧截图/设备运行结果。

## Surprises & Discoveries

- `CommandCustom` 已存在，但在 CommandBuffer 中没有直接公开录制方法。
- 旧 Postprocess 纹理维度来自 `RenderingData::_width/_height`，不是 Postprocess 实际输入 RG Texture。
- 官方 Manual Hooking 要求每帧正确执行 `presentCommon()`。
- 上传快照不包含足以确认用户真实本机编译过程的全部环境信息。

## Validation

| Milestone | 编译 | 运行 | Debug Layer / PIX | Acceptance Criteria | 状态 |
|---|---|---|---|---|---|
| M0 | 未执行 | 未执行 | 未执行 | 未执行 | Pending |
| M1 | 未执行 | 未执行 | 未执行 | 未执行 | Pending |
| M2 | 未执行 | 未执行 | 未执行 | 未执行 | Pending |
| M3 | 未执行 | 未执行 | 未执行 | 未执行 | Pending |

## Blockers

- 尚无当前工作树与本地编译 / RTX 显卡测试记录。开始 M0 时由执行者核实；不能当成已验证。

## Next Step

读取 `m0-sdk-runtime.md`，完成 M0 第 1 项：核实代码基线、Streamline SDK v2.14.1、项目编译入口、GPU 和运行环境。

## Known Follow-ups

- SR 成功后再评估 RR 与 FG 独立接入计划；不在 M0 阶段创建空 Feature 接口体系。

## Outcomes

- [ ] M0 SDK & Hook 通过
- [ ] M1 原生命令/资源桥接通过
- [ ] M2 DLSS SR 图像闭环通过
- [ ] M3 工程稳定性通过

## Resume Protocol

每次新 session 先读取 README、本文件和当前 mX，检查工作树；完成当前阶段前严禁跳下一阶段。重要变化写 Decision Log；构建失败及复现日志写 Validation/Blockers，保留未完成步骤。阶段完成后更新 `Current Milestone`、`Next Step`。
