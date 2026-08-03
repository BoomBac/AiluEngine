# AiluEngine 原生 Frame Debugger 实现方案

> 目标仓库：`BoomBac/AiluEngine`  
> 分析基准：仓库 `master` 分支，提交 `c31eda6f49d4ecd9d3c702731d3ac6f42f473f36`  
> 目标平台：Windows / DirectX 12  
> UI 要求：使用 AiluEngine 原生 UI 与 Dock 系统，不使用 ImGui 实现主界面  
> 性能要求：仅在 Capture 帧记录详细数据；非 Capture 状态不得产生内存分配、字符串构造、锁竞争或容器写入

---

## 1. 背景

AiluEngine 当前已经具备实现 Frame Debugger 的关键基础：

1. `CommandBuffer` 将 Draw、Dispatch、SetTarget、Barrier、Profiler 等操作录制为统一的 `GfxCommand` 流。
2. `Material::CaptureDrawState()` 已经在命令录制阶段将材质、全局资源、CommandBuffer 资源和常量缓冲区固化为
   `MaterialDrawState` 与 binding snapshot。
3. `CommandRecordingContext` 已经维护图形 PSO 状态、PSO lookup、当前 PSO 和解析后的资源绑定。
4. `D3DCommandBuffer` 已经维护以下图形状态缓存：
   - PSO
   - VertexBuffer
   - IndexBuffer
   - VertexInputLayout
   - VB/IB view version
   - 32 个 graphics root slot 的 binding hash
5. `RenderingStates` 已经统计：
   - PSO lookup、hit、miss、dirty
   - Material binding resolve、cache hit
   - Pipeline resource submit、override
   - 实际 root slot bind、跳过 bind
   - Draw、Dispatch、CommandList、Submission 等数据
6. `RenderGraph` 已经保存：
   - 排序后的 Pass
   - 编译后的 Pass
   - 输入输出资源
   - 资源版本
   - Pre/Post barrier
   - Submission index
7. Editor 已经有原生：
   - `DockWindow`
   - `DockManager`
   - `TreeView`
   - `SplitView`
   - `ScrollView`
   - `ListView`
   - `CollapsibleView`
   - `Button`
   - `Text`
   - `Image`
   - `Dropdown`

因此不应新建另一套渲染命令或绑定系统。正确方案是在现有热路径旁增加一个仅 Capture 时启用的
**Capture Sidecar**。

---

## 2. 目标

### 2.1 功能目标

实现一个类似 Unity Frame Debugger 的原生编辑器窗口，支持：

- 捕获一个完整渲染帧。
- 按 RenderGraph Pass、CommandBuffer、Draw、Dispatch、Barrier 展示事件树。
- 查看每个 Draw 的：
  - Material
  - Shader
  - Pass
  - Variant
  - PSO
  - RenderTarget / DepthTarget
  - VertexBuffer / IndexBuffer
  - Draw 参数
  - 资源绑定
  - Barrier
  - 是否真正执行
- 查看管线绑定缓存：
  - PSO 是否重绑
  - PSO 状态为什么 dirty
  - PSO library 是否 hit/miss
  - VB/IB 是否重绑以及原因
  - 每个 graphics root slot 是否重绑
  - 每个 root slot 缓存失效的具体原因
  - Material binding cache 是否失效以及原因
  - 当前 Draw 未提交但继承自前一 Draw 的 binding
- 可选保存每个事件后的输出缩略图。
- 支持按缓存失效原因过滤事件。

### 2.2 性能目标

非 Capture 状态：

- 不创建 Capture 对象。
- 不分配 Capture 内存。
- 不写 Capture vector。
- 不生成调试字符串。
- 不读取资源名称。
- 不构造详细 binding key。
- 不执行互斥锁。
- 不使用全局事件队列。
- 热路径最多保留一次可预测的空指针或状态判断。

Capture 状态：

- 允许明显 CPU 和 GPU 开销。
- 多线程命令录制时不得使用全局锁串行化所有 Job。
- Capture 内存应有明确预算和溢出策略。

---

## 3. 非目标

第一版不实现完整 RenderDoc 式重放：

- 不持久化所有 GPU 资源内容。
- 不重建 descriptor heap。
- 不保存所有 upload buffer。
- 不支持任意事件截断后的真实 GPU replay。
- 不提供 shader debugging。
- 不替代 PIX 或 RenderDoc。

第一版输出预览采用事件后 RenderTarget copy 或缩略图历史，而不是完整命令重放。

---

## 4. 现有代码映射

重点文件：

```text
Engine/Inc/Render/GfxCommand.h
Engine/Inc/Render/CommandBuffer.h
Engine/Src/Render/CommandBuffer.cpp

Engine/Inc/Render/MaterialDrawState.h
Engine/Inc/Render/Material.h
Engine/Src/Render/Material.cpp

Engine/Inc/Render/GraphicsPipelineStateObject.h
Engine/Src/Render/GraphicsPipelineStateObject.cpp

Engine/Inc/Render/RenderingStates.h

Engine/Inc/Render/RenderGraph/RenderGraph.h
Engine/Src/Render/RenderGraph/RenderGraph.cpp

Engine/Inc/RHI/DX12/D3DCommandBuffer.h
Engine/Src/RHI/DX12/D3DCommandBuffer.cpp

Engine/Inc/RHI/DX12/D3DContext.h
Engine/Src/RHI/DX12/D3DContext.cpp

Engine/Inc/RHI/DX12/D3DGraphicsPipelineState.h
Engine/Src/RHI/DX12/D3DGraphicsPipelineState.cpp

Editor/Inc/Dock/DockWindow.h
Editor/Inc/Dock/DockManager.h
Editor/Inc/Widgets/WorldOutline.h
Editor/Src/Widgets/WorldOutline.cpp

Engine/Inc/UI/TreeView.h
Engine/Inc/UI/Basic.h
Engine/Inc/UI/Container.h
```

现有关键路径：

```text
RenderFeature / RenderGraph Pass
        │
        ▼
CommandBuffer
        │ 录制 GfxCommand
        │ Material::CaptureDrawState()
        ▼
GpuCommandWorker
        │ CommandGroup
        │ 可并行录制
        ▼
D3DContext::ProcessGpuCommand()
        │ 配置 Shader / PSO / Binding
        ▼
CommandRecordingContext::FindMatchPSO()
        │
        ▼
D3DGraphicsPipelineState::BindImpl()
        │ PSO cache
        │ graphics root slot cache
        ▼
ID3D12GraphicsCommandList
```

---

## 5. 总体架构

```text
FrameDebuggerWindow
        │
        │ RequestCapture
        ▼
FrameCaptureService
        │
        ├── Capture 生命周期
        ├── ActiveSession 原子指针
        ├── LatestCapture
        └── CaptureOptions
                │
                ▼
        FrameCaptureSession
                │
                ├── Command Capture Chunk
                ├── RenderGraph Capture Chunk
                ├── RHI Capture Chunk
                ├── String Interner
                ├── Object Registry
                └── Capture Arena
                        │
                        ▼
                FrameCaptureFinalizer
                        │ 合并、排序、重建层级
                        ▼
                Immutable FrameCapture
                        │
                        ▼
                FrameDebuggerWindow
```

