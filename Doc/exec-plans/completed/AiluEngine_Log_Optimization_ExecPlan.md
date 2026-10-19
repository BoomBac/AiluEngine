# AiluEngine 日志系统治理与格式化增强 — ExecPlan

> 类型：单文件执行计划（交付 Codex / Claude）  
> 基线：AiluEngine `master`，2026-10-19 源码审计；以实现时检出的实际工作树为准  
> 状态：**已完成 / Current Milestone = M3（全部验收通过）**  
> 原则：**最小侵入、先修正确性再降噪、真实异常不能被静音；每个 Milestone 验收后才能进入下一阶段。**

## Purpose

解决 AiluEngine 一次「启动 → 正常运行 → 退出」过程中日志量大、信息密度低的问题，同时补足 `format_str` 对 C++20 标准格式说明符（如 `{:08X}`、`{:.2f}`）的支持。

目标不是减少全部日志，而是将**正常摘要**与**按需诊断**分离，让 `WARNING/ERROR` 代表真实问题，并且避免关闭的调试日志仍消耗格式化和同步 IO 开销。

## Scope

1. **格式化兼容性：**保持现有窄/宽字符串 `format_str` 调用方式，支持标准 `std::format` 语法；处理已有自定义类型参数的编译兼容与格式化异常。
2. **日志基础设施：**在保留 `LogMgr + IAppender` 的前提下增加 `DEBUG`、`TRACE`；在格式化前过滤；修复已发现的初始化问题；串行化多线程输出；复用文件句柄。
3. **调用点治理：**优先处理 ResourceMgr、Shader/ComputeShader、AssetHandlers、PSO、Descriptor/ResourcePool、TimerBlock、DX12 与 Input/Editor 中的高噪声调用。
4. **摘要输出：**合并可直接获取计数的加载、编译、缓存信息；在 INFO 中保留重要启动/关闭、项目与功能事件。
5. **轻量模块过滤与验证：**仅为高噪声模块加入简单可选分类过滤，覆盖「启动→运行→退出」回归与负载测试。

## Non-goals

- 不引入 spdlog、fmt 等第三方日志库；不重写整个引擎诊断/Profiler/Tracy 体系。
- 不做异步日志队列、无锁环形缓冲、复杂配置中心、远端日志上传、结构化遥测平台或新 Editor Log UI。
- 不批量删除日志、不通过全局提升阈值掩盖警告/错误、不无条件去重本来不同的错误。
- 不为所有数学/引擎类型一次性添加 `std::formatter`；只解决现有日志调用确实需要的类型。
- 不顺带重构资源生命周期、Shader 缓存、PSO 或 GPU 状态机；发现真实业务错误单独记录并处理。

## Architecture Constraints

- 兼容现有 `LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` / `LOG_FATAL`、`AE_LOG` 和 `LogMgr::LogMsg` 使用方式；新功能优先增量扩展，避免跨模块大规模修改。
- 保留现有 `IAppender` 输出流程、`LogMessage` 源位置/线程信息；保持启动与销毁时序合法；`FATAL` 输出不得因过滤失效。
- 留意现有 `ELogLevel` 枚举数值、日志颜色数组与可能的 ABI 依赖：**不要仅插入枚举项却继续用 `level` 直接索引旧颜色数组**。优先保留旧级别数值或明确逐处检查兼容性，采用显式级别映射。
- `TraceLevle / SetTraceLevel` 是现有旧接口，不能与新增 `ELogLevel::kTrace` 混为一谈；避免无目的 API 重命名。
- 正式输出必须安全地支持多线程；不允许持有日志互斥锁时递归记录日志。
- 先判断是否输出，再构造/格式化 `LogMessage`；注意只在 `LogMsg()` 内检查不会阻止**调用点参数表达式**求值，必要时由宏入口短路。
- 已有代码命名/ABI 优先保持兼容；新增变量用小写下划线，成员以 `_` 开头，静态变量以 `s` 开头，常量以 `k` 开头加驼峰，新增类/函数用驼峰；代码尽量遵循 160 列。
- 禁止为了降低日志数量改变正常行为，或将真正的可恢复故障一律降级为 DEBUG。

## Baseline

### 代码现状

