# M3 — CPU-only 无 GPU 执行模式

**状态**：Closed by user decision；当前主机实现与验收已完成，正式 gate 的剩余项转为后续使用中排查。**前置门槛**：M1 与 M2 均 Accepted（本轮按用户授权独立实现，但不伪造前置 gate）。**本阶段独立交付**：`--graphics none` 启动流程**不创建 Window、不枚举/创建设备、不注册 SwapChain**，在受支持的 Windows 主机完成资源元数据、场景文档、序列化及纯 CPU 算法测试。

## Purpose

让 AiluEngine 的非渲染自动化不再依赖 GPU，缩短测试启动时间、降低 CI 资源要求，并为资源数据库、Prefab、序列化、动画数据和插件元数据建立真正的 headless 验证能力。

## Scope

- Runtime 按需初始化：项目/路径/日志/反射/JobSystem/资源元数据库/脚本可选/场景文档能力，严格关闭图形上下文与 UI。
- ResourceMgr 分离 `Metadata/Database` 与 `GpuRuntimeResources` 初始化、Shutdown 和能力检查；资源 handler 可以只注册可用于 CPU 的功能。
- Test Registry 的 RequiredCapabilities 驱动启动配置；不支持的命令/测试明确跳过或拒绝。
- 有无 GPU 的资源/场景数据结果一致性验证、无设备机器或 DX12 被禁用环境的真实测试。
- CPU 模式严格尊重 Batch 的默认只读和工作副本隔离。

## Non-goals

- 不实现 CPU 光栅化器、Software RHI 或用 WARP 模拟真实 GPU；`graphics=none` 就是**完全不创建图形设备**。
- 不把所有材质 Shader 编译、Texture GPU 上传、GPU Importer 都硬塞进 CPU 路径；只能做无需 GPU 的解析/元数据校验，明确 capability 边界。
- 不在此重做资源状态管理、D3D12 后端或 AssetDatabase 格式。
- 不承诺 Lua/Script、物理、动画等全部模块无条件可启动；由测试所需能力声明并按依赖验证。

## Architecture Constraints

1. 为 Runtime 定义小而明确的能力集合（如 `kProject`, `kAssetDatabase`, `kCpuAssetDecode`, `kSceneData`, `kScript`, `kGpuRender`）；一个入口选择实际初始化集合。**不要**把上百个 feature flags 暴露为命令行。
2. 同一 ResourceMgr 接口应在需要 GPU 的操作上检查能力并报 `graphics_unavailable` 或显式 Unsupported，不允许解引用空 `GraphicsContext`；调用方不得仅凭对象类型就假设 GPU 实体已创建。
3. GPU 资源与 CPU asset metadata 的生命周期分开管理：含多个底层 D3D Resource 的资源在 none 模式**不创建任何子资源**；不能创建假的 D3D Object 满足字段非空。
4. `ResourceMgr::Initialize()` 的资源库装载、DerivedDataCache、handler 注册与 Shader/Texture/Mesh GPU 初始化分相；不复制第二套 AssetDatabase。
5. Shutdown 逆序且幂等，任何阶段 Initialize 失败都可清理已创建的部分。依赖全局单例的模块至少验证 `nullptr` / not initialized 行为。
6. `--graphics none` 禁止调用 `GraphicsContext::InitGlobalContext()` / `WindowFactory::Create()`；禁用 UI/音频等与 CPU 测试无关的外设初始化。
7. 遵循 M1 报告协议 schema v1，不为 CPU 模式另造一套测试报告。

## Baseline

- `EngineRuntime::Initialize(context, Window&, desc)` 首先创建图形上下文与 Window Backbuffer，然后 `ResourceSystem::Initialize()`、`ScriptSystem`、Audio、Gizmo、UI、Scene；`Shutdown` 目前依赖这些统一存在的前提。
- `ResourceMgr::Initialize()` 先初始化 `DerivedDataCache`、资产类型映射、AssetDomain、AssetDB、AssetHandler；随后 dispatch 预载 Shader/ComputeShader、创建 4×4 默认 `Texture2D`、Apply 到 GPU、加载贴图和 Mesh。
- `ResourceMgr` 的 `Load<Shader>`/`Load<Texture>`/`Load<Mesh>` 可能触发 GPU 构建；不能简单跳过全局图形初始化却运行原函数。
- `ProjectManager::OpenProject`、`ObjectRegister`、`Enum::InitTypeInfo` 已在 `Application::Initialize` 早期执行；目前后续强制 Window/Input。
- `Application::UpdateScenes` 始终包含 Script Tick 与 SceneMgr FixedUpdate/Update/LateUpdate，但 CPU-only 测试可能只需要文档加载和数据断言，不应自动开始完整场景仿真。