### 5.1 核心原则

- Capture 数据是旁路，不改变正常渲染数据结构语义。
- Capture 数据只保存稳定 ID 和值，不保存帧结束后失效的临时指针。
- 热路径中的详细原因应在发生判断的位置记录，不能在 Capture 结束后通过结果猜测。
- 多线程录制使用独立 chunk，最后合并。
- UI 只读取不可变 `FrameCapture`。

---

## 6. 新增目录与文件

建议增加：

```text
Engine/Inc/Render/FrameDebugger/
    FrameCapture.h
    FrameCaptureTypes.h
    FrameCaptureReason.h
    FrameCaptureService.h
    FrameCaptureSession.h
    FrameCaptureWriter.h

Engine/Src/Render/FrameDebugger/
    FrameCaptureService.cpp
    FrameCaptureSession.cpp
    FrameCaptureWriter.cpp
    FrameCaptureFinalizer.cpp

Editor/Inc/Widgets/
    FrameDebuggerWindow.h

Editor/Src/Widgets/
    FrameDebuggerWindow.cpp
```

可选增加：

```text
Engine/Inc/Render/FrameDebugger/
    FrameCaptureOutput.h
    FrameCaptureSerializer.h

Engine/Src/Render/FrameDebugger/
    FrameCaptureOutput.cpp
    FrameCaptureSerializer.cpp
```

---

## 7. 编译开关

在 Editor 和开发版本中启用：

```cpp
#define AILU_ENABLE_FRAME_DEBUGGER 1
```

Shipping Player 中关闭：

```cpp
#define AILU_ENABLE_FRAME_DEBUGGER 0
```

所有插桩通过：

```cpp
#if AILU_ENABLE_FRAME_DEBUGGER
#endif
```

包围。

Release Editor 仍然保留功能，但非 Capture 状态不得创建详细事件。

---

## 8. Capture 生命周期

### 8.1 状态

```cpp
enum class EFrameCaptureState : u8
{
    kIdle,
    kArmed,
    kCapturing,
    kFinalizing,
    kReady
};
```

含义：

- `kIdle`：没有 Capture。
- `kArmed`：用户请求捕获下一完整帧。
- `kCapturing`：当前帧正在采集。
- `kFinalizing`：渲染线程已经完成，正在合并数据。
- `kReady`：新的不可变 Capture 已生成。

### 8.2 开始和结束时机

推荐流程：

1. UI 调用 `FrameCaptureService::RequestCapture()`。
2. 状态切换为 `kArmed`。
3. 在明确的帧起点调用 `BeginFrame()`。
4. `BeginFrame()` 将 `kArmed` 切换为 `kCapturing`。
5. 当前帧所有 CommandGroup 和 GPU submission 完成。
6. 在 `GpuCommandWorker::EndFrame()` 中调用 `FinalizeFrame()`。
7. `FinalizeFrame()` 合并所有 Capture chunk。
8. 发布新的 `Ref<const FrameCapture>`。

`GpuCommandWorker::EndFrame()` 当前位于本帧录制和提交工作全部完成之后，适合作为 Finalize 点。

### 8.3 服务接口

```cpp
struct FrameCaptureOptions
{
    EFrameCaptureOutputMode _output_mode = EFrameCaptureOutputMode::kMetadataOnly;
    u64 _memory_budget = 256ull * 1024ull * 1024ull;
    u16 _thumbnail_width = 320u;
    u16 _thumbnail_height = 180u;
    bool _capture_resource_names = true;
    bool _capture_binding_reasons = true;
    bool _capture_barriers = true;
};

class FrameCaptureService final
{
public:
    static void RequestCapture(const FrameCaptureOptions &options);
    static void CancelCapture();

    static void BeginFrame(u64 frame_index);
    static void FinalizeFrame();

    static FrameCaptureSession *ActiveSession();
    static Ref<const FrameCapture> LatestCapture();
    static EFrameCaptureState State();

private:
    inline static std::atomic<FrameCaptureSession *> s_active_session = nullptr;
    inline static std::atomic<EFrameCaptureState> s_state = EFrameCaptureState::kIdle;
    inline static std::atomic<bool> s_capture_requested = false;
};
```

### 8.4 ActiveSession

热路径不应每个 slot 都读取全局原子变量。

在 CommandGroup 或 RHI command buffer 开始录制时获取一次：

```cpp
FrameCaptureSession *capture_session = FrameCaptureService::ActiveSession();
```

之后保存到本地：

```cpp
FrameCaptureWriter *capture_writer = capture_session != nullptr
        ? capture_session->CreateWriter(submission_index, command_buffer_name)
        : nullptr;
```

---

## 9. 非 Capture 快速路径

### 9.1 分支策略

推荐在 CommandGroup 粒度分成两条路径：

```cpp
void GpuCommandWorker::RecordCommandGroup(CommandGroup &group, Ref<RHICommandBuffer> &cmd)
{
#if AILU_ENABLE_FRAME_DEBUGGER
    FrameCaptureSession *capture_session = FrameCaptureService::ActiveSession();
    if (AL_UNLIKELY(capture_session != nullptr))
    {
        RecordCommandGroupCaptured(group, cmd, *capture_session);
        return;
    }
#endif

    RecordCommandGroupFast(group, cmd);
}
```

普通路径中不再传递 `capture_writer`。

### 9.2 模板策略

也可以复用逻辑但生成两套编译路径：

```cpp
template<bool kCapture>
void D3DContext::ProcessGpuCommandImpl(GfxCommand *cmd,
                                       RHICommandBuffer *cmd_buffer,
                                       FrameCaptureWriter *capture_writer)
{
    if constexpr (kCapture)
    {
        AL_ASSERT(capture_writer != nullptr);
    }

    // 原有命令处理。
}
```

调用：

```cpp
ProcessGpuCommandImpl<false>(cmd, cmd_buffer, nullptr);
```

Capture：

```cpp
ProcessGpuCommandImpl<true>(cmd, cmd_buffer, capture_writer);
```

要求：

- `if constexpr` 内才允许构造详细对象。
- 普通路径不访问 Capture Arena。
- 普通路径不调用 virtual Capture interface。
- 普通路径不查询资源名称。

---

## 10. Capture 内存模型

### 10.1 Arena

```cpp
class FrameCaptureArena final
{
public:
    explicit FrameCaptureArena(u64 capacity);

    void *Allocate(u64 size, u64 alignment);

    template<typename T>
    T *Allocate(u32 count = 1u)
    {
        return static_cast<T *>(Allocate(sizeof(T) * count, alignof(T)));
    }

    u64 UsedSize() const;
    u64 Capacity() const;
    bool Overflowed() const;

private:
    Vector<u8> _memory;
    u64 _offset = 0u;
    bool _overflowed = false;
};
```

### 10.2 预算溢出

达到预算时：