- `Engine/Inc/Framework/Common/Log.h`：`format_str` 通过自定义 `format_helper + ostringstream` 替换 `{}`，**不支持** `{:08X}` / 浮点精度 / 对齐等标准说明符；仅有 Info/Warning/Error/Fatal 四级。
- `Engine/Src/Framework/Common/Log.cpp`：`LogMsgImpl` 才检查级别；输出 Appender 的调用未统一串行化；`FileAppender::Print` 每条日志打开/关闭文件；默认构造函数 `_output_mark = _output_mark` 为自赋值错误。
- `Engine/Src/Framework/Common/ResourceMgr.cpp`：资源加载成功打印 `LOG_WARNING`，属于错误分级。
- `Engine/Src/Render/Shader.cpp` 与 `Engine/Src/RHI/DX12/D3DShader.cpp`：Shader Variant 成功可能被上层和 RHI 层重复打印；RHI 的部分路径在 `succeed == false` 时仍无条件输出 `READY`。
- `Engine/Src/Assets/AssetHandlers.cpp`：Texture artifact 命中与详细耗时等信息重复记录在 INFO。
- `Engine/Src/RHI/DX12/DescriptorManager.cpp`、`Engine/Inc/Render/ResourcePool.h`、`Engine/Src/Render/GraphicsPipelineStateObject.cpp`：正常回收/扩容/初始化等存在 WARNING 滥用。
- `Engine/Inc/Framework/Common/TimeMgr.h`：`TimerBlock` 析构时不论耗时长短均使用 `LOG_INFO`。

### 已提交日志样本（只作为问题定位，不作为当前版本性能结论）

- `FileAppender_log.txt`（2026-09-28）：**769 行**，其中匹配到 INFO 579、WARNING 167、ERROR 7，其余 16 行为不完整/交织内容。`ResourceMgr::Load` 成功 WARNING 119 次；图形 Shader 上下层 `READY` 分别 95 / 97 次；Compute Shader 71 次；Texture artifact 两个位置各 35 次。
- `Tools/AiluMcpServer/FileAppender_log.txt`（2026-08-06）：**1537 行**，其中 ResourceStateTracker 旧实现曾有大量 INFO/WARNING；相关调用在当前源码中已经收敛，**不能当作当前代码仍有该问题的证据**。
- 注意两份样本并非可复现的完整当前版本生命周期记录，也无法据此直接给出性能损耗结论；实现时必须补新的对照采样。

## Target Outcome

正常默认 INFO 输出中，只看到：项目/后端/设备摘要、核心系统初始化结果、Shader/Asset 加载统计、关键用户操作、重大状态变化、真实 WARNING/ERROR 以及简短关闭摘要。

例如（**示意而非实际数据**）：

```text
[INFO] AiluEngine | DX12 | Debug
[INFO] Project opened: AiluTestProj
[INFO] GPU: NVIDIA GeForce RTX xxxx
[INFO] Assets initialized: 132 assets, 38ms
[INFO] Shaders ready: 168 variants, cache_hits=151, failures=0, 542ms
[INFO] Editor initialized: 1287ms
```

同时满足：

- `LOG_DEBUG` / `LOG_TRACE` 可选择性开启；默认不会大量输出逐资产、逐变体、逐资源的成功细节。
- `LOG_WARNING` 仅报告值得关注的异常或明确风险；不再报告正常的 load success、资源释放、缓存创建等。
- `format_str("0x{:08X}", 0x80070005u)` 得到 `0x80070005`；宽字符串版等价可用。
- 多线程日志不出现跨条目拼接；被过滤的日志不做消息格式化，尽量避免参数求值。
- 源位置与行号可追溯，错误现场信息没有因降噪丢失。

## Relevant Files

| 文件 | 关注点 |
|---|---|
| `Engine/Inc/Framework/Common/Log.h` | 格式化函数、级别与宏、早期过滤、Appender 声明 |
| `Engine/Src/Framework/Common/Log.cpp` | 过滤、同步、初始化、输出与关闭 |
| `Engine/Inc/Framework/Common/TimeMgr.h` | TimerBlock 的日志等级/耗时阈值 |
| `Engine/Src/Framework/Common/ResourceMgr.cpp` | 加载成功 WARNING、逐资源日志 |
| `Engine/Src/Render/Shader.cpp` | 上层编译结果日志 |
| `Engine/Src/RHI/DX12/D3DShader.cpp` | 图形/计算 Shader 缓存和失败状态日志 |
| `Engine/Src/Assets/AssetHandlers.cpp` | Texture/Mesh/Animation artifact 诊断噪声 |
| `Engine/Src/Render/GraphicsPipelineStateObject.cpp` | PSO 初始化/缓存/创建失败 |
| `Engine/Src/Render/Material.cpp`、`Engine/Src/Render/Features/CommonPasses.cpp` | 无效 Shader 的失败传播与绘制跳过 |
| `Engine/Src/RHI/DX12/D3DGraphicsPipelineState.cpp` | PSO 正常销毁 |
| `Engine/Src/RHI/DX12/DescriptorManager.cpp` | 正常描述符回收与扩容 |
| `Engine/Inc/Render/ResourcePool.h` | 容量增长与清理日志 |
| `Engine/Src/RHI/DX12/D3DContext.cpp` | Adapter 详细枚举与周期性清理 |
| `Engine/Src/Render/Texture.cpp` | RT 池正常回收 |
| `Engine/Src/Objects/Type.cpp` | 懒注册时的误报 WARNING |
| `Engine/Src/Input/InputSystem.cpp` | Debug dump 与普通生命周期 |
| `Editor/Src/EditorApp.cpp`、`Engine/Src/Framework/Common/Application.cpp` | 主要生命周期信息与初始化耗时 |
| `Test/`（按现有构建组织） | 格式化、过滤、并发与日志回归测试 |