## Target Outcome

```powershell
# 预期命令：不依赖 DX12 图形设备的资源/场景文档测试
AiluEditor.exe "D:\Projects\Fixture.ailuproject" --batch --graphics none `
  --run-tests "Serialization.*" --report "D:\Reports\cpu\result.json" --timeout-seconds 120

# 模式错误时给出能力原因，不尝试创建 GPU
AiluEditor.exe "D:\Projects\Fixture.ailuproject" --batch --graphics none `
  --run-tests "Rendering.*" --report "D:\Reports\unsupported\result.json"
```

第二条应按照正式确定的能力策略，报告测试因 `gpu_required` 未运行并使整组请求无法通过门禁（不能出现 0 个测试被执行却 Exit Code 0）。

## Relevant Files

- 核心启动：`Engine/Inc/Framework/Common/Application.h`、`Engine/Src/Framework/Common/Application.cpp`、`Engine/Inc/Framework/Common/EngineRuntime.h`、`Engine/Src/Framework/Common/EngineRuntime.cpp`。
- 资源与 handler：`Engine/Inc/Framework/Common/ResourceMgr.h`、`Engine/Src/Framework/Common/ResourceMgr.cpp`、`Engine/Inc/Assets/AssetRegistry.h`、`Engine/Src/Assets/*`。
- 设备/资源对象：`Engine/Inc/Render/GraphicsContext.h`、`Engine/Inc/Render/Texture.h`、`Engine/Inc/Render/Shader.h`、`Engine/Inc/Render/Mesh.h`、`Engine/Src/Render/*`（仅对真实触发 GPU 的路径做限制）。
- Script / 场景：`Engine/Inc/Framework/Script/ScriptSystem.h`、`Engine/Src/Framework/Script/ScriptSystem.cpp`、`Engine/Src/Scene/*`。
- Automation：M1 的 Test Registry / Runner / Report、`Editor/Src/Automation/AutomationSceneService.cpp`、`AutomationAssetService.cpp`。

## Implementation

### Step 0 — 资源初始化依赖审计

- 在真实仓库画出 CPU-only 所需的最小资源图：Project → Asset Domains/DB → Asset Handles → Scene/Prefab serialization → (optional Script/Animation data)。
- 逐行审计 `ResourceMgr::Initialize()`，将操作标注为 `PureCPU`、`FileCacheWrites`、`GpuCreate/Upload`、`GpuShaderCompile`、`Mixed`；重点检查默认纹理、Shader 加载 dispatch、Mesh 注册、材质创建、编辑器 Font/UI 图元。
- 检查 DerivedDataCache 及 `RemoveOrphanArtifacts` 是否删文件：默认只读情况下不能修改用户正式 cache；必须允许 cache 临时根目录或只读加载。
- 记录 `Texture::s_p_default_white` 等静态默认资源、`Shader` 静态全局、Device 查询、反射与预览器的隐式使用点，明确切分边界。
- **先证明“哪些类型纯 CPU 可载入”，再设置 CPU-only 的测试范围**；不得一开始宣称所有 AssetHandler 都支持 CPU-only。

### Step 1 — 有限的 Runtime Capabilities

- 增加 `RuntimeCapabilities` 或等价 Options：最多数个宏观级别，避免细粒度 bool 参数爆炸。默认正常 Editor 仍为完整能力；`none` 只启 CPU 基础和所需的可选模块。
- `Application::Initialize` 允许跳过 WindowFactory/InputSystem/WinInputBackend/ImGui；保留 Project/ObjectRegister/Log/JobSystem 最小路径。
- `EngineRuntime::Initialize` 在没有 Window 的条件下可调用，独立管理已完成的每一步与 rollback；拆 `InitializeCoreServices`、`InitializeGraphicsServices`、`InitializeEditorServices` 或类似最小方法（不要为重构而拆过多抽象）。
- Runtime Shutdown 顺序应依据真实依赖图逆序，对 `ResourceMgr` / Scene / Script/Jobs/GPU 仅释放实际初始化过的对象。
- 在初始化/关闭边界增加轻量计数或日志，证明 none 模式 GPU device count = 0、窗口数 = 0，且没有隐藏的 `GraphicsContext::Get()`。