- 不崩溃。
- 不继续无限扩容。
- 保留已经捕获的数据。
- 设置 `_is_truncated = true`。
- 停止缩略图或大资源 copy。
- 尽量继续保存轻量事件头。
- UI 显示 `Capture truncated due to memory budget`。

### 10.3 字符串

使用 Capture 内部 String Interner：

```cpp
using CaptureStringId = u32;

class CaptureStringTable final
{
public:
    CaptureStringId Intern(StringView text);
    StringView Get(CaptureStringId id) const;
};
```

不要在每个事件中保存 `String`。

只有 Capture 时允许读取：

```cpp
resource->Name();
shader->Name();
material->Name();
```

---

## 11. 对象身份

Capture 不能只使用裸指针作为最终展示 ID。

```cpp
using CaptureObjectId = u64;

enum class ECaptureObjectType : u8
{
    kUnknown,
    kMaterial,
    kShader,
    kGraphicsPso,
    kComputeShader,
    kTexture,
    kBuffer,
    kVertexBuffer,
    kIndexBuffer,
    kConstantBuffer,
    kRenderTarget
};

struct CaptureObjectInfo
{
    CaptureObjectId _id = 0u;
    ECaptureObjectType _type = ECaptureObjectType::kUnknown;
    CaptureStringId _name = 0u;
    u64 _runtime_instance_id = 0u;
    u64 _native_handle = 0u;
};
```

优先使用已有稳定 instance ID 或对象注册 ID。

若当前没有统一稳定 ID，可在 Capture 中使用：

```text
runtime object pointer + capture generation
```

生成仅本 Capture 内稳定的 `CaptureObjectId`，但 UI 不得在后续帧解引用对象。

---

## 12. 事件模型

### 12.1 事件类型

```cpp
enum class EFrameEventType : u8
{
    kFrame,
    kRenderGraphPass,
    kCommandGroup,
    kProfilerScope,
    kSetRenderTarget,
    kClearTarget,
    kDraw,
    kDispatch,
    kDispatchRays,
    kBuildAccelerationStructure,
    kResourceUpload,
    kResourceBarrier,
    kUavBarrier,
    kCopyCounter,
    kReadback,
    kPresent,
    kDebuggerInternal
};
```

### 12.2 执行结果

```cpp
enum class EFrameEventExecutionResult : u8
{
    kExecuted,
    kRecordedOnly,
    kSkippedShaderNotReady,
    kSkippedPsoNotReady,
    kSkippedInvalidDispatch,
    kSkippedInvalidResource,
    kSkippedUnknown
};
```

### 12.3 基础事件

```cpp
struct FrameEvent
{
    u32 _event_id = 0u;
    u32 _parent_event_id = 0u;

    u32 _submission_index = 0u;
    u32 _command_index = 0u;
    u16 _sub_event_index = 0u;

    EFrameEventType _type = EFrameEventType::kDraw;
    EFrameEventExecutionResult _execution_result = EFrameEventExecutionResult::kRecordedOnly;

    CaptureStringId _name = 0u;
    u32 _payload_index = 0u;
};
```

### 12.4 排序

多线程录制完成后按以下键稳定排序：

```text
submission_index
command_index
sub_event_index
```

不要按照 Job 完成顺序排序。

---

## 13. Draw Capture

```cpp
struct DrawEventCapture
{
    CaptureObjectId _material_id = 0u;
    CaptureObjectId _shader_id = 0u;
    CaptureObjectId _pso_id = 0u;
    CaptureObjectId _vertex_buffer_id = 0u;
    CaptureObjectId _index_buffer_id = 0u;
    CaptureObjectId _argument_buffer_id = 0u;

    u16 _pass_index = 0u;
    u16 _sub_mesh = 0u;
    u64 _variant_hash = 0u;

    u32 _vertex_count = 0u;
    u32 _index_count = 0u;
    u32 _index_start = 0u;
    u32 _instance_count = 0u;
    u32 _start_instance = 0u;
    u32 _argument_offset = 0u;

    bool _is_indexed = false;
    bool _is_indirect = false;
    bool _is_procedural = false;

    u32 _pipeline_state_index = 0u;
    u32 _render_target_state_index = 0u;

    u32 _binding_range_begin = 0u;
    u16 _binding_count = 0u;

    u32 _barrier_range_begin = 0u;
    u16 _barrier_count = 0u;
};
```

记录点：

- `CommandBuffer` 阶段记录逻辑 Draw 参数和 Material snapshot。
- `D3DContext::ProcessGpuCommand()` 阶段补充：
  - 是否执行
  - 实际 PSO
  - VB/IB bind 结果
  - root slot bind 结果
  - 实际 draw 类型
  - 实际 index / vertex / instance 数

---

## 14. RenderGraph Capture

### 14.1 Pass 信息

```cpp
struct RenderGraphPassCapture
{
    CaptureStringId _name = 0u;
    EPassType _type = EPassType::kGraphics;

    u32 _submission_index = 0u;
    bool _allow_parallel_recording = true;

    u32 _input_range_begin = 0u;
    u16 _input_count = 0u;

    u32 _output_range_begin = 0u;
    u16 _output_count = 0u;

    u32 _pre_barrier_range_begin = 0u;
    u16 _pre_barrier_count = 0u;

    u32 _post_barrier_range_begin = 0u;
    u16 _post_barrier_count = 0u;
};
```

### 14.2 资源访问

```cpp
struct RenderGraphResourceAccessCapture
{
    CaptureObjectId _resource_id = 0u;
    CaptureStringId _resource_name = 0u;

    u32 _handle_id = 0u;
    u32 _handle_version = 0u;

    EResourceUsage _usage = EResourceUsage::kNone;
    ELoadStoreAction _load_action = ELoadStoreAction::kLoad;
    ELoadStoreAction _store_action = ELoadStoreAction::kStore;

    u32 _mip_level = 0u;
    u32 _mip_count = 1u;
    u32 _array_slice = 0u;
    u32 _array_slice_count = 1u;
    bool _all_sub_resources = true;
};
```

### 14.3 插桩位置

在 `RenderGraph::Execute()` 遍历 `_compiled_passes` 时记录：

```cpp
capture_writer->BeginRenderGraphPass(compiled_pass);
compiled_pass._pass->Execute(*this, cmd, data);
capture_writer->EndRenderGraphPass();
```

必须显式保存 pass capture ID，并传给该 Pass 创建的 CommandBuffer 或 CommandGroup。

不能仅依赖 CommandBuffer 名称推断 Pass 层级。

---

## 15. PSO 状态失效原因

当前 `CommandRecordingContext::MarkPSODirty()` 只设置 `_is_pso_dirty`，具体原因丢失。

### 15.1 原因枚举

```cpp
enum class EPsoDirtyReason : u32
{
    kNone = 0u,
    kShaderChanged = 1u << 0u,
    kShaderPassChanged = 1u << 1u,
    kShaderVariantChanged = 1u << 2u,
    kVertexLayoutChanged = 1u << 3u,
    kBlendStateChanged = 1u << 4u,
    kRasterizerStateChanged = 1u << 5u,
    kDepthStencilStateChanged = 1u << 6u,
    kRenderTargetStateChanged = 1u << 7u
};
```