实现前根据最新工作树确认路径和调用行为；日志中的历史行号不要机械套用到新代码。

## Milestones

### M1 — 格式化与日志基础设施（必须先通过）

**目标：**支持标准格式说明符；添加 Debug/Trace；修复过滤与输出层的正确性问题；保证全项目仍可编译运行。此时**不大规模迁移调用点**。

**实施步骤**

1. **迁移 `format_str`：**保留现有窄/宽字符串重载、名称、参数传递方式，将手写 `{}` 替换器改为 `std::vformat + std::make_format_args / std::make_wformat_args`。移除不再需要的 `format_helper`。参考实现的参数在 `format_str` 内是具名左值，确保 `make_format_args` 使用方式符合实际 MSVC 标准库要求。
2. **解决现有类型兼容：**编译所有调用点，针对已有 `operator<<` 但无 `std::formatter` 的自定义参数，优先在具体调用点显式转换 `ToString()` / `ToChar()` / `ToWChar()`；仅在大量合法复用时添加最小的 formatter 适配。不允许以不安全泛型模板吞掉格式错误。
3. **处理格式异常：**对日志入口中的 `std::format_error` 设计明确的无递归兜底（保留原格式和错误提示）；不应让单条日志格式错误直接终止 Editor。若底层通用 `format_str` 原本是独立格式化 API，其异常语义可以维持标准行为，兜底只设置在 Log 入口。
4. **扩展级别：**支持 Trace / Debug / Info / Warning / Error / Fatal；保留 `LOG_INFO` 等旧宏，新建 `LOG_DEBUG`、`LOG_TRACE`。更新 `LogLevelToString`、ANSI 颜色映射、默认阈值以及所有依赖级别枚举顺序的逻辑。默认阈值保持 `INFO`。
5. **提前过滤：**在 `LogMsg` 格式化前进行判断，并让新增 Debug/Trace 宏能在未启用时短路昂贵参数表达式；检查旧宏在无分号、`if/else` 等使用方式下是否保持兼容。必要时迁移到安全的宏语义并修复编译点，不能引入悬空 `else`。
6. **同步输出：**保证多线程对 Appender 的整条消息输出串行化；检查 `AddAppender` / `Finalize` 的生命周期。`FileAppender` 持有一个打开的文件句柄，不再逐条 open/close；在退出、严重错误等必要时 flush。**暂不引入异步队列。**
7. **修复现有问题：**`LogMgr` 默认构造中的 `_output_mark = _output_mark`；级别过滤配置的并发安全；初始化和 Shutdown 次序下的空实例与悬空指针风险（只做必要的防护，不扩大生命周期重构）。

**测试与 Acceptance Criteria**

- [x] 窄/宽字符串支持 `{}`、`{:08X}`、`{:016X}`、`{:.2f}`、`{:<10}`、`{:>10}`、`{{` / `}}`；普通数字、字符串和原有用例行为符合 C++20 标准。
- [x] 已有引擎日志调用编译通过；不因自定义类型不支持 `std::formatter` 导致大面积编译失败；处理方案有明确测试。
- [x] `format_str` 使用标准无效说明符时行为明确；日志级别的格式化错误不会造成递归日志或进程退出。
- [x] 调低/调高阈值时，新增 Debug/Trace 按预期可见/隐藏；被过滤的消息没有进入实际格式化与 Appender；错误和 Fatal 正常可见。
- [x] 多线程并发写入测试（例如 8 线程 × 1000 条）记录完整、不交织、不丢失；输出可独立解析。
- [x] 文件仍是单次启动清空并持续追加的预期行为，生命周期结束后完整落盘；`LogMgr::Shutdown` 不留下持有文件的句柄。
- [x] Editor 既有编译目标、`LogSmoke` 与无 GPU Editor batch 生命周期通过；Player target 当前未由根 CMake 启用，保留为环境限制。

### M2 — 调用点分级与摘要治理

**目标：**保留真正有价值的错误和主要事件，最大幅度减少日常 INFO/WARNING 噪声；优先做定位明确、收益高的修改。

**优先清单（按顺序处理）**

