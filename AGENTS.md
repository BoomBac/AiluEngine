# AiluEngine Repository Instructions

## 1. Working principles

- 优先保持实现简单、直接和高效。简单模块不要过度工程化。
- 修改前先确认现有 ownership、lifetime、threading、serialization 和 data flow，不要仅根据类型名推断职责。
- 优先在现有架构的正确位置修复问题，不要为了局部方便创建新的全局单例、隐式全局状态或跨层依赖。
- 不要把聊天上下文当作项目长期事实来源。长期架构事实来自源码和 `Doc/architecture/`；长任务状态来自
  `Doc/exec-plans/active/`。
- 源码是当前实现事实的最终依据。架构文档若与源码冲突，应先确认实际行为，再修正文档或实现。
- 避免与当前任务无关的大范围重构、格式化、重命名或目录移动。

## 2. Code style

- 普通变量：小写和下划线组合，例如 `frame_index`。
- 类/结构体成员变量：以下划线开头，之后使用小写和下划线，例如 `_frame_index`。
- 静态变量：以 `s` 开头。
- 常量：小写 `k` 开头，之后使用驼峰命名，例如 `kFrameCount`。
- 类、结构体以及函数名称：驼峰命名。
- 默认按 160 列控制新增和修改代码的行宽；不要为了满足行宽机械拆碎可读性更好的表达式。
- 同时遵循仓库 `.clang-format` 的其余格式规则。
- 不要对未触及代码做纯格式化修改。

> Repository note: 当前 `.clang-format` 的 `ColumnLimit` 仍为 140。若希望格式化工具与本规则完全一致，应单独将其改为
> 120；不要在普通功能任务中顺手修改。

## 3. Build and validation

默认构建验证只能使用：

```text
cmake --build --preset build-debug
```

不要使用：

```text
cmake --build --preset build-develop
```

验证遵循“最小充分”原则：

1. 先运行与修改直接相关的最小测试、smoke test 或静态检查。
2. 再执行 `build-debug` 验证编译和链接。
3. 只有任务本身需要时才启动完整 Editor、执行图像捕获或更大范围测试。

涉及渲染结果、时序稳定性、RenderGraph pass/resource 输出或 before/after 图像验证时，使用：

```text
.agents/skills/ailu-render-frame-validation/SKILL.md
```

以下结果是不同的验证层级，不要混为一谈：

- 源码检查通过；
- Debug 构建通过；
- 自动化测试通过；
- capture transport 完成；
- 图像/数值分析通过；
- 实际视觉验收通过。

构建日志不要整段复制进任务文档。只记录失败 target、关键错误、涉及文件和最终处理结果。

## 4. Repository map

主要目录：

- `Engine/Inc`：Engine public/internal headers。
- `Engine/Src`：Engine implementation。
- `Engine/Inc/RHI/DX12`、`Engine/Src/RHI/DX12`：DirectX 12 backend。
- `Editor/Inc`、`Editor/Src`：Editor-only code。
- `Player`：runtime player。
- `Test`：测试、smoke 和 regression executables。
- `AiluHeadTool`：reflection/code-generation tool。
- `Engine/Ext`：third-party dependencies。
- `Doc/architecture`：当前有效的长期架构事实。
- `Doc/exec-plans/active`：正在执行的长任务。
- `Doc/exec-plans/completed`：已完成任务的执行记录。
- `Doc` 根目录中的旧设计稿/任务包：历史参考资料，不自动视为当前架构事实。

默认不要修改 `Engine/Ext`，除非任务明确涉及第三方依赖。

## 5. Architecture source of truth

涉及某个 subsystem 的修改前：

1. 先阅读 `Doc/architecture/index.md`。
2. 只读取与当前 subsystem 相关的 architecture 文档。
3. 检查对应源码入口，确认文档仍与实现一致。
4. 如果任务会改变长期架构，完成代码后同步更新相关 architecture 文档。

不要为了“获取上下文”默认读取整个 `Doc/` 目录，也不要把旧 `Task_Package`、`Development_Plan` 或
`Refactor_Plan` 自动当成当前实现。

Architecture 文档描述“现在是什么”。

ExecPlan 描述“这次任务准备把它变成什么”，对拆分式 ExecPlan，只读取 README.md、progress.md 和当前 milestone 文件；
除非当前阶段明确依赖，否则不要读取其他 milestone 文件。

## 6. Generated reflection code

AiluHeadTool 扫描：

```text
Engine/Inc
Editor/Inc
```

典型生成文件位于：

```text
*/generated/*.gen.h
*/generated/*.gen.cpp
```

生成文件位于源码树并参与构建。

除非正在修改 generator 本身，否则：

- 不要手工修改生成文件；
- 修改带 reflection metadata 的源 header；
- 通过正常 code-generation/build 流程更新生成结果；
- 修改公共反射结构时检查 serialization 和旧资产兼容性。