需要提供按位运算支持。

### 15.2 修改 MarkPSODirty

```cpp
void MarkPSODirty(EPsoDirtyReason reason)
{
    _is_pso_dirty = true;
    _pso_request_submitted = false;

#if AILU_ENABLE_FRAME_DEBUGGER
    if (AL_UNLIKELY(_capture_writer != nullptr))
        _capture_pso_dirty_reasons |= reason;
#endif
}
```

普通路径中 `_capture_writer` 不应存在或始终为空。

更严格的方式是在 Captured 版本的 `CommandRecordingContext` 中保存原因，普通版本不保存。

### 15.3 ConfigureShader

```cpp
EPsoDirtyReason reason = EPsoDirtyReason::kNone;

if (shader != _current_shader)
    reason |= EPsoDirtyReason::kShaderChanged;
if (pass_index != _current_pass_index)
    reason |= EPsoDirtyReason::kShaderPassChanged;
if (variant_hash != _current_variant_hash)
    reason |= EPsoDirtyReason::kShaderVariantChanged;

if (reason != EPsoDirtyReason::kNone)
    MarkPSODirty(reason);
```

### 15.4 其他配置函数

映射：

```text
ConfigureVertexInputLayout -> kVertexLayoutChanged
ConfigureBlendState        -> kBlendStateChanged
ConfigureRasterizerState   -> kRasterizerStateChanged
ConfigureDepthStencilState -> kDepthStencilStateChanged
SetRenderTargetState       -> kRenderTargetStateChanged
ResetRenderTargetState     -> kRenderTargetStateChanged
```

### 15.5 PSO lookup 结果

```cpp
enum class EPsoLookupResult : u8
{
    kNotRequired,
    kCacheHit,
    kCacheMiss,
    kNotReady,
    kCreationRequested
};

struct PsoLookupCapture
{
    PSOHash _previous_hash;
    PSOHash _current_hash;
    EPsoDirtyReason _dirty_reasons = EPsoDirtyReason::kNone;
    EPsoLookupResult _result = EPsoLookupResult::kNotRequired;
};
```

UI 必须区分：

```text
PSO State Dirty
PSO Library Lookup
PSO Native Bind
```

PSO 状态 dirty 不等于 PSO library miss；PSO library hit 也可能需要 native `SetPipelineState()`。

---

## 16. Native PSO 绑定原因

当前 `D3DGraphicsPipelineState::BindImpl()` 使用：

```cpp
const bool is_same_pso = d3dcmd->IsGraphicsPSOActive(this);
```

Capture 时记录：

```cpp
enum class EPsoBindReason : u8
{
    kCacheHit,
    kFirstBind,
    kPsoObjectChanged,
    kCommandListReset
};
```

建议 `GraphicsStateCache` Capture shadow 额外保存：

- 是否曾绑定过 PSO
- 上一个 PSO capture ID
- cache reset generation

`SetGraphicsPSOActive()` 当前会在 PSO 变化时清空 VB、IB 和 root slot cache。Capture 必须把这种清空标记为
`kPsoChanged`，否则后续 slot 只会显示 `SlotUninitialized`，无法解释根因。

---

## 17. Root Slot 绑定缓存失效原因

当前 `BuildBindingHash()` 将多个字段混成一个 `u64`。正常路径继续使用该 hash，但 Capture 时必须保存可比较的完整 key。

### 17.1 Binding Key

```cpp
struct PipelineBindingKey
{
    CaptureObjectId _resource_id = 0u;
    EBindResDescType _resource_type = EBindResDescType::kUnknown;

    u64 _gpu_handle = 0u;
    u64 _native_resource = 0u;

    u32 _view_index = 0u;
    u32 _sub_resource = 0u;

    u16 _slot = 0u;
    u16 _descriptor_heap_id = 0u;
};
```

### 17.2 失效原因

```cpp
enum class EBindingInvalidReason : u32
{
    kNone = 0u,
    kSlotUninitialized = 1u << 0u,
    kPsoChanged = 1u << 1u,
    kCommandListReset = 1u << 2u,
    kResourceChanged = 1u << 3u,
    kResourceTypeChanged = 1u << 4u,
    kGpuAddressChanged = 1u << 5u,
    kNativeResourceChanged = 1u << 6u,
    kViewIndexChanged = 1u << 7u,
    kSubResourceChanged = 1u << 8u,
    kDescriptorHeapChanged = 1u << 9u
};
```

### 17.3 比较函数

```cpp
EBindingInvalidReason CompareBindingKey(const PipelineBindingKey &old_key,
                                        const PipelineBindingKey &new_key)
{
    EBindingInvalidReason reasons = EBindingInvalidReason::kNone;

    if (old_key._resource_id != new_key._resource_id)
        reasons |= EBindingInvalidReason::kResourceChanged;
    if (old_key._resource_type != new_key._resource_type)
        reasons |= EBindingInvalidReason::kResourceTypeChanged;
    if (old_key._gpu_handle != new_key._gpu_handle)
        reasons |= EBindingInvalidReason::kGpuAddressChanged;
    if (old_key._native_resource != new_key._native_resource)
        reasons |= EBindingInvalidReason::kNativeResourceChanged;
    if (old_key._view_index != new_key._view_index)
        reasons |= EBindingInvalidReason::kViewIndexChanged;
    if (old_key._sub_resource != new_key._sub_resource)
        reasons |= EBindingInvalidReason::kSubResourceChanged;
    if (old_key._descriptor_heap_id != new_key._descriptor_heap_id)
        reasons |= EBindingInvalidReason::kDescriptorHeapChanged;

    return reasons;
}
```

### 17.4 Capture Shadow Cache

不要修改正常 `GraphicsStateCache` 的大小和访问方式。仅 Capture 时创建：

```cpp
struct CaptureGraphicsStateCache
{
    Array<PipelineBindingKey, 32> _slot_keys;
    u32 _valid_slot_mask = 0u;
    u32 _pso_invalidated_slot_mask = 0u;
    u32 _command_list_reset_slot_mask = 0u;
};
```

它可以属于 `FrameCaptureWriter` 或 Captured RHI command buffer context。

### 17.5 Binding 结果

```cpp
enum class EBindingCacheResult : u8
{
    kBound,
    kSkipped,
    kInherited,
    kUnbound
};

enum class EBindingSource : u8
{
    kUnknown,
    kGlobal,
    kMaterial,
    kCommand,
    kInherited
};

struct PipelineBindingCapture
{
    u16 _slot = 0u;
    CaptureStringId _slot_name = 0u;

    PipelineBindingKey _key;

    EBindingCacheResult _cache_result = EBindingCacheResult::kUnbound;
    EBindingInvalidReason _invalid_reasons = EBindingInvalidReason::kNone;
    EBindingSource _source = EBindingSource::kUnknown;

    u32 _inherited_from_event = 0u;
    u16 _priority = 0u;
};
```

---

## 18. Inherited Binding

现有 `D3DGraphicsPipelineState::BindImpl()` 的语义是：