1. `ResourceMgr::Load`：将成功加载的 `LOG_WARNING` 改为 DEBUG，失败继续 ERROR；重载成功非异常情况下不再报 WARNING。必要的汇总计数在阶段/批量加载完成时由实际拥有生命周期的调用方打印 INFO，避免每资源汇总。
2. `D3DShader::RHICompileImpl`、`D3DComputeShader::RHICompileImpl` 与上层 `Shader::Compile`：逐 Variant `READY` 只留 DEBUG 的唯一可信位置；INFO 输出批量完成统计（变体数、缓存命中/编译、失败数、总耗时，优先复用 `ShaderArtifactCache::Stats()`）；修复 `succeed == false` 仍打印 `READY` 的分支，确保成功/失败输出与返回值一致。
3. `AssetHandlers.cpp`：Texture/Mesh/Animation 的单资产缓存命中、artifact 路径、细粒度耗时转 DEBUG；同一操作避免“命中一次 + 详情一次”双重输出。缓存损坏而回退重建、无法读取/存储等**真正异常**保留 WARNING/ERROR。
4. `GraphicsPipelineStateObject.cpp` / `D3DGraphicsPipelineState.cpp`：PSO 缓存建库、添加、正常销毁降为 DEBUG；初始化成功可汇总 INFO；`PSO creation failed` 仍 WARNING/ERROR，不能通过过滤掩盖真实故障。
5. `DescriptorManager.cpp`、`ResourcePool.h`、`Texture.cpp`、`D3DContext.cpp`：正常释放、清理、池扩容降 DEBUG/TRACE，必要时用真正的容量阈值报告 WARNING；周期性资源清理不再充当 WARNING。异常资源泄漏、设备移除、调用失败仍保留对应级别。
6. `TimeMgr.h::TimerBlock`：短耗时记录不进入默认 INFO（优先 TRACE/Profiler）；可选仅超过显式阈值时输出 DEBUG/INFO，且不得随意影响计时行为。
7. `Type::Find`：如果未注册但能合法地完成惰性注册，不能输出 WARNING；确实不存在/失败时保留有用的 ERROR。`InputSystem` 的调试 dump 应是显式请求或 DEBUG/TRACE，不默认刷 INFO。
8. `D3DContext` 的 Adapter/显存详细枚举从十余条 INFO 合并为单条必要摘要；详细内容保留 DEBUG。`EditorApp` / `Application` 保留初始化完成、项目、退出等重要事件；一般控件操作、文件变更细节和测试性事件适当降级。
9. `ImageCapture` / 自动化测试过程：区分用户可见的“任务开始/结束/失败”与每帧/每张纹理详细信息；测试失败必须仍提供足够上下文定位。

**注意：**修改前判断日志是否实际表达异常，不可按文本匹配批量把 WARNING 全改为 DEBUG；例如 `Type::Find` 的惰性注册警告、常规池扩容与真实内存耗尽是不同情况。

**测试与 Acceptance Criteria**

- [x] 同项目、同场景、同缓存状态，完成至少一轮完整「启动→固定时长运行→退出」对照采样；legacy 高噪声 replay 与最终优化版本均使用同一 Gray Card fixture 快照，均 exit 0。
- [x] 正常资源加载、Shader 编译成功、PSO 建立/销毁、Descriptor 回收、Type 惰性注册不再使用 WARNING；静态检查与有效 cold/hot 日志均未发现这些成功模板，仍保留真实 UV/Cubemap/allocator 风险 WARNING。
- [x] 编译失败/资源加载失败/Shader 变体不 Ready 时，日志级别、信息与真实状态一致；资源失败、PSO 空上下文失败和损坏 Shader 均记录明确 ERROR/WARNING，损坏 Shader Editor fixture exit 0，且原文件哈希恢复。
- [x] 默认 INFO 不再为每个 Shader Variant、每个缓存命中、每次资源池回收等高频正常操作输出明细；有效 cold log 只有批量 `Compiled 131 shaders` 摘要。
- [x] Debug 可恢复足够的逐对象诊断信息；模块过滤回归证明 Shader Debug 可单独开启，原有 ERROR/FATAL API 保留。
- [x] 至少对正常冷启动和缓存热启动各验证一次；最终当前源码 cold/hot 均 exit 0，性能改善相对代码前 baseline 仍单独保持未验收。

### M3 — 最小模块过滤与端到端验收

**目标：**在不改造所有调用点的前提下，支持常用子系统单独排查，并定量验收 CPU、IO 和信息密度。

**实施步骤**

1. 增加**可选、轻量**的分类标签或模块级过滤（例如 `Asset / Shader / RHI / Editor`）；已有不带分类的宏视为 `General`，保证兼容。只对重点调用点显式打标签；不要引入复杂注册表、每模块 Logger 对象或专用配置文件。
2. 支持运行时选择全局日志级别和重点模块的 Debug/Trace（可复用 `LogMgr` API；若配置入口代价大，测试中用编程接口即可，不强制做 CLI/UI）。默认仍为 INFO。
3. 设计一份轻量日志审计流程：读取本次生成的日志，统计 level、调用点、重复模板和每秒输出量；在 Debug/Trace 关闭/开启两种模式复测。
4. 对比日志关闭与开启时的格式化/写入成本。可用简单基准测试或 Profiler 验证：被过滤日志几乎不产生格式化与磁盘写入；文件句柄不反复创建；并发写入稳定。区分真实优化收益与不同场景导致的样本差异。
5. 补充回归：资源缺失、Shader 编译失败、PSO 创建失败（采用隔离测试或可恢复的可控故障），保证它们仍被记录；真正的 DX12 Debug Layer WARNING 不能因为降噪消失。

