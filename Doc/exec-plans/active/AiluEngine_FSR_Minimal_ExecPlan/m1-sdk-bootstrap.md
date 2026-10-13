# M1 — SDK Bootstrap & DX12 Runtime Capability

## Purpose

建立一个**独立、最小且可验证**的 FSR 3.1.5 DX12 SDK 使用入口，不接 RenderGraph，不改 Camera，先确认 DLL/ABI/Provider/设备支持。

## Scope

- 锁定官方 FidelityFX SDK 2.x 版本，提取必要头文件及 2 个签名 DLL。
- 动态加载 API，查询当前 DX12 Device 的 Upscale provider，选中名称/版本匹配的 **FSR 3.1.5**（禁止意外用 FSR 4）。
- 运行时可观测的 Init/Shutdown/IsSupported/Error，缺 SDK 可平稳退化；所有 DLL 生命周期与 DX12 Context 关联。
- 集成仅用于编译/调用探测；暂不创建/执行完整超分 RenderGraph Pass。

## Non-goals

不实现 FG/FSR4；不改 FrameGen Swapchain；不搭建通用插件系统；不实现 Camera 画质设置；不做无法测试的“假成功”代码。

## Architecture Constraints

沿用现有 `AILU_WITH_STREAMLINE` 风格处理可选 SDK 构建，但用独立开关（如 `AILU_WITH_FSR`）。所有 `ffx*` 定义只暴露在 `Engine/Src/RHI/DX12` 私有实现内部；头文件只用 opaque 或 engine 类型；无可用 SDK 时依然能编译并运行引擎。

**注意**：SDK C++ wrapper 的 `ffx::CreateContext/Query` 可能直接链接符号；本阶段采用原始 C 函数指针，对应 `ffxCreateContext`、`ffxDestroyContext`、`ffxDispatch`、`ffxQuery`、`ffxConfigure`。不要调用 wrapper 再假设纯 `LoadLibrary/GetProcAddress` 就能链接。

## Baseline

- `Engine/Src/RHI/DX12/D3DContext.cpp` 已初始化 `_streamline_runtime`；Streamline SDK hooks/Device proxy 不应修改。
- `Engine/Src/RHI/DX12/StreamlineRuntime.cpp` 是动态加载风格参考，但 FSR 不应复用其内部 sl API 锁或 DLL 生命周期。
- SDK 2.x 官方二进制拆分：loader `amd_fidelityfx_loader_dx12.dll` + upscaler `amd_fidelityfx_upscaler_dx12.dll`。

## Target Outcome

在支持的 DX12 机器上，日志准确打印 SDK 版本、所选 FSR Provider 版本与 `supported`；缺 DLL 或不支持时返回 `unavailable`，无崩溃、不影响 DLSS 与原有渲染。

## Relevant Files

- `Engine/Inc/RHI/DX12/D3DContext.h`, `Engine/Src/RHI/DX12/D3DContext.cpp`
- `Engine/Inc/RHI/DX12/StreamlineRuntime.h`, `Engine/Src/RHI/DX12/StreamlineRuntime.cpp`（只参考）
- 新增建议 `Engine/Inc/RHI/DX12/FsrRuntime.h`, `Engine/Src/RHI/DX12/FsrRuntime.cpp`
- 真实仓库的构建文件（此上传源码包未包含；执行时先定位）。

## Milestones

本文件是 M1；验收后执行 `m2-render-path.md`。不跨阶段实施。

## Current Milestone

**M1**。

## Implementation Steps

### S1. SDK 依赖与版本锁定

1. 从 SDK 官方仓库下载**匹配版本的 headers + 签名 minimal package**，优先从 2.3.0 开始并记录实际下载版本/包来源；核查下列文件实际存在：
   - `amd_fidelityfx_loader_dx12.dll`
   - `amd_fidelityfx_upscaler_dx12.dll`
   - `ffx_api.h`、DX12 backend + Upscale API 的头文件（准确 include 路径以该 SDK 发行包为准）。
2. 在既有 ThirdParty 约定目录配置 include/运行时 DLL 复制；**不要**引入 SDK samples/Cauldron、FrameGeneration DLL、静态 SDK 源码构建。
3. 构建选项默认关或根据存在的 SDK 置开；无 SDK 时必须有完整 stub；引擎不会尝试从用户工作目录意外加载不明 DLL。
4. 明确 SDK 的 release/debug DLL 命名和位数，禁止运行时 DLL/header 版本不一致；在日志中报告具体文件路径与版本。

### S2. Runtime 最小接口