- 当前 Draw 提交的 slot 会参与比较和绑定。
- 当前 Draw 未提交的可选 slot 不会主动清空。
- 这些 slot 在 PSO 不变时保持上一 Draw 的值。
- PSO 改变时 slot cache 才清空。

因此 Frame Debugger 不能只展示当前 `MaterialDrawState::_bindings`，必须展示实际有效 binding。

### 18.1 算法

每个 Draw 开始：

1. 获取 PSO 声明的 slot。
2. 标记当前 Draw snapshot 中提交的 slot。
3. 对未提交但 capture shadow cache 中有效的 slot：
   - 标记为 `kInherited`
   - 保存来源事件 ID
4. 对 PSO 声明但没有当前提交、也没有继承值的 slot：
   - 标记为 `kUnbound`
5. 若该 slot 非 optional，生成 warning。

### 18.2 UI

示例：

```text
Slot  Name         Result      Source       Reason
0     _PerScene    Skipped     Global       Cache Hit
1     _MainTex     Bound       Material     ResourceChanged
2     _ShadowMap   Inherited   Event #183   Current Draw Omitted
4     _PerObject   Bound       Command      GpuAddressChanged
```

Inherited binding 使用黄色。

Required but unbound 使用红色。

---

## 19. VB/IB 缓存失效原因

### 19.1 原因枚举

```cpp
enum class EGeometryBindingInvalidReason : u32
{
    kNone = 0u,
    kFirstBind = 1u << 0u,
    kPsoChanged = 1u << 1u,
    kCommandListReset = 1u << 2u,
    kBufferChanged = 1u << 3u,
    kInputLayoutChanged = 1u << 4u,
    kViewVersionChanged = 1u << 5u
};
```

### 19.2 VertexBuffer 判断

当前条件：

```cpp
!d3dcmd->IsVertexBufferActive(draw_cmd->_vb, layout, vb_view_version)
```

Capture 时逐字段比较：

```text
old vb != new vb                       -> kBufferChanged
old layout != new layout               -> kInputLayoutChanged
old view version != new view version   -> kViewVersionChanged
PSO 切换清空                         -> kPsoChanged
```

### 19.3 IndexBuffer 判断

```text
old ib != new ib                       -> kBufferChanged
old view version != new view version   -> kViewVersionChanged
PSO 切换清空                         -> kPsoChanged
```

### 19.4 数据

```cpp
struct GeometryBindingCapture
{
    CaptureObjectId _vertex_buffer_id = 0u;
    CaptureObjectId _index_buffer_id = 0u;

    bool _vertex_buffer_bound = false;
    bool _index_buffer_bound = false;

    EGeometryBindingInvalidReason _vertex_buffer_reasons =
            EGeometryBindingInvalidReason::kNone;
    EGeometryBindingInvalidReason _index_buffer_reasons =
            EGeometryBindingInvalidReason::kNone;

    u64 _vertex_buffer_view_version = 0u;
    u64 _index_buffer_view_version = 0u;
    u64 _input_layout_id = 0u;
};
```

---

## 20. Material Binding Cache 失效原因

当前 `Material::CaptureDrawState()` 的缓存条件包含：

```text
resource_binding_version
layout_version
variant_hash
global_resource_layout_version
global_resource_binding_version
```

### 20.1 原因枚举

```cpp
enum class EMaterialBindingInvalidReason : u32
{
    kNone = 0u,
    kMaterialResourceChanged = 1u << 0u,
    kBindingLayoutChanged = 1u << 1u,
    kShaderVariantChanged = 1u << 2u,
    kGlobalLayoutChanged = 1u << 3u,
    kGlobalBindingChanged = 1u << 4u
};
```

### 20.2 Full rebuild 与局部更新

当前逻辑中应区分：

```text
Cache Hit
Global Binding Refresh
Full Binding Rebuild
```

例如：

- 只有 global binding version 变化：不一定需要完整重建 layout。
- Material local resource version 变化：需要 full rebuild。
- Shader variant 或 layout 变化：需要 full rebuild。
- Command resource override：不是 cache invalidation。

### 20.3 Capture 数据

```cpp
enum class EMaterialBindingResolveResult : u8
{
    kCacheHit,
    kGlobalBindingRefresh,
    kFullRebuild
};

struct MaterialBindingCapture
{
    EMaterialBindingResolveResult _result =
            EMaterialBindingResolveResult::kCacheHit;
    EMaterialBindingInvalidReason _invalid_reasons =
            EMaterialBindingInvalidReason::kNone;

    u32 _material_resource_version = 0u;
    u32 _layout_version = 0u;
    u64 _variant_hash = 0u;
    u32 _global_layout_version = 0u;
    u32 _global_binding_version = 0u;
};
```

### 20.4 Binding 优先级

每个最终资源应记录：

```text
Global
Material
CommandBuffer
Effective
```

CommandBuffer 覆盖材质资源时显示：

```text
Reason: Command Override
```

这不应归类为 Material cache miss。

---

## 21. Barrier Capture

### 21.1 数据

```cpp
struct ResourceBarrierCapture
{
    CaptureObjectId _resource_id = 0u;
    CaptureStringId _resource_name = 0u;

    EResourceState _before = EResourceState::kCommon;
    EResourceState _after = EResourceState::kCommon;

    u32 _sub_resource = kTotalSubRes;
    bool _is_uav = false;
    bool _is_reconcile = false;
    bool _is_debugger_internal = false;
};
```

### 21.2 来源

记录：

- RenderGraph compiled pre barrier
- RenderGraph compiled post barrier
- 显式 `CommandResourceBarrier`
- `CommandTranslateState`
- UAV barrier
- 多 command list submission 时的 resource state reconcile barrier
- Frame Debugger 输出 copy 自己插入的内部 barrier

UI 默认隐藏 `_is_debugger_internal`。

---

## 22. D3DContext 插桩

`D3DContext::ProcessGpuCommand()` 是最终执行真相来源。

### 22.1 Draw

在 Draw 分支记录：

1. `CommandDraw` 的基础信息。
2. Material state 是否 ready。
3. Shader bind。
4. Rasterizer state 配置。
5. 解析后的 binding snapshot。
6. PSO lookup。
7. VB/IB bind 判断。
8. `pso->Bind()` 的 native PSO 和 root slot bind。
9. 实际 Draw / DrawIndexed / ExecuteIndirect。
10. execution result。

### 22.2 被跳过的 Draw

当前以下情况会跳过：

- Material/Shader variant 未 ready。
- PSO 未找到或未 ready。
- 资源无效。

必须保留事件并写明原因，不能直接不显示。

### 22.3 Dispatch

记录：

- ComputeShader
- Kernel
- Group count
- Indirect 参数
- Snapshot 是否 ready
- 资源绑定
- execution result

### 22.4 CommandProfiler

Profiler scope 可直接作为事件树的中间层级：

```text
RenderGraph Pass
  CommandGroup
    Profiler Scope
      Draw
      Draw
```

需要用栈生成 parent event ID。

---

## 23. 多线程录制