**测试与 Acceptance Criteria**

- [x] 能只打开 Shader 或 Asset 的 DEBUG/TRACE，而不是不得不暴露全引擎所有诊断信息。
- [x] 所有旧日志 API 无分类参数也能正常编译和输出；默认级别与输出行为稳定。
- [x] 重新运行完整 Editor 生命周期，最终当前源码 cold/hot 均 exit 0；`LogSmoke` 8×1000 并发逐行校验通过，且文件在 `Finalize` 后可立即读取，证明无交织并完成 flush/close。
- [x] 对相同场景、相同缓存、相同运行时长的前后样本提供统计表（总行数、INFO/WARNING/ERROR、前 10 个热点、初始化耗时/相关 CPU 开销）。
- [x] 优先目标：**默认运行日志行数相对同条件基线减少至少 50%**；legacy replay 1104 行，最终优化版本 366 行，减少 66.85%；剩余 WARNING/ERROR 均为真实 Streamline、allocator、UV/Cubemap 或资源状态事件。
- [x] 所有新增测试通过；最终 `LogSmoke` PASS，build-debug 无新增编译错误，日志格式/并发回归、PSO 空上下文和损坏 Shader 恢复回归通过。

## Current Milestone

**M3 — 最小模块过滤与端到端验收（已完成）。**

M1 已完成源码、回归测试、并发输出和无 GPU Editor 生命周期验收；M2 第一批调用点治理、有效资源失败注入、PSO 空上下文失败回归和 Shader 损坏可恢复传播均已完成。M3 已使用同一 Gray Card fixture 快照完成 legacy 高噪声 replay 与优化版本对照，日志行数减少 66.85%，模块过滤、并发、flush/close 和 Editor 生命周期均通过。

## Progress

- [x] M1：格式化升级、级别、过滤、线程与文件输出、安全回归
- [x] M2：高噪声调用点、成功/失败语义与摘要输出（实现与失败传播回归完成）
- [x] M3：模块过滤、端到端对照和验收

### M1 Progress

- 已将 `format_str` 迁移到 `std::vformat` / `std::make_format_args` 与宽字符串等价实现；窄/宽格式异常在日志入口无递归兜底。
- 保留 `Info/Warning/Error/Fatal` 的旧枚举数值，新增 `Trace/Debug`，显式映射 ANSI 颜色；日志宏在格式化前短路，兼容历史无分号调用。
- `LogMgr` 阈值使用原子值，Appender 输出串行化；`FileAppender` 复用打开句柄并在 Error/Fatal/析构时 flush/close。
- 对 `Path`、FBX 字符串、资源指针等少量不满足标准 formatter 的调用点做显式字符串/地址转换；为 `Vector2D/3D/4D` 添加只接受 `{}` 的最小 formatter。
- 新增 `Test/LogSmoke.cpp` 与 `LogSmoke` CMake 目标，覆盖标准格式、窄/宽字符串、转义、非法格式、宏参数短路、Debug 开关和 8×1000 并发文件输出。

### M2 Progress

- `ResourceMgr::Load` 正常成功/重载改为 Asset 模块 DEBUG；失败仍为 ERROR。
- 图形/Compute/RayTracing Shader 成功 READY 与上层成功 READY 改为 Shader 模块 DEBUG；失败不再伪报 READY，上层失败仍 ERROR。
- Texture/Mesh/Animation artifact 命中与详情、PSO 正常缓存建库/添加/销毁、Descriptor/ResourcePool/RT pool 正常回收、TimerBlock、Input debug dump、Type 惰性注册和 DX12 正常清理已降为 DEBUG/TRACE；真实故障未降级。
- 有效 M3 Gray Card hidden-compat fixture 的 cold/hot batch 均 exit 0：cold 349 行（INFO 143、WARNING 103、ERROR 1），hot 366 行（INFO 143、WARNING 120、ERROR 1）。唯一 ERROR 是 Streamline 在当前硬件上的 DLSS context 校验；WARNING 主要是既有 allocator leak、UV 多通道限制、Cubemap mipmap 限制和 hot 路径 `MinMaxHeight` 未 ready。
- 已完成可控资源失败注入：material 文件替换后仍记录 `ResourceMgr::Load ... mismatched asset type` 和 `Load material slot 0 failed`，进程 exit 0；文件随后哈希恢复。
- 已完成 Shader 失败注入：parser 无 pass、Compile、PSO cache、Material Construct 和 RenderGraph 绘制路径均输出明确 ERROR/跳过无效绘制；最终损坏 Shader fixture exit 0，原文件 SHA256 恢复。
- 已补充可控 PSO 失败回归：`CommandRecordingContext` 未配置 Shader 时只输出一次 WARNING 并返回 `nullptr`，`LogSmoke` 覆盖该路径。
- 已完成同 fixture legacy replay 对照：legacy 1104 行（INFO 881、WARNING 120、ERROR 1），优化 366 行（INFO 143、WARNING 120、ERROR 1），减少 66.85%；最终优化 Editor exit 0。