### Step 2 — ResourceMgr CPU 与 GPU 阶段分离

- 保留**一份** AssetRegistry/AssetDB、URI Resolve 和 AssetHandle 基础设施；将 CPU Metadata 初始化从 GPU Prefetch、Default Texture/Mesh、Shader Compilation 与 Texture Apply 中隔离。
- 按需要引入 `InitializeAssetDatabase` + `InitializeGpuRuntime` / `ReleaseGpuRuntime`（或同语义命名），应是幂等、可选择、遵守原依赖顺序的操作。
- `AssetHandler` 能力按实际功能分类：只读文档/元数据 handler 在 none 下可调用；需要 GPU 的 decode+upload 分开（先 CPU decode，后 GPU upload），或对不支持的路径明确 `unsupported_capability`。
- CPU-only 测试中 `GetAssetPath`、枚举类型、查询依赖、GUID 引用、Prefab/Scene document 解析应提供可信结果；`Shader::Create`、`Texture2D::Apply`、DX12 buffer readback 在 none 下禁止调用。
- 清理状态保证 no-GPU 运行不创建 GPU Fence/Texture/Descriptor；退出后无悬挂静态 `Texture::*`、`Shader::*` 指针，正常 GPU 模式仍能在新进程/回归下初始化。
- Asset 数据库及 DDC 的任何写入须进入沙盒副本；区分**验证临时构建的衍生产物**与不允许的用户项目修改。

### Step 3 — Scene/Prefab/Animation CPU 验证

- 在 CPU-only 路径只创建完成读档所必须的 Scene 和 Component 基础设施。若必须初始化 ScriptSystem，先确认它不会通过 `InputSystem` / UI / GPU 隐式强依赖；否则测试按能力禁用脚本执行。
- 建议首批测试：
  - `Serialization.SceneRoundTrip`：测试副本中 Save→Reload，关键实体 GUID、组件及父子层级一致；报告差异路径。
  - `Serialization.PrefabReferences`：检测丢失 GUID、循环引用（若规则不允许）、override 路径和类型。
  - `Assets.AssetDatabaseIntegrity`：资产 GUID 唯一、路径解析、引用完整性、Meta 数据可访问。
  - `Animation.ClipDataIntegrity`：Clip/skeleton 轨道的时间范围、采样顺序、引用完整性（不依赖 GPU Skinning）。
  - `Automation.CpuCapabilities`：GPU 方法在 none 下返回受控 Unsupported，未执行 GPU 命令。
- RoundTrip 使用独立测试副本，不能覆盖原始 Scene/Prefab；原始输入和输出内容 hash/结构差异写入 artifacts。
- 如 File Watcher、异步 DDC 或 AssetImport 在 CPU 模式仍工作，应声明其副作用和最大等待期限，不能在 Finalize 后继续改写项目。

### Step 4 — 资源生命周期和能力回归

- 调试构建添加断言/轻量 Instrumentation：`none` 下 `WindowFactory::Create`、`GraphicsContext::InitGlobalContext`、`RegisterWindow`、`Texture::Apply` 的**GPU 分支调用次数必须为零**。
- 不具备 D3D12 设备的机器/虚拟环境上实测 CPU 任务；不能靠强行卸载驱动破坏开发主机，使用适当的 CI Agent/隔离虚拟机。
- 再跑 M1/M2 图形回归，重点验证 ResourceMgr 阶段重排后默认白/黑/法线纹理、Shader 加载顺序、Camera/Material/RenderGraph 是否一致。
- 执行意外初始化失败/不完整项目/坏 Meta/资产数据库损坏/无可写缓存/脚本依赖失败等负面场景，并确保合理的报告与退出码。
- 连续执行 20 次并行两份独立 fixture；对比每次结果一致性、内存上界、文件副作用和无残留后台线程。

## Validation