建议类 `Ailu::RHI::DX12::FsrRuntime`，公开最少：`Init(ID3D12Device*)`、`Shutdown()`、`IsSupported()`、`GetError()`（类型按工程约定调整）。

- 成员可包括 `_module`, `_device`, `_api`, `_selected_provider_id`, `_status`，不要引入不必要虚接口/注册表。
- `Init` 使用 `LoadLibraryEx` / `GetProcAddress` 对 5 个入口取址；失败后清理所有已经初始化的对象。
- 不在 Runtime 头文件包含 SDK headers（可用 private PIMPL/前置声明/opaque 句柄，只选最简单方案）。
- DX12 Device 以现有 D3DContext 持有的原生 Device 为准，**不得替换为 FSR 代理 Device**。

### S3. Provider 枚举和严格版本匹配

1. 按当前 `ffx_api.h` 声明设置所有 descriptor 的 `header.type` 与 `header.pNext`。
2. 使用 `ffxQuery(nullptr, ffxQueryDescGetVersions...)` 获取 Upscale 版本 ID 和名称，**同时读取同一 query 结果**，复制名称到自有字符串。
3. 选择明确标记为 FSR 3.1.5 的 ID（支持 SDK 的实际 naming），记录 ID/版本/失败原因；不能 hardcode ID，也不能默认认为首个 Provider 就是 FSR3。
4. SDK >= 2.1 创建上下文还需按目标版本要求补充 `ffxCreateContextDescUpscaleVersion` / `FFX_UPSCALER_VERSION` 等结构，再把查询到的 Provider ID 以 `ffxOverrideVersion` 链入；**最终结构连接以锁定 SDK 的头文件及 sample 为准**。
5. 查询失败或没有目标版本则 `unavailable`；不可悄悄升级到 FSR4。验证在其他 DX12 GPU 上也不会误选 ML provider。

### S4. 集成 D3DContext 生命周期

- 在 DX12 Device 可用之后探测 FSR；不要早于可用的 ID3D12Device 初始化。
- D3DContext 释放 GPU/Device 时先停止新 FSR 使用并清理 Runtime；确保今后 M2 的 Context destroy 能安排在 GPU 工作完成后执行。
- `Init` / `Shutdown` 幂等，失败日志只打印一次，避免每帧刷屏。严格杜绝日志中输出“supported”但实际 DLL/provider 不可用。
- 可选：将探测延后到首次启用 FSR，避免非 FSR 用户不必要的初始化成本；若延迟初始化，务必可在安全的主/渲染线程同步点执行。

### S5. 最小测试钩子

- 增加仅在 debug 内可调用的能力日志，不增加 Editor UI。
- 测试 SDK DLL 齐全、删除任一 DLL、错误 SDK header/DLL 配对、非 DX12 Device、DLSS 正常启动五种路径。
- 新代码仅包含能力判断；M1 不需要渲染实际 FSR 图像，也不允许为通过测试改动 TAA/MV shader。

## Acceptance Criteria

- [ ] Debug/Release（实际已有配置）构建成功；SDK 宏关闭时同样成功，且没有 FFX 符号链接错误。
- [ ] 完整 DLL + 支持的 DX12 环境日志显示 Provider 为 **FSR 3.1.5**（提供原始版本/ID）。
- [ ] 删去 loader 或 upscaler DLL 后引擎依旧启动且标准渲染正常，日志不刷屏。
- [ ] 无关的 DLSS 模式正常；没有替换原有 Device/Swapchain。
- [ ] 退出时无 DLL 生命周期异常；不静默升级到 FSR4。
- [ ] `progress.md` 写明 SDK 锁定版本、修改文件、测试命令/截图及失败结果。

## Progress

`NOT STARTED`，细节由 `progress.md` 更新。

## Decision Log

默认用 C ABI 加载，保留 Streamline 独立路径。若 SDK 发行物与文档 API 不一致，以实际头文件修订本阶段实现并记录差异。

## Surprises & Discoveries

待填；不推测 SDK 中不存在的结构类型或参数。

## Validation

编译 + 启动日志 + DLL 缺失测试；机器/显卡/驱动、DLL hash 和 provider ID 必须留下证据。若无 Windows/DX12 编译环境，状态写 `BLOCKED`，不能推进 M2。

## Known Follow-ups

M2 将增加 Context 创建、Dispatch、GPU 资源管理；不要在此阶段提前开发一套全能 Context 管理器。

## Outcomes

待验收。

## Resume Protocol

阅读 `README.md` + `progress.md` + 本文件；完成 S1–S5 并复验 Acceptance Criteria；所有勾选项通过才切换 M2。