详见 `Doc/architecture/reflection-codegen.md`。

## 7. Runtime architecture boundaries

高层依赖方向：

```text
Editor / Player
      ↓
    Engine
      ↓
Framework / Scene / Assets / Render
      ↓
GraphicsContext
      ↓
RHI / DX12
```

- Engine 不应依赖 Editor。
- `Application` 负责编排应用生命周期和 frame loop。
- `EngineRuntime` 持有主要 runtime subsystem 的生命周期。
- 新增长期 runtime subsystem 前必须先确认 owner、初始化顺序和 shutdown 顺序。
- 不要因为现有系统存在 `Get()`/singleton 接口，就默认新的 subsystem 也应该使用 singleton。

详见 `Doc/architecture/runtime-lifecycle.md`。

## 8. Rendering architecture

当前主要渲染路径：

```text
Scene
  ↓
Renderer scene extraction
  ↓
ScenePrimitive / PrimitiveData / ViewRenderData
  ↓
RenderFeature / RenderPass
  ↓
RenderGraph
  ↓
GraphicsContext
  ↓
RHI / DX12
```

`RenderPipeline` 中仍存在一套旧/过渡路径，包括：

```text
RenderObject
VisibilityObject
ViewEntity
FramePacket view data
CollectViews
UpdateRenderObject
UpdateVisibilityObject
```

当前 `RenderPipeline::Setup()` 已不使用其中主要的 scene collection/update 流程。不要基于这套 legacy/transitional path
开发新的渲染功能，除非任务明确要求清理或兼容它。

新的可选渲染能力优先通过 `RenderFeature` / RenderPass registration 接入，不要无必要继续增加 `Renderer` 对具体 feature 的
硬编码依赖。

详见：

- `Doc/architecture/rendering.md`
- `Doc/architecture/render-graph.md`
- `Doc/architecture/rhi-dx12.md`

## 9. Assets and scene boundaries

`ResourceMgr` 当前仍是较重的 orchestration facade，包含 asset database、import/reload、mount/path、runtime resource 等职责；
`AssetRegistry` 已承担稳定 handle、revision、publish 和 dependency state。

新增资产功能时，不要仅因为 `ResourceMgr::Get()` 易于访问就继续把所有逻辑塞入 `ResourceMgr`。

`Scene` 当前同时承担 entity ownership、hierarchy、ECS、persistent GUID、prefab、update 和部分 GPU/acceleration data 工作。
不要因为某数据“属于场景”就默认它必须成为 `Scene` 成员。

详见：

- `Doc/architecture/asset-system.md`
- `Doc/architecture/scene-ecs.md`

## 10. Threading

区分以下概念：

- main thread；
- render thread / render submission synchronization；
- `JobSystem` worker jobs；
- `Core::ThreadPool` background task pool；
- GPU queue/fence synchronization。

不要假设 worker job 可以直接访问 main-thread-only、Editor UI、Scene mutation 或 GPU API。

跨线程修改资源或场景时，优先使用现有的 queue / synchronization point / dispatcher 模式。

详见 `Doc/architecture/threading.md`。

## 11. Long-running tasks

跨多个阶段、多个 subsystem，或预计会经历多次上下文压缩的任务，应创建：

```text
Doc/exec-plans/active/<task-name>.md
```

使用 `Doc/exec-plans/TEMPLATE.md` 作为起点。

ExecPlan 至少维护：

- Purpose
- Scope / Non-goals
- Architecture constraints
- Relevant files
- Current milestone
- Progress
- Decision Log
- Surprises & Discoveries
- Validation
- Outcomes

每个 milestone 必须能够独立验证。

每完成一个 milestone：

1. 完成实现；
2. 执行对应验证；
3. 更新 ExecPlan；
4. 检查 git diff；
5. 再进入下一阶段。

不要依赖聊天上下文保存长期任务状态。

任务完成后，将 ExecPlan 移入：

```text
Doc/exec-plans/completed/
```

稳定、长期有效的设计结论提炼到 `Doc/architecture/`，不要永久只保留在 ExecPlan 中。

## 12. Change discipline

- 不要删除看似 unused 的代码，除非确认 runtime、Editor、serialization、reflection、generated code 和资产兼容性。
- 涉及 ownership、resource lifetime、main/render thread 交互时，显式检查初始化与销毁顺序。
- 修改公共数据结构时检查 serialization、reflection、generated code、asset version 和旧数据迁移。
- 修改渲染资源时检查 CPU ownership、GPU lifetime、frame resource 和 fence。
- 修改 RenderGraph resource access 时检查 import/read/write、资源状态、subresource 和 barrier 语义。
- Windows 下避免使用可能破坏 CRLF 的全仓库批量 `sed -i` 操作。
- 保留用户已有 dirty changes；不要用 reset/checkout 覆盖与当前任务无关的修改。