| ID | 测试 | 预期 |
| --- | --- | --- |
| M3-V01 | `none` 模式启动后执行 `Automation.CpuCapabilities` | 0 Window、0 GPU Device、0 SwapChain、0 Present |
| M3-V02 | 无可用 D3D12 设备的环境运行 Scene/Asset DB 只读测试 | 正常完成并 Exit Code 0 |
| M3-V03 | Scene/Prefab RoundTrip（隔离副本） | 结构一致、预期字段不丢失、项目原始文件不变 |
| M3-V04 | 缺 GUID、重复 GUID、断裂引用或坏资产格式 | 按断言规则准确报告，不崩溃、不假通过 |
| M3-V05 | `--graphics none --run-tests "Rendering.*"` | 明确 GPU 能力缺失，整组验收非零且无 GPU 初始化 |
| M3-V06 | 模拟 ResourceMgr CPU 初始化中途失败 | 已初始化部分被完整释放，无 DDC 非预期副作用 |
| M3-V07 | 20 次顺序运行、两个隔离进程并行 | 报告稳定、不互相覆盖缓存/Session/用户项目 |
| M3-V08 | M1/M2 + 正常 Editor 全量回归 | GPU 默认纹理、Shader、RenderGraph、截图路径无功能回归 |

### Acceptance Criteria（本轮证据状态）

- [ ] A1：当前主机证明了 none 模式 0 Window/0 GPU context/0 SwapChain/0 Present；尚未在物理无 D3D12 设备主机复测。
- [~] A2：ResourceMgr CPU/GPU 初始化分离、GPU Load 受控拒绝、正常 shutdown 与三个阶段失败注入已验证；Scene/Prefab RoundTrip 与独立坏数据 fixture 仍未覆盖。
- [x] A3：`Cpu.*` 4/4 通过；`Rendering.*` 在 none 下 8 项 skipped、整体 failed、退出码 1，报告明确 `unsupported_capability`。
- [x] A4：正式项目、Library/DDC、EngineConfig/EditorConfig/UI 配置 hash audit 通过；20 次顺序 + 2 进程并行全部通过且无残留 Editor，失败注入阶段也均为 0 hash 差异。
- [ ] A5：hidden-compat `Automation.*` 3/3 通过；M1/M2 全量回归仍受既有 gate/Debug Layer 未完成约束。
- [~] A6：已记录当前 Windows/D3D12 主机证据；无 GPU 主机与性能对照仍待补充。

## Decision Log

- M3-D01：CPU-only 不实现 NullRHI/WARP，而是从 Runtime 能力图上彻底跳过图形上下文。
- M3-D02：只先支持真正纯 CPU 的 Asset/Scene 操作；复用同一资源数据库，不制造第二套资源模型。
- M3-D03：不把全部插件/脚本系统变为 CPU-safe 当成阶段前提，只让相应测试声明所需能力。

## Surprises & Discoveries

源码表明 ResourceMgr 早期 AssetDB/Handler 注册与后期 Shader/默认 Texture 初始化混在一个 `Initialize()` 中，且 DDC 可能执行孤儿清理；这不只是 GPU 指针问题，也涉及正式项目文件安全。实施过程补充更准确的依赖审计。

## Known Follow-ups

- CPU-only 下完整 asset import、动画离线 Bake、NavMesh、物理模拟、Lua headless、无 DX12 的 Linux/macOS 运行均需单独可行性设计，**不等于完成 M3 即已支持**。

## Outcomes

已实现并完成当前主机验收。证据：`Temp/m3_cpu_final2/result.json`（Cpu.* 4/4，0 window/0 GPU/0 SwapChain/0 Present）、`Temp/m3_gpu_rejected_final/result.json`（Rendering.* 8 skipped，整体 failed，unsupported_capability）、`Temp/m3_repeat_final/`（20 次顺序 + 2 次并行，22/22 通过）、`Temp/m3_formal_hash_audit2/audit.json`（20/20 顺序、2/2 并行、0 文件差异）、`Temp/m3_resource_failure_audit/`（三个失败阶段均 exit 2、0 hash 差异、0 残留进程）。计划按用户决定关闭；无 GPU 主机、M1/M2 全量 gate 等剩余项转为 deferred follow-ups。

## Resume Protocol

读 `README.md` → `progress.md` → 本文件；确认 M1/M2 皆 Accepted。首先复查 ResourceMgr 的真实初始化依赖图和上一次失败用例，只修改 M3 范围；没有无 GPU 验收主机则将 M3 标为 Blocked 而非 Accepted。