### M3 Preparation

- 新增 `ELogModule`、`SetModuleDiagnostics` 与 `LOG_MODULE_DEBUG/TRACE`，支持仅打开 Asset/Shader/RHI/Editor 的 DEBUG/TRACE；旧 API 默认 General 且默认 INFO 不变。
- M2 重点 Asset/Shader/RHI 调用点已接入模块；`LogSmoke` 覆盖模块开启/关闭短路。
- `LogSmoke` 模块审计：5000 条 Shader DEBUG 在模块关闭时到达 Appender 0 条、约 6.8 ms；开启后到达 5000 条、约 245.6 ms，证明关闭路径跳过格式化与 Appender。
- 最终复跑模块审计：关闭模块到达 Appender 0 条、`filtered_us=6119`；开启后到达 5000 条、`enabled_us=158194`；`LogSmoke: PASS`。

执行过程中用简短条目记录：当前变更、测试命令/结果、已知失败、下一步；不要仅把任务勾选为完成而缺少证据。

## Decision Log

| 决定 | 原因 |
|---|---|
| 保留 `LogMgr + IAppender` | 已有体系足以满足当前需求，降低迁移风险 |
| 使用 `std::vformat` 替代手写格式解析 | 标准 C++20 功能完整，直接解决 `{:08X}` 等说明符 |
| Debug/Trace 承载逐对象诊断，INFO 只承载摘要 | 提升默认日志信噪比 |
| 优先修正错误等级/错误 READY 信息 | 日志正确性高于行数指标 |
| 文件输出先复用句柄 + 加锁，不做异步重构 | 复杂度小，足以处理当前瓶颈与交织风险 |
| 模块过滤保持可选/兼容旧宏 | 不因观察性改造波及全部子系统 |
| 每个 Milestone 必须独立通过 | 防止兼容性问题被后续批量日志改动掩盖 |
| 2026-10-19：先完成 M1 再进入 M2 | `LogSmoke`、完整 Debug 构建和无 GPU Editor 生命周期已通过；图形 fixture 的场景断言单独记录，避免把环境输入问题混入日志基础设施结论 |
| 2026-10-19：自定义类型采用显式转换与少量 formatter | 标准 formatter 拒绝 `Path`/引擎对象指针；不引入无约束 ostream 泛型，保留格式错误可见性 |
| 2026-10-19：模块过滤采用原子 bit mask | 只增加 Asset/Shader/RHI/Editor 的 Debug/Trace opt-in，不引入 Logger 对象或配置中心 |
| 2026-10-19：损坏 Shader 采用“记录错误并跳过依赖路径” | parser、PSO cache、Material 和 RenderGraph 绘制均不能继续消费零 pass Shader；保持 Editor 可恢复退出，不把异常静音 |
| 2026-10-19：legacy 对照采用临时 log-level replay | 保持当前 dirty 工作树、场景和缓存输入不变，仅恢复本次新增 DEBUG/TRACE 的默认可见行为，取得可复现的 before/after 行数证据；replay 文件不进入产品代码 |

后续遇到设计取舍，继续在此追加**日期 / 决定 / 原因 / 影响文件**。

## Surprises & Discoveries

- 已提交的 2026-09-28 日志中，`ResourceMgr::Load` 的 119 条资源加载成功日志使用 WARNING；可优先整改。
- `Shader::Compile` 与 D3D Shader RHI 层分别报告图形 Variant READY，容易形成重复输出。
- D3D Shader 的部分失败路径也可能打印 READY，属诊断准确性 bug，需要实质修复。
- `FileAppender` 每条日志打开关闭文件，且已提交日志样本存在行拼接/不完整片段；同步性必须验证。
- 2026-08-06 样本的 ResourceStateTracker 大量日志，在当前源码里已明显减少；不能拿旧样本推断当前热点。
- 若新版 `std::vformat` 暴露了原有非法格式串或不支持的自定义类型，记录具体调用点、最小兼容调整与测试结果，不应掩盖成静默格式化成功。
- M1 编译使用当前 preset 的 `F:\AiluEngine` 生成树；该 preset 与当前 checkout 源码一致但不在仓库目录，受限环境首次运行因生成目录权限失败，授权后完整构建通过。
- 共享 `Editor/EditorConfig.json` 的 `_scene_path` 为空；普通 `--exit-after-frames` fixture 因 `SceneMgr::OpenScene` 空路径断言退出，改用 `--batch --list-tests --graphics none --scene ...` 完成初始化/退出证据。
- 当前根 `CMakeLists.txt` 未启用 `Player` 子目录，因此不存在可构建的 Player target；M1 已验证 Engine、BlenderLiveLink、Editor 与 `LogSmoke`，Player 编译证据留为环境限制。