当前 `GpuCommandWorker` 可以把多个 CommandGroup 分给多个 Job。

### 23.1 禁止方案

不要：

- 所有线程锁一个 `Vector<FrameEvent>`。
- 每条事件调用全局 `Intern()` 并加 mutex。
- 使用单一全局 object map。
- 按 Job 完成时间形成事件顺序。

### 23.2 推荐方案

每个录制 Job 或 CommandGroup 拥有：

```cpp
struct FrameCaptureChunk
{
    u32 _submission_index = 0u;
    CaptureStringId _command_group_name = 0u;

    Vector<FrameEvent> _events;
    Vector<DrawEventCapture> _draws;
    Vector<PipelineBindingCapture> _bindings;
    Vector<ResourceBarrierCapture> _barriers;

    LocalCaptureStringTable _strings;
    LocalCaptureObjectTable _objects;
};
```

Finalize 时：

1. 收集 chunk。
2. 按 submission index 排序。
3. 合并 local string table。
4. 修正 string ID。
5. 合并 object table。
6. 修正 object ID。
7. 修正 payload index 和 parent ID。
8. 生成最终只读数组。

### 23.3 Chunk 注册

Capture Session 可以预分配固定 chunk 数组，使用原子索引获取：

```cpp
u32 chunk_index = _next_chunk_index.fetch_add(1u, std::memory_order_relaxed);
```

只在每个 CommandGroup 开始时调用一次，不在每个 Draw 中调用。

---

## 24. 输出预览

### 24.1 模式

```cpp
enum class EFrameCaptureOutputMode : u8
{
    kMetadataOnly,
    kOutputThumbnails,
    kFullOutputHistory
};
```

### 24.2 MetadataOnly

- 默认。
- 不增加 GPU Copy。
- 最适合分析缓存和绑定问题。

### 24.3 OutputThumbnails

- 对修改主颜色目标的事件保存缩略图。
- 默认 320 × 180。
- 可只在 Draw、Dispatch 或 Clear 后保存。
- 使用独立 Capture texture array 或 readback。
- 显著增加 Capture 帧成本，但只影响 Capture 帧。

### 24.4 FullOutputHistory

- 每个目标事件后保存完整输出。
- 默认关闭。
- 严格受内存预算限制。
- 达到预算后自动退化为缩略图或停止保存。

### 24.5 Debugger Internal Event

输出 copy 插入的 barrier、copy、resolve 必须标记：

```cpp
EFrameEventType::kDebuggerInternal
```

默认不显示，也不参与正常渲染统计。

### 24.6 纹理预览

第一版支持：

- Color RenderTarget
- 常规 Texture2D
- Texture2DArray 的指定 slice
- Mip 选择

后续支持：

- Depth 映射到灰度
- Stencil
- UAV
- Cube face
- 3D texture slice
- HDR exposure

---

## 25. 原生 UI 设计

### 25.1 Window

```cpp
class FrameDebuggerWindow final : public DockWindow
{
public:
    FrameDebuggerWindow();
    ~FrameDebuggerWindow() override;

    void Update(f32 dt) override;

private:
    void BuildUI();
    void BindEvents();

    void RefreshCapture();
    void RefreshSummary();
    void RefreshEventTree();
    void RefreshSelectedEvent();
    void RefreshOutputPreview();

    void BuildEventSection(const FrameEvent &event);
    void BuildPipelineSection(const FrameEvent &event);
    void BuildGeometrySection(const FrameEvent &event);
    void BuildRenderTargetSection(const FrameEvent &event);
    void BuildBindingSection(const FrameEvent &event);
    void BuildBarrierSection(const FrameEvent &event);
    void BuildStatisticsSection(const FrameEvent &event);

private:
    class FrameEventTreeDataSource;

    Scope<FrameEventTreeDataSource> _data_source;
    Ref<const FrameCapture> _capture;

    UI::Button *_capture_button = nullptr;
    UI::Dropdown *_capture_mode_dropdown = nullptr;
    UI::InputBlock *_search_input = nullptr;
    UI::Text *_summary_text = nullptr;

    UI::TreeView *_event_tree = nullptr;
    UI::Image *_output_preview = nullptr;
    UI::ScrollView *_detail_scroll = nullptr;
    UI::VerticalBox *_detail_content = nullptr;

    u32 _selected_event_id = 0u;
    u64 _observed_capture_revision = 0u;
};
```

### 25.2 布局

```text
VerticalBox
├── Toolbar
│   ├── Capture Button
│   ├── Capture Mode Dropdown
│   ├── Output History CheckBox
│   ├── Filter Dropdown
│   └── Search Input
├── Summary Bar
└── SplitView
    ├── Event Tree
    └── SplitView
        ├── Output Preview
        └── Detail ScrollView
```

### 25.3 Event Tree

数据源层级：

```text
Frame
├── RenderGraph Pass
│   ├── CommandGroup
│   │   ├── Profiler Scope
│   │   │   ├── Clear
│   │   │   ├── Draw
│   │   │   └── Dispatch
│   │   └── Barrier
│   └── CommandGroup
└── Present
```

TreeItemId 可直接使用 Event ID：

```cpp
return static_cast<UI::TreeItemId>(event_id + 1u);
```

保留 `0` 作为 invalid。

### 25.4 行样式

建议：

- Draw：普通文字
- Dispatch：蓝色
- Clear：灰色
- Barrier：紫色
- Skipped：红色
- PSO dirty：橙色标记
- Root binding 发生：黄色小标记
- 全部缓存命中：绿色小标记
- Debugger internal：默认隐藏

### 25.5 详情面板

使用 `CollapsibleView`：

```text
Event
Pipeline State
Shaders
Render Targets
Geometry
Resource Bindings
Binding Cache
Resource Barriers
Statistics
Warnings
```

### 25.6 Binding 表格

第一版可用：

```text
ScrollView
  VerticalBox
    HorizontalBox row
```

每行：

```text
Slot | Name | Type | Resource | Source | Result | Invalid Reason
```

若事件数量和 binding 行过多，后续增加原生 `TableView` 和行虚拟化。

---

## 26. 过滤器

建议支持：

```cpp
enum class EFrameEventFilter : u32
{
    kNone = 0u,
    kDraw = 1u << 0u,
    kDispatch = 1u << 1u,
    kBarrier = 1u << 2u,
    kSkipped = 1u << 3u,
    kPsoDirty = 1u << 4u,
    kPsoCacheMiss = 1u << 5u,
    kMaterialCacheMiss = 1u << 6u,
    kRootSlotBind = 1u << 7u,
    kInheritedBinding = 1u << 8u,
    kWarnings = 1u << 9u
};
```

常用快捷过滤：

- Only Draws
- PSO Changes
- PSO Cache Misses
- Material Cache Misses
- Actual Root Binds
- Inherited Bindings
- Skipped Events
- Warnings

---

## 27. 顶部统计

最终 Capture 直接复制现有 `RenderingStatesData`：