## Validation

**实现前记录**：Git commit / 配置（Debug 或 Release）、启动场景、是否热缓存、固定运行时长、默认日志级别、对照日志路径。

**M1 实际记录**：工作树含用户已有 dirty changes；使用 Debug preset（生成目录 `F:\AiluEngine\build\debug`，输出 `F:\AiluEngine\artifacts\out`），默认阈值 INFO。`LogSmoke` 在测试输出目录运行，8 线程 × 1000 条；独立 Editor batch fixture 使用 `--batch --list-tests --graphics none`，退出码 0，日志包含 Application/Editor 初始化与 shutdown 标记。普通图形 10 帧 fixture 因输入场景/共享 EditorConfig 限制退出码非 0，未将其冒充为 PASS。

**M1**：编译 Editor / Player 与相关 Tests；运行窄/宽格式化、转义、非法格式串、自定义类型、级别过滤、多线程 8×1000 条、文件关闭刷盘测试。

**M2**：分别进行冷/热缓存生命周期；记录日志级别统计；注入可恢复的资源与 Shader 失败案例；验证所有失败仍可被发现且输出准确。

**M2 当前证据**：有效 Gray Card hidden-compat fixture cold/hot 均 exit 0；cold 349 行（INFO 143、WARNING 103、ERROR 1），hot 366 行（INFO 143、WARNING 120、ERROR 1）。资源损坏注入记录 ResourceMgr mismatch 与 material-slot failure；PSO 空上下文回归返回 `nullptr` 并只记录一次 WARNING；Shader 损坏注入记录 parser/compile/PSO/material ERROR，最终 fixture exit 0。

**最终当前源码证据**：`Temp/log_final_audit_20261019/optimized_original_final/FileAppender_log.txt` hot 366 行、79367 bytes（INFO 143、WARNING 120、ERROR 1；`Application Initialize` 0.0521866 s；`Compiled 136 shaders!`），Editor exit 0，DEBUG/TRACE 均为 0；唯一 ERROR 是当前硬件不支持 DLSS 的 Streamline context 校验。此前 cold 证据 `final_valid` 为 349 行、INFO 143、WARNING 103、ERROR 1。

**同条件 legacy 对照**：临时 legacy log-level replay 使用相同 Gray Card fixture 快照、相同 batch 命令和 Debug 构建，仅将本次新增 DEBUG/TRACE 调用恢复为默认可见的 INFO 行为；`Temp/log_final_audit_20261019/legacy_run/FileAppender_log.txt` 为 1104 行、282379 bytes（INFO 881、WARNING 120、ERROR 1；`Application Initialize` 5.269619 s；`Compiled 136 shaders!`）。优化版本 366 行，行数减少 `66.85%`。

详细日志证据保留在 `Temp/valid_log_audit_20261019/{cold.log,hot.log,invalid_asset.log,invalid_shader.log}`、`Temp/log_final_audit_20261019/{legacy_run,optimized_original_final,invalid_shader_final}`；fixture 原始 material、shader 和 editor session 均已哈希恢复。

**最终构建/回归**：`cmake --build --preset build-debug -- /m:1` 通过 Engine、BlenderLiveLink、Editor；`LogSmoke` 最终通过，模块审计 `filtered_us=6119`、`enabled_us=158194`、5000 messages；损坏 Shader fixture exit 0 且源文件 SHA256 恢复。Player target 仍未由根 CMake 启用。

有效 fixture 统计补充：cold/hot `Application::Initialize` 分别 63.6/41.4 ms，`EditorApp::Initialize` 分别 181.8/90.2 ms；主要热点为 Streamline info 88 条、allocator leak 64 条、Mesh::SetUVs 18 条，hot 另有 GpuResource::Bind MinMaxHeight 17 条。它们是当前真实事件/环境噪声，不能通过静音异常伪造 50% 改善。

**M3**：切换日志阈值/分类并重复；比较输出条数、重复来源、CPU/文件 IO、线程交织情况；生成简要验收报告（可以附在本文末尾，不另建大型文档）。

若项目测试设施缺失，优先在既有 `Test/` 增加最小自测，不为日志专项建立新测试框架。无法在当前环境启动 Editor 时，必须准确写明哪些验收尚未完成，不能默认通过。

## Known Follow-ups

- 如后续验证发现日志 IO 是明显 CPU 瓶颈，再评估异步日志/批量 flush，而非本次预先引入。
- 如长期开启 Trace 产生大量数据，可后续引入独立诊断 sink、轮转文件与大小限制。
- 如 Editor 控制台确有独立等级/筛选需求，单独设计 UI；本次不扩展 Editor 功能面。
- 如果真实的 GPU 资源状态、PSO 或 Shader 编译缺陷仍持续制造 WARNING/ERROR，应各自建独立修复任务，**不得用过滤规则替代修复**。
- 已提交的 `FileAppender_log.txt` 是否应该停止版本追踪、加入 `.gitignore`，可在完成后单独处理，不混入日志行为修改。

## Outcomes

### M1 — 已完成

- 改动：`Engine/Inc/Framework/Common/Log.h`、`Engine/Src/Framework/Common/Log.cpp`；新增 `Vector2D/3D/4D` formatter；修复 6 个现有不满足标准 formatter 的调用点；新增 `Test/LogSmoke.cpp` 与 CMake target。
- 构建：`cmake --build --preset build-debug -- /m:1` 通过，生成 Engine、BlenderLiveLink、Editor；`LogSmoke` target 通过。
- 回归：`LogSmoke: PASS`；标准窄/宽格式、转义、非法格式异常、过滤参数短路、Debug 开关、8×1000 并发文件输出通过。
- 生命周期：隐藏 Editor batch/list-tests fixture 退出码 0；无项目早期错误退出码 1 且独立文件含 `No project file specified.`；普通图形 fixture 的场景断言未通过，原因与日志基础设施分离。
- 代码检查：`git diff --check` 无 whitespace error；输出 warning 仅为工作树中既有 LF/CRLF 提示。
- 剩余限制：Player target 未由根 CMake 启用；M2/M3 的可构建 Editor、fixture、失败传播和日志对照证据由后续里程碑补齐。
- Gate：M1 基础设施与可独立运行验证通过，进入 M2；图形 fixture 和 Player 缺失证据保留为限制。

### M2 — 已完成

- 改动：ResourceMgr、D3D/Render/RayTracing Shader、AssetHandlers、PSO、Descriptor/ResourcePool、Texture、D3DContext、Type、Input、TimerBlock 调用点分级；成功状态不再用 WARNING/伪 READY 表达。
- 构建/回归：最终 `cmake --build --preset build-debug -- /m:1` 通过；`LogSmoke: PASS`。
- 量化：有效 Gray Card cold/hot 分别为 349/366 行；默认 INFO 不再输出逐 Variant READY、逐缓存命中和资源池回收明细，保留批量 `Compiled N shaders!` 摘要。
- 失败语义：资源类型错误 exit 0 且有 ERROR/WARNING 上下文；PSO 空 Shader 返回 `nullptr` 并只记录一次 WARNING；损坏 Shader 的 parser/compile/PSO/material/draw 链记录 ERROR 并安全跳过，Editor fixture exit 0。
- M2 gate：调用点分级、摘要、冷/热生命周期与失败语义均通过；Player target 未由根 CMake 启用，保留为环境限制。

### M3 — 已完成

- 对照：同一 Gray Card fixture 快照、同一 batch 命令和 Debug 构建，legacy log-level replay 1104 行（INFO 881、WARNING 120、ERROR 1），优化版本 366 行（INFO 143、WARNING 120、ERROR 1），减少 66.85%。
- 运行时：legacy 与优化 Editor 均 exit 0；优化版 cold/hot 证据、文件 flush/close、8×1000 并发逐行完整性均通过。
- 模块过滤：Shader/Asset 可独立开启；最终 `LogSmoke` 输出 `filtered_us=6119 enabled_us=158194 messages=5000`，过滤路径无 Appender 消息。
- Gate：M3 全部 Acceptance Criteria 通过；本 ExecPlan 可移入 `Doc/exec-plans/completed/`。

## Resume Protocol

下次由 Codex/Claude 接手时：

1. 阅读本文 `Current Milestone`、`Progress`、`Decision Log`、`Validation`、`Outcomes`，只展开当前 Milestone 所需实现文件。
2. 检查 git 状态与最近提交；不得覆盖用户或其他 agent 的未提交修改。
3. 对照当前 Milestone 的 Acceptance Criteria 和已记录测试，从第一个未通过项目继续；不要跨阶段扩展范围。
4. 每次工作结束前更新 `Progress`、`Decision Log`（如有新决定）、`Surprises & Discoveries`（如有）、`Validation`、`Outcomes`，记录可复现的命令及下一步。
5. 只有当前 Milestone **全部通过**后，才修改 `Current Milestone` 为下一个。被环境阻塞时标记 `Blocked` 和缺失证据，不得虚报完成。

---

**完成定义：**默认日志显著更简洁但错误不失真；`format_str` 支持标准格式；关闭的调试日志不会产生无谓格式化/IO；跨线程文件输出完整；Editor/Player 生命周期及测试可复现通过。