```text
DrawCall
DispatchCall
GfxPsoBindCount
GfxResBindCount
GfxPsoDirtyCount
PsoLookupCount
PsoCacheHitCount
PsoCacheMissCount
MaterialCaptureCount
MaterialBindingResolveCount
MaterialBindingCacheHitCount
PipelineResourceSubmitCount
PipelineResourceOverrideCount
ActualRootSlotBindCount
SkippedRootSlotBindCount
CommandGroupCount
CommandListCount
CommandSubmitCount
CommandFenceSignalCount
CommandRecordingTimeMs
CommandSubmissionTimeMs
```

额外 Capture 统计：

```cpp
struct FrameCaptureStatistics
{
    RenderingStatesData _rendering_states;

    u64 _capture_cpu_memory = 0u;
    u64 _capture_gpu_memory = 0u;

    u32 _event_count = 0u;
    u32 _draw_count = 0u;
    u32 _dispatch_count = 0u;
    u32 _barrier_count = 0u;

    u32 _pso_dirty_event_count = 0u;
    u32 _pso_miss_event_count = 0u;
    u32 _material_cache_miss_event_count = 0u;
    u32 _inherited_binding_count = 0u;
    u32 _warning_count = 0u;

    bool _is_truncated = false;
};
```

---

## 28. API 修改建议

### 28.1 SubmitParams

增加 Capture 关联信息：

```cpp
struct SubmitParams
{
    String _name;
    bool _is_end_frame = false;
    CommandRenderingStatesData _rendering_states_data;

#if AILU_ENABLE_FRAME_DEBUGGER
    u32 _capture_render_pass_id = 0u;
    Scope<CaptureCommandMetadata> _capture_metadata;
#endif
};
```

如果希望完全避免非 Capture 时增加移动成本，可将 `_capture_metadata` 放到可选 side table，不直接放入常规结构。

### 28.2 CommandBuffer

增加仅 Capture 数据：

```cpp
class CommandBuffer
{
public:
#if AILU_ENABLE_FRAME_DEBUGGER
    void SetCaptureRenderPassId(u32 render_pass_id);
    Scope<CaptureCommandMetadata> TakeCaptureMetadata();
#endif
};
```

### 28.3 RHICommandBuffer

Capture 版本需要关联 Writer：

```cpp
class RHICommandBuffer
{
public:
#if AILU_ENABLE_FRAME_DEBUGGER
    void SetCaptureWriter(FrameCaptureWriter *writer);
    FrameCaptureWriter *CaptureWriter() const;
#endif
};
```

普通状态中始终为 `nullptr`。

### 28.4 CommandRecordingContext

增加 Captured API：

```cpp
#if AILU_ENABLE_FRAME_DEBUGGER
void SetCaptureWriter(FrameCaptureWriter *writer);
EPsoDirtyReason TakePsoDirtyReasons();
#endif
```

不要让 UI 或 Editor 直接依赖 DX12 类型。

---

## 29. 推荐实现方式：逻辑层与 RHI 层分离

Capture 数据分为两层：

### 29.1 Logical Capture

来源：

- CommandBuffer
- Material
- RenderGraph

内容：

- Material、Shader、Pass、Variant
- 逻辑 binding 来源
- Material cache invalid reason
- RenderGraph Pass
- 资源读写关系
- Draw 原始参数

### 29.2 RHI Capture

来源：

- D3DContext
- D3DCommandBuffer
- D3DGraphicsPipelineState

内容：

- 实际 PSO lookup/bind
- VB/IB bind
- Root slot bind
- Binding invalid reason
- 实际 Draw 类型
- Barrier
- Execution result

Finalize 时通过：

```text
submission_index + command_index
```

合并 Logical 和 RHI 数据。

这样未来添加 Vulkan backend 时可以复用 Logical Capture，只替换 RHI Capture。

---

## 30. Phase 1 实现任务

目标：先完成纯元数据 Frame Debugger 和缓存失效原因，不做输出图像。

### 30.1 Engine Core

- [ ] 新增 `FrameCaptureTypes.h`
- [ ] 新增 `FrameCaptureReason.h`
- [ ] 新增 `FrameCapture.h`
- [ ] 新增 `FrameCaptureService`
- [ ] 新增 `FrameCaptureSession`
- [ ] 新增 `FrameCaptureWriter`
- [ ] 新增 Capture Arena
- [ ] 新增 String Table
- [ ] 新增 Object Table
- [ ] 实现 chunk 合并和排序
- [ ] 实现 Capture revision

### 30.2 RenderGraph

- [ ] 捕获 compiled pass
- [ ] 捕获 pass 输入输出
- [ ] 捕获资源版本
- [ ] 捕获 pre/post barrier
- [ ] 把 RenderGraph pass ID 传入 CommandBuffer

### 30.3 CommandBuffer / Material

- [ ] 捕获 command index
- [ ] 捕获 Draw 原始参数
- [ ] 捕获 Material binding cache invalid reason
- [ ] 捕获 Global / Material / Command binding 来源
- [ ] 捕获 Command override

### 30.4 PSO

- [ ] `MarkPSODirty(reason)`
- [ ] ConfigureShader 原因细分
- [ ] RenderTarget state 原因
- [ ] PSO lookup result
- [ ] Native PSO bind result

### 30.5 Geometry

- [ ] VB bind reason
- [ ] IB bind reason
- [ ] InputLayout change reason
- [ ] View version change reason

### 30.6 Root Slot

- [ ] Capture binding key
- [ ] Capture shadow slot cache
- [ ] 比较 binding key
- [ ] 记录 bind/skip
- [ ] 记录 inherited binding
- [ ] 记录 required but unbound warning

### 30.7 UI

- [ ] 新增 `FrameDebuggerWindow`
- [ ] 原生 toolbar
- [ ] Event Tree data source
- [ ] 详情面板
- [ ] Binding 列表
- [ ] 过滤器
- [ ] Capture 状态和错误提示
- [ ] DockManager 注册入口

### 30.8 Phase 1 验收

- 点击 Capture 后捕获下一完整帧。
- Event Tree 顺序在多线程录制开关前后保持确定性。
- 能查看每个 Draw 的 Shader、Material、Pass、Variant、PSO、VB、IB。
- 能查看 PSO dirty 的具体原因。
- 能区分 PSO dirty、PSO library miss 和 native PSO bind。
- 能查看每个 graphics root slot 的 bind/skip。
- 能查看 root slot 失效具体字段。
- 能查看 inherited binding。
- 能查看 Material cache miss 原因。
- 非 Capture 状态不分配 Capture 内存。
- 非 Capture 状态不生成资源调试字符串。
- 非 Capture 状态性能变化处于测试噪声范围。

---

## 31. Phase 2 实现任务

目标：完善资源、Barrier 和 RenderGraph 检查。

- [ ] RenderTarget / DepthTarget 完整描述
- [ ] Texture / Buffer descriptor
- [ ] Mip / slice / view index
- [ ] Barrier 时间线
- [ ] Resource state reconcile barrier
- [ ] Required binding 检查
- [ ] 同一 slot 类型变化警告
- [ ] Active RenderTarget 同时作为 SRV/UAV 的警告
- [ ] 资源生命周期和 transient 标识
- [ ] 按资源查看所有读写事件
- [ ] 按事件查看所有相关资源

---

## 32. Phase 3 实现任务

目标：逐事件输出预览。

- [ ] MetadataOnly 模式
- [ ] Thumbnail 模式
- [ ] Full Output History 模式
- [ ] Capture GPU 内存预算
- [ ] Debugger internal event
- [ ] Color RT preview
- [ ] Mip / array slice 选择
- [ ] Depth debug shader
- [ ] HDR exposure
- [ ] 选中事件输出显示

---

## 33. Phase 4 实现任务

- [ ] Capture JSON 导出
- [ ] Capture binary 导出
- [ ] 两次 Capture Diff
- [ ] 按原因聚合
- [ ] 重复 PSO bind 分析
- [ ] 重复 root slot bind 分析
- [ ] Binding 排序优化建议
- [ ] GPU timestamp
- [ ] Pass 时间线
- [ ] Resource lifetime 图

---

## 34. 测试计划

### 34.1 功能测试

构造场景：

1. 多个对象使用同一材质和同一 PSO。
2. 交替使用不同材质纹理。
3. 同一纹理不同 view index。
4. 同一 buffer GPU 地址变化。
5. Shader variant 切换。
6. Rasterizer cull mode 切换。
7. RenderTarget format 切换。
8. VB 相同但 input layout 不同。
9. VB view version 更新。
10. 当前 Draw 省略 optional slot，继承上一 Draw。
11. Required slot 未绑定。
12. Shader 未 ready。
13. PSO 创建请求尚未完成。
14. 多 CommandGroup 并行录制。
15. Graphics job 开关前后对比。

### 34.2 原因验证

每个测试必须验证：

```text
预期 bind/skip
预期 invalid reason
预期来源
预期 inherited_from_event
预期 execution result
```

### 34.3 性能测试

分别测试：

```text
Frame Debugger 编译关闭
Frame Debugger 编译开启但 Idle
Frame Debugger Capture MetadataOnly
Frame Debugger Capture Thumbnail
```

指标：

- CPU Frame Time
- Render Thread Time
- Command Recording Time
- 内存分配次数
- Capture 内存
- Draw throughput
- Root slot bind count

Idle 相对编译关闭版本：

```text
CPU 性能差异应处于测量噪声范围
每帧 Capture allocation = 0
每帧 Capture string construction = 0
每帧 Capture mutex lock = 0
```

### 34.4 多线程稳定性

连续 Capture 100 次：

- 不崩溃。
- 无数据竞争。
- Event ID 不重复。
- Event 顺序稳定。
- parent event 合法。
- Chunk 合并后 payload index 合法。
- Capture 完成后不持有 Command、RenderPass、FrameAllocator 临时指针。

---

## 35. 常见风险

### 35.1 保存临时指针

禁止在最终 `FrameCapture` 中保存：

```text
GfxCommand*
CommandDraw*
RenderPass*
CompiledRenderPass*
MaterialDrawState 中 FrameAllocator 分配的 entries 指针
PipelineBindingSnapshotEntry*
RenderGraph transient ResourceNode*
```

必须深拷贝为 Capture 数据。

### 35.2 Capture 修改渲染语义

Capture 不能：

- 改变资源绑定顺序。
- 为了记录原因额外调用 `Bind()`。
- 改变 PSO lookup。
- 清空原本继承的 optional slot。
- 把 debugger copy 混入正常统计。

### 35.3 字符串开销泄漏到普通帧

以下操作只能在 Capture 时执行：

```cpp
std::format(...)
resource->Name()
shader->Name()
material->Name()
SlotToName(...)
```

### 35.4 全局锁

不要让 Draw 热路径写全局 vector。

### 35.5 PSO 切换原因丢失

PSO 切换会清空 root slot 和 VB/IB cache。Capture 必须把清空根因传递给下一次 bind 判断。

### 35.6 Hash 碰撞

正常路径仍可使用现有 binding hash 判断。

Capture 原因分析不能只比较 hash，必须比较完整 key。

### 35.7 输出历史内存爆炸

完整分辨率每事件 copy 很容易耗尽显存。必须：

- 默认 MetadataOnly。
- Thumbnail 默认关闭或手动启用。
- FullOutputHistory 显式开启。
- 有显存和 CPU 内存预算。
- 支持自动截断。

---

## 36. 建议的最小可用版本

最小可用版本只做：

```text
Capture 下一完整帧
RenderGraph Pass 树
Draw / Dispatch / Barrier 列表
Draw 基础参数
PSO dirty reason
PSO lookup hit/miss
Native PSO bind
VB/IB bind reason
Root slot bind/skip/reason
Inherited binding
Material binding cache reason
原生 UI 展示
```

暂不做：

```text
事件输出图像
Capture 导出
Diff
GPU timestamp
完整资源内容
```

这个版本已经能直接定位：

- Draw 为什么导致 PSO 重绑。
- 哪个 PSO 字段发生变化。
- PSO 是状态 dirty 还是 library miss。
- 哪个 root slot 重新绑定。
- root slot 是资源、GPU 地址、view 还是 subresource 变化。
- 哪些 slot 实际继承了上一 Draw。
- Material binding cache 为什么失效。
- 是否存在不必要的重复状态切换。

---

## 37. 最关键的三个插桩点

### 37.1 `CommandRecordingContext::MarkPSODirty(reason)`

负责保存 PSO 状态变化原因。

### 37.2 `D3DGraphicsPipelineState::BindImpl()`

负责保存：

- Native PSO bind
- Root slot bind/skip
- Binding invalid reason
- Inherited binding

### 37.3 `Material::CaptureDrawState()`

负责保存：

- Material binding cache invalid reason
- Full rebuild / global refresh / cache hit
- Global / Material / Command binding resolution

这三个位置分别对应 AiluEngine 当前三层核心缓存。原因必须在这些位置直接记录，不应在 Capture 完成后猜测。

---

## 38. 最终验收标准

实现完成后应满足：

1. 使用 AiluEngine 原生 UI 和 Dock 系统。
2. 不依赖 ImGui 实现 Frame Debugger 主界面。
3. 支持捕获下一完整帧。
4. 支持 RenderGraph Pass 和命令层级。
5. 支持 Draw、Dispatch、Barrier、Present。
6. 支持查看最终实际执行结果。
7. 支持 PSO dirty 原因。
8. 支持 PSO lookup hit/miss/not ready。
9. 支持 native PSO bind 原因。
10. 支持 VB/IB bind 原因。
11. 支持 graphics root slot bind/skip。
12. 支持 root slot 缓存失效字段级原因。
13. 支持 Material binding cache 原因。
14. 支持 inherited binding。
15. 支持 required but unbound 警告。
16. 多线程录制时事件顺序稳定。
17. Capture 完成后不持有帧临时指针。
18. 非 Capture 状态没有 Capture 内存分配。
19. 非 Capture 状态没有 Capture 字符串生成。
20. 非 Capture 状态没有 Capture 锁竞争。
21. Shipping 可通过编译开关完全移除。
