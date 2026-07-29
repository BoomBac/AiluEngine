# AiluEngine D3D12 渲染提交路径 CPU 优化方案

> 文档目标：结合 Visual Studio CPU Usage 诊断结果与 AiluEngine 当前源码，给出一套可分阶段实施、
> 可验证、可回滚的渲染提交路径优化方案。
>
> 重点范围：`Material::Bind`、`Shader::Bind`、`GraphicsPipelineStateMgr::FindMatchPSO`、
> `PipelineResource` 收集、D3D12 增量绑定、资源生命周期标记以及渲染线程等待策略。

---

## 1. 文档范围与源码快照

本文基于 AiluEngine 默认分支在分析时的源码快照，核心文件包括：

| 文件 | 主要职责 | 分析时 Blob SHA |
|---|---|---|
| `Engine/Src/Render/GraphicsPipelineStateObject.cpp` | PSO Hash、PSO 查找、资源集合归并 | `def3db95bdb32cdf68635f5a558a15b382621545` |
| `Engine/Inc/Render/GraphicsPipelineStateObject.h` | PSO 与 PSO Manager 数据结构 | `5f6e06aa5d791ba7c5c7fea2ca3cfc1087f2a391` |
| `Engine/Src/Render/Material.cpp` | 材质状态快照、纹理与资源绑定 | `469f5708353348ffdbdfd9d2624eedeec59b4239` |
| `Engine/Inc/Render/Material.h` | Material、BindState、属性容器 | `b44f94e7b1d44ac6da3689e9293858751fdb64ba` |
| `Engine/Src/Render/Shader.cpp` | Shader Variant、状态配置与 Shader Hash | `85b2b6c2c3287ae81b2d2b7d4e92ad233d402d41` |
| `Engine/Src/RHI/DX12/D3DContext.cpp` | GPU Command Worker 与 Draw Command 消费 | `af659c57434d15ee719408f196c39fb1b3644f80` |
| `Engine/Src/RHI/DX12/D3DGraphicsPipelineState.cpp` | D3D12 PSO 和 Root Slot 增量绑定 | `19ecd26bf5efb217ecaa1eb13e237a9c559847eb` |
| `Engine/Inc/RHI/DX12/D3DCommandBuffer.h` | Command Buffer 状态缓存和资源集合 | `4c13051e484712c0673f600ca1f4f93243757ace` |
| `Engine/Src/RHI/DX12/D3DCommandBuffer.cpp` | Upload Buffer、资源追踪和状态重置 | `359ebd70badddb1adb1a0a5d9d561d017dcba842` |
| `Engine/Inc/Render/CoreType.h` | `PipelineResource` 和 Shader 反射数据结构 | `1e9362ddc8d197938cd8b60fbe0ac8e09bde165c` |

后续代码若已发生变化，应优先对照“职责与数据流”而不是机械套用具体行号。

---

## 2. 诊断数据摘要

关闭 PIX 和 D3D12 Debug Layer 后，Visual Studio CPU Usage 中的主要路径为：

| 热点 | Total CPU |
|---|---:|
| `GpuCommandWorker::RunAsync` | 53.07% |
| `D3DContext::ProcessGpuCommand` | 33.17% |
| `GraphicsPipelineStateMgr::FindMatchPSO` | 6.65% |
| `GpuResource::Bind` | 4.76% |
| `Material::Bind` | 4.10% |
| `D3DContext::PresentImpl` | 3.11% |
| `GraphicsPipelineStateObject::SetPipelineResource` | 2.89% |
| `D3DComputeShader::Bind` | 2.12% |
| `PipelineResource::PipelineResource` | 约 1% |
| `D3DRenderTexture::StateTranslation` | 1.09% |
| `std::basic_string` | 1.05% |
| `D3DCommandBuffer::MarkUsedResource` | 1.01% |

`FindMatchPSO` 内部进一步出现：

- `GetPipelineResource`
- `ConstructPSOHash`
- `ExtractPSOHash`
- `ResourceMgr::Get<Shader>`
- `std::list<PipelineResource>::clear`
- `std::list<PipelineResource>::push_back`
- `PipelineResource` 构造、复制和析构
- `unordered_map`、`list` 迭代器和字符串操作

这说明热点不是某个复杂算法，而是每个 Draw 都重复执行大量描述性状态处理。

---

## 3. 诊断结论

### 3.1 核心矛盾

AiluEngine 已经实现了底层 D3D12 增量绑定：

- 相同 PSO 时跳过 `SetGraphicsRootSignature` 和 `SetPipelineState`
- 相同 VB/IB 时跳过重复绑定
- 相同 Root Slot Binding Hash 时跳过资源绑定

但这些判断发生得太晚。

在到达 `D3DGraphicsPipelineState::BindImpl()` 之前，每个 Draw 已经完成了：

1. 从 Material 队列中取状态并加锁
2. 查询 Shader Variant 状态
3. 重新配置 Shader 的所有 PSO 字段
4. 重新构造 Shader Hash
5. 为每个资源构造 `PipelineResource`
6. 向 `std::list` 分配节点并复制对象
7. 重新构造 PSO Hash
8. 从 Hash 中再次解析 Shader ID、Pass 和 Variant
9. 通过 `ResourceMgr` 再次获取 Shader
10. 遍历资源 List 并执行名称到 Slot 的解析
11. 按优先级合并资源
12. 重置并重新填充 PSO 的资源数组

因此当前架构属于：

```text
高层每 Draw 重建状态
        ↓
底层发现状态没变化
        ↓
跳过部分 D3D12 API
```

目标应改为：

```text
状态修改时更新缓存
        ↓
Draw Packet 直接携带已解析状态
        ↓
底层只执行必要的增量绑定
```

### 3.2 不应优先优化的项目

当前阶段不应首先投入大量时间优化：

- `BuildBindingHash()` 的几个 XOR
- `D3D12_RESOURCE_STATES` 枚举转换
- `PresentImpl()` 内部的等待
- 单个小型 getter 或 bitset 操作
- HashMap 本身的常数级性能

它们都不是当前结构性成本的来源。

---

## 4. 当前 Draw 提交路径

```text
CommandBuffer::DrawMesh
    ├─ Material::PushState
    └─ 生成 CommandDraw

GpuCommandWorker::RunAsync
    └─ D3DContext::ProcessGpuCommand
        └─ CommandDraw
            ├─ Material::Bind
            │   ├─ lock(_state_mutex)
            │   ├─ GetVariantState
            │   ├─ Shader::SetCullMode
            │   ├─ Shader::Bind
            │   │   ├─ ConfigureVertexInputLayout
            │   │   ├─ ConfigureRasterizerState
            │   │   ├─ ConfigureDepthStencilState
            │   │   ├─ ConfigureTopology
            │   │   ├─ ConfigureBlendState
            │   │   └─ ConstructHash + ConfigureShader
            │   ├─ 遍历 Material BindState 资源槽
            │   ├─ 构造 PipelineResource
            │   └─ SubmitBindResource → std::list::push_back
            │
            ├─ GraphicsPipelineStateMgr::FindMatchPSO
            │   ├─ 检查 Shader 编译队列
            │   ├─ 计算 RenderTarget Hash
            │   ├─ ConstructPSOHash
            │   ├─ _pso_library.find
            │   ├─ ExtractPSOHash
            │   ├─ ExtractInfoFromHash
            │   ├─ ResourceMgr::Get<Shader>
            │   ├─ ResetPipelineResources
            │   ├─ 遍历 _bind_resource_list
            │   ├─ NameToSlot
            │   ├─ 优先级归并
            │   └─ _bind_resource_list.clear
            │
            ├─ 绑定全局/材质/对象 CBuffer
            └─ PSO::Bind
                ├─ PSO 增量检查
                ├─ Root Slot Binding Hash
                ├─ GpuResource::Bind
                ├─ MarkUsedResource
                └─ D3D12 Draw
```

这个调用链中，前半段存在大量可以提前缓存或直接消除的步骤。

---

## 5. 优化目标

### 5.1 主要目标

1. 将 `FindMatchPSO()` 的常规命中路径压缩为一次 dirty 判断和一次 HashMap 查找。
2. 消除每 Draw 的 `std::list` 节点分配。
3. 消除每 Draw 的字符串名称到 Slot 查找。
4. 消除每 Draw 的 Shader ID 反解析和 `ResourceMgr` 查询。
5. 将 Material 状态从“内部队列 + Mutex”改为显式 Draw Packet。
6. 相同材质数据在一帧内只上传一次。
7. 让 Shader 热重载不破坏稳定的逻辑属性 ID。
8. 降低编辑器闲置时 RenderThread 的忙等 CPU。
9. 保持现有 RHI 接口可逐步迁移，不要求一次性重写。

### 5.2 非目标

第一阶段不要求：

- 立即全面切换 Bindless
- 重写整个 RenderGraph
- 改成多 Command List 并行录制
- 引入复杂的 Pipeline Library 持久化
- 一次性消除所有字符串 API
- 修改所有材质资产格式

---

# 第一阶段：低风险热路径瘦身

## 6. 建立正确的 Profiling 配置

当前诊断模块名称为 `engine_d`，并出现 `_RTC_CheckStackVars`、Debug STL 迭代器等符号。

Debug 构建适合识别调用结构，不适合衡量最终收益。建议增加专用 `Profile` 配置：

```text
Optimization                 /O2
Inline Function Expansion    /Ob2
Debug Information Format     /Zi
Linker Generate Debug Info   /DEBUG
Runtime Checks               关闭 /RTC
Runtime Library              /MD
_ITERATOR_DEBUG_LEVEL        0
PIX                          关闭
D3D12 Debug Layer            关闭
GPU Validation               关闭
```

应保留 PDB，确保采样器可以显示完整函数名。

建议固定一组测试场景：

| 场景 | 用途 |
|---|---|
| 空场景、编辑器静置 | 测量线程忙等和 UI 基线 |
| 100 个相同材质 Mesh | 测量状态缓存命中 |
| 1000 个相同材质 Mesh | 测量单 Draw 固定成本 |
| 1000 个不同材质 Mesh | 测量材质资源绑定 |
| 大量 Shadow Caster | 测量多 Pass 和排序 |
| 材质/Shader 热重载 | 验证缓存失效正确性 |

每次改造记录：

- CPU Frame Time
- RenderThread Time
- Draw Count
- PSO Lookup Count
- PSO Cache Hit/Miss
- Material Resolve Count
- Resource Submit Count
- Actual D3D12 PSO Bind Count
- Actual D3D12 Root Slot Bind Count
- CBuffer Upload Bytes
- `MarkUsedResource` 调用与去重后数量

---

## 7. `FindMatchPSO()` 增加 Dirty Fast Path

### 7.1 当前问题

`ConfigureShader()`、`ConfigureRasterizerState()` 等接口当前无条件覆盖 Hash 字段。

`FindMatchPSO()` 每 Draw 都重新：

- 计算 RenderTarget Hash
- 构造 PSO Hash
- 查询缓存
- 解包 Hash
- 获取 Shader
- 重建资源集合

即使连续 Draw 使用完全相同的 PSO，也会重复执行完整流程。

### 7.2 建议数据结构

```cpp
class GraphicsPipelineStateMgr
{
public:
    void ConfigureShader(Shader* shader, u64 shader_hash);
    void ConfigureVertexInputLayout(u8 input_layout_hash);
    void ConfigureBlendState(u8 blend_state_hash);
    void ConfigureRasterizerState(u8 raster_state_hash);
    void ConfigureDepthStencilState(u8 depth_stencil_state_hash);
    void ConfigureRenderTarget(u8 render_target_state_hash);

    GraphicsPipelineStateObject* FindMatchPSO();

private:
    PSOHash BuildCurrentPsoHash() const;

private:
    GraphicsPipelineStateObject* _current_pso = nullptr;
    Shader* _current_shader = nullptr;
    PSOHash _current_pso_hash{};

    u64 _shader_hash = 0u;
    u8 _input_layout_hash = 0u;
    u8 _blend_state_hash = 0u;
    u8 _raster_state_hash = 0u;
    u8 _depth_stencil_state_hash = 0u;
    u8 _render_target_state_hash = 0u;

    bool _pso_dirty = true;
};
```

### 7.3 Setter 只在状态变化时标脏

```cpp
void GraphicsPipelineStateMgr::ConfigureShader(Shader* shader, u64 shader_hash)
{
    if (_current_shader == shader && _shader_hash == shader_hash)
        return;

    _current_shader = shader;
    _shader_hash = shader_hash;
    _pso_dirty = true;
}

void GraphicsPipelineStateMgr::ConfigureRasterizerState(u8 raster_state_hash)
{
    if (_raster_state_hash == raster_state_hash)
        return;

    _raster_state_hash = raster_state_hash;
    _pso_dirty = true;
}
```

其余状态同理。

### 7.4 常规命中路径

```cpp
GraphicsPipelineStateObject* GraphicsPipelineStateMgr::FindMatchPSO()
{
    if (!_pso_dirty)
        return _current_pso;

    _current_pso_hash = BuildCurrentPsoHash();

    const auto iter = _pso_library.find(_current_pso_hash);
    if (iter != _pso_library.end() && iter->second->IsReady())
    {
        _current_pso = iter->second.get();
        _pso_dirty = false;
        return _current_pso;
    }

    _current_pso = FindOrCreatePso(_current_pso_hash);
    _pso_dirty = _current_pso == nullptr;
    return _current_pso;
}
```

### 7.5 注意事项

`_current_pso` 的复用只代表 PSO 对象不变，不代表绑定资源不变。

因此应该把：

- PSO 选择状态
- Draw 资源绑定状态

拆成两个独立缓存，不能因为 PSO 未变化就跳过 Material Binding 更新。

---

## 8. Shader 编译队列移出每 Draw 路径

`FindMatchPSO()` 每次调用都检查 `_shader_compiled_queue`，而 Shader 编译完成是低频事件。

推荐在 RenderThread 帧边界执行：

```cpp
void GpuCommandWorker::EndFrame()
{
    GraphicsPipelineStateMgr::Get().ProcessPendingShaderCompiles();
    RenderPipeline::Get().FrameCleanup();
    RenderingStates::Reset();
}
```

热重载处理时应：

- 新建 Shader Variant Layout
- 新建或重建相关 PSO
- 递增 Shader Layout Version
- 将当前 PSO 标记为 dirty
- 不立即销毁仍被旧 Command 使用的数据

---

## 9. 移除 `FindMatchPSO()` 中的 Shader 反解析

当前路径大致为：

```text
Shader*
→ Shader::ConstructHash(shader_id, pass, variant)
→ PSO Hash
→ ExtractPSOHash
→ Shader::ExtractInfoFromHash
→ shader_id
→ ResourceMgr::Get<Shader>(shader_id)
```

这是不必要的往返转换。

`ConfigureShader()` 应直接接收并缓存 Shader 指针：

```cpp
void GraphicsPipelineStateMgr::ConfigureShader(Shader* shader, u16 pass_index,
                                                ShaderVariantHash variant_hash)
{
    const u64 shader_hash = shader->GetPipelineHash(pass_index, variant_hash);
    ConfigureShader(shader, shader_hash);
}
```

Hash 用于缓存 Key 和比较，不应用于重新寻找已有对象。

---

## 10. Shader Hash 预计算

`Shader::Bind()` 每 Draw 调用的 Shader Hash 只依赖 Shader、Pass 和 Variant。

建议：

```cpp
struct ShaderVariant
{
    ShaderVariantHash _variant_hash = 0u;
    u64 _pipeline_shader_hash = 0u;
};
```

Variant 构建时：

```cpp
variant._pipeline_shader_hash = Shader::ConstructHash(_id, pass_index, variant_hash);
```

绑定时直接读取。

Topology 当前并未包含在 PSO Hash 中，需要明确：

- 如果 `PrimitiveTopologyType` 可能变化，应纳入 PSO Key。
- 如果只有 `IASetPrimitiveTopology` 变化，可单独作为动态状态缓存。
- 不能保留“配置了但 Hash 不使用”的模糊状态。

---

## 11. RenderTarget Hash 改为状态变化时计算

RenderTarget 格式只会在 `SetRenderTarget`、MRT 数量变化和 Depth 格式变化时改变。

建议在修改 RenderTarget State 时更新 Hash：

```cpp
void GraphicsPipelineStateMgr::UpdateRenderTargetHash()
{
    const u8 new_hash = RenderTargetState::_s_hash_obj.GenHash(_render_target_state);
    if (_render_target_state_hash == new_hash)
        return;

    _render_target_state.Hash(new_hash);
    _render_target_state_hash = new_hash;
    _pso_dirty = true;
}
```

不要在 `FindMatchPSO()` 中每 Draw 重新生成。

---

## 12. `std::list<PipelineResource>` 改为固定槽位数组

`PipelineResource` 包含 `String`，而 Root Slot 最大数量已经是 32，使用 List 会产生不必要的节点分配。

```cpp
struct PendingPipelineBindings
{
    Array<PipelineResource, 32> _resources;
    u32 _mask = 0u;
    u16 _max_slot = 0u;

    void Reset()
    {
        _mask = 0u;
        _max_slot = 0u;
    }

    void Submit(const PipelineResource& resource)
    {
        AL_ASSERT(resource._slot < 32u);

        const u16 slot = resource._slot;
        const u32 slot_mask = 1u << slot;

        if ((_mask & slot_mask) == 0u || resource._priority >= _resources[slot]._priority)
        {
            _resources[slot] = resource;
            _resources[slot]._slot = slot;
        }

        _mask |= slot_mask;
        _max_slot = std::max(_max_slot, slot);
    }
};
```

命名资源必须先解析成 Slot，再进入该容器。长期应让热路径只接收已解析资源。

---

## 13. `ResetPipelineResources()` 只清 Mask

```cpp
void GraphicsPipelineStateObject::ResetPipelineResources()
{
    _bind_res_signature = 0u;
    _max_slot = 0u;
}
```

下一次 `SetPipelineResource()` 会覆盖有效槽位，不需要对旧 `String` 调用 `clear()`。

前提是所有读取 `_bind_res` 的地方都先检查 `_bind_res_signature`。

---

## 14. 优化 PSO Hash 构造

Debug 范围断言应从 `pow()` 改为整数位运算：

```cpp
AL_ASSERT(input_layout < (1u << StateHashStruct::kInputLayout._size));
AL_ASSERT(shader < (1ull << StateHashStruct::kShader._size));
```

长期可以将 PSO Key 改为简单 POD：

```cpp
struct GraphicsPsoKey
{
    u64 _low = 0u;
    u64 _high = 0u;

    bool operator==(const GraphicsPsoKey& other) const = default;
};
```

避免热路径中反复使用通用 Hash 类的 `Set()`/`Get()`。

---

## 15. 删除 PSO Miss 中无实际作用的全库遍历

PSO Miss 分支中存在遍历 `_pso_library` 查找相同 Shader Hash 的代码，但实际只剩注释，应删除。

PSO Miss 路径只应执行：

1. 检查 Variant 是否 Ready
2. 构造 Initializer
3. 创建或排队创建 PSO
4. 返回空或 fallback PSO

---

# 第二阶段：Material Draw Packet 化

## 16. 移除 Material 状态队列与 Mutex

当前模型：

```text
记录线程：Material::PushState()
执行线程：Material::Bind() → _states.front() → _states.pop()
```

建议让 `CommandDraw` 直接持有不可变 Draw State：

```cpp
struct ResolvedPipelineResource
{
    GpuResource* _resource = nullptr;
    EBindResDescType _resource_type = EBindResDescType::kUnknown;
    u16 _slot = 0u;
    u16 _priority = 0u;
    PipelineResource::AddiInfo _additional_info;
};

struct MaterialDrawState
{
    Shader* _shader = nullptr;
    u16 _pass_index = 0u;
    ShaderVariantHash _variant_hash = 0u;
    u64 _pipeline_shader_hash = 0u;

    u32 _binding_mask = 0u;
    Array<ResolvedPipelineResource, 32> _bindings;

    const u8* _property_data = nullptr;
    u32 _property_size = 0u;
    u32 _material_version = 0u;
};
```

可以移除：

- `Material::_state_mutex`
- `Material::_states`
- RenderThread 每 Draw 加锁
- Material 队列顺序与 Draw Command 顺序的隐式耦合

Draw State 必须保证 Material、Shader、Layout 和 Property Data 的生命周期。

---

## 17. Material Binding Cache

材质绑定集合只在以下情况变化：

- `SetTexture`
- `SetBuffer`
- `RemoveTexture`
- Shader 切换
- Keyword/Variant 改变
- Shader 热重载
- 反射布局变化

建议每 Pass/Variant 缓存：

```cpp
struct MaterialPassBindingCache
{
    const ShaderBindingLayout* _layout = nullptr;
    u32 _layout_version = 0u;
    u32 _material_version = 0u;
    ShaderVariantHash _variant_hash = 0u;
    u64 _pipeline_shader_hash = 0u;

    u32 _binding_mask = 0u;
    Array<ResolvedPipelineResource, 32> _bindings;
};
```

只有缓存失效时才重新解析属性、类型和 Slot。

---

## 18. 热重载安全的 `ShaderPropertyId`

### 18.1 核心原则

```text
ShaderPropertyId
    稳定的逻辑属性身份，不因 Shader 热重载变化

ShaderPropertyBinding
    当前 Pass/Variant 的 Slot、Offset、Type，可随热重载变化
```

`ShaderPropertyId` 不能等于 Root Slot、Reflection 下标或反射对象指针。

### 18.2 进程级 Name Registry

```cpp
using ShaderPropertyId = u32;

inline constexpr ShaderPropertyId kInvalidShaderPropertyId = 0u;

class ShaderPropertyRegistry
{
public:
    static ShaderPropertyRegistry& Get();

    ShaderPropertyId Intern(const String& name);
    const String& GetName(ShaderPropertyId property_id) const;

private:
    std::mutex _mutex;
    HashMap<String, ShaderPropertyId> _name_to_id;
    Vector<String> _id_to_name;
};
```

外部按值使用：

```cpp
inline const ShaderPropertyId kBaseMapId =
        ShaderPropertyRegistry::Get().Intern("_BaseMap");

material.SetTexture(kBaseMapId, texture);
```

不需要传引用。热重载只会改变 `PropertyId → Slot` 的映射，不改变 Property ID。

### 18.3 Shader Binding Layout

```cpp
struct ShaderPropertyBinding
{
    ShaderPropertyId _property_id = kInvalidShaderPropertyId;
    EBindResDescType _resource_type = EBindResDescType::kUnknown;
    i16 _bind_slot = -1;
    u32 _buffer_offset = 0u;
    u32 _buffer_size = 0u;
};

class ShaderBindingLayout
{
public:
    u32 Version() const
    {
        return _version;
    }

    const ShaderPropertyBinding* Find(ShaderPropertyId property_id) const
    {
        const auto iter = _bindings.find(property_id);
        return iter == _bindings.end() ? nullptr : &iter->second;
    }

private:
    u32 _version = 0u;
    HashMap<ShaderPropertyId, ShaderPropertyBinding> _bindings;
};
```

热重载创建新 Layout，不原地修改旧 Layout。

Draw Command 建议持有：

```cpp
Ref<const ShaderBindingLayout> _binding_layout;
```

这样旧命令仍可安全使用旧 Layout。

---

## 19. Material 属性容器改为 Property ID

```cpp
HashMap<ShaderPropertyId, Texture*> _texture_properties;
HashMap<ShaderPropertyId, GPUBuffer*> _buffer_properties;
```

接口：

```cpp
void Material::SetTexture(ShaderPropertyId property_id, Texture* texture)
{
    const auto iter = _texture_properties.find(property_id);
    if (iter != _texture_properties.end() && iter->second == texture)
        return;

    _texture_properties[property_id] = texture;
    ++_property_version;
}
```

保留字符串兼容接口：

```cpp
void Material::SetTexture(const String& name, Texture* texture)
{
    SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture);
}
```

资产序列化仍建议保存字符串，不直接保存运行时 `u32` ID。

---

## 20. 内建 CBuffer 使用固定语义

当前 Draw 路径会遍历 `HashMap<String, UploadBuffer::Allocation>` 并逐项执行字符串验证和 Slot 查找。

建议：

```cpp
enum class EBuiltinCBuffer : u8
{
    kPerScene,
    kPerCamera,
    kPerObject,
    kPerMaterial,
    kCount
};

struct BuiltinCBufferBinding
{
    UploadBuffer::Allocation _allocation;
    bool _valid = false;
};
```

Shader Layout 编译时解析内建语义到 Root Slot，执行 Draw 时按固定数组和 Mask 绑定。

---

## 21. 材质 CBuffer 一帧只上传一次

当前相同材质的每个 Draw 都重新分配和复制 Material CBuffer。

建议使用：

```text
Material* + Material Version
    → 当前 Frame/CommandBuffer 的 GPU Address
```

材质属性改变时递增 `_property_version`。

缓存必须是 Frame 或 CommandBuffer 级别，不能跨 Upload Buffer Reset 长期保存 GPU Address。

---

# 第三阶段：资源绑定与生命周期优化

## 22. `MarkUsedResource()` 从 `std::set` 改为 Stamp + Vector

在 `GpuResource` 中增加：

```cpp
u64 _last_marked_command_id = 0u;
```

CommandBuffer 使用：

```cpp
Vector<GpuResource*> _used_resources;
```

```cpp
void D3DCommandBuffer::MarkUsedResource(GpuResource* resource)
{
    if (resource == nullptr)
        return;

    if (resource->_last_marked_command_id == _id)
        return;

    resource->_last_marked_command_id = _id;
    _used_resources.emplace_back(resource);
}
```

多线程录制前需要重新设计 Stamp 的同步或使用本地开放寻址集合。

---

## 23. Root Signature 与 PSO 状态缓存分离

当前 PSO 指针变化会清空全部 Slot Hash。

可以额外缓存 Root Signature：

```cpp
const void* _root_signature = nullptr;
```

只有 Root Signature 变化时清空 Root Slot Cache。相同 Root Signature 下切换不同 PSO，可以保留兼容资源绑定。

必须验证：

- Root Parameter 布局完全一致
- Descriptor Heap 未切换
- Slot 类型兼容
- Root Signature Flags 一致

---

## 24. RenderThread 空队列改为阻塞等待

`std::this_thread::yield()` 仍是忙等。

建议使用 `std::counting_semaphore`、`condition_variable` 或 `atomic::wait/notify`：

```cpp
void GpuCommandWorker::Push(Vector<GfxCommand*>&& commands, SubmitParams&& params)
{
    while (!_cmd_queue.Push(CommandGroup(std::move(commands), std::move(params))))
        std::this_thread::yield();

    _command_semaphore.release();
}
```

Stop 时必须唤醒等待线程。

该优化主要降低：

- 编辑器空闲 CPU
- RenderThread Ready Time
- 不必要的线程争抢

---

## 25. RenderGraph 与资源状态转换

长期建议按资源来源区分：

| 资源 | 状态管理 |
|---|---|
| 静态纹理 | 上传后长期保持 Shader Resource |
| RDG 临时资源 | Pass 边界统一生成 Barrier |
| 非 RDG 动态资源 | `D3DResourceStateGuard` 隐式管理 |
| External 资源 | 外部显式声明 |

避免 RDG 和 Bind 阶段同时作为状态真值来源。

---

# 第四阶段：Bindless 材质资源

AiluEngine 已有 Bindless SRV/UAV Index，可将普通材质纹理放入 Material GPU Data：

```text
Material GPU Data
    ├─ albedo_texture_index
    ├─ normal_texture_index
    ├─ metallic_texture_index
    └─ sampler_index
```

推荐混合绑定：

| 资源类型 | 推荐方式 |
|---|---|
| 普通材质纹理 | Bindless Index |
| Per Scene/Camera/Object/Material CBuffer | Root CBV 或固定 Descriptor |
| RDG 临时纹理 | 显式 Slot 或临时 Bindless Index |
| UAV 写资源 | 显式或 Bindless UAV |
| AS | 专门 Root 参数 |
| Sampler | Static Sampler 或 Bindless Sampler |

Bindless 应放在前面缓存改造完成后实施，因为它涉及 Descriptor 生命周期和资产升级。

---

# 26. Draw 排序策略

当前已有基于 Render Queue、Shader、Pass、Variant、Material、Mesh 的排序。

建议：

### Opaque

```text
PSO Key
→ Root Signature
→ Material Binding Set
→ VB/IB
→ Depth Bucket
```

### Transparent

保持大致 Back-to-Front：

```text
Depth Bucket
→ PSO
→ Material
```

### Shadow

```text
Shadow PSO
→ Vertex Layout
→ Mesh
→ Alpha Test Variant
```

排序 Key 建议在生成 `QueuedDrawItem` 时预计算，不在比较器中反复访问复杂对象。

---

# 27. 文件级改造计划

## `GraphicsPipelineStateObject.h/.cpp`

- 增加 Current PSO、Current Shader 和 Dirty Flag
- Configure 接口比较旧值
- 拆分 `FindMatchPSO`
- List 改固定槽位数组
- Reset 只清 Mask
- 删除无效全库遍历

## `Shader.h/.cpp`

- Variant 缓存 Pipeline Shader Hash
- 引入 `ShaderBindingLayout`
- Reflection 输出 Property ID
- 热重载创建新 Layout
- 明确 Topology 的 PSO Key 归属

## `Material.h/.cpp`

- 增加 `_property_version`
- 增加 Pass Binding Cache
- 引入 `CaptureDrawState`
- 移除 State Queue 和 Mutex
- String 属性逐步迁移到 Property ID

## `D3DContext.cpp`

- Draw 分支应用 `MaterialDrawState`
- 移除每 Draw 遍历 `_allocations`
- Material CBuffer 使用帧内缓存
- Shader 编译处理移至帧边界
- RenderThread 改阻塞等待

## `D3DCommandBuffer.h/.cpp`

- Used Resource 改 Stamp + Vector
- 增加 Material Upload Cache
- 增加 Builtin CBuffer 数组
- Root Signature 与 PSO Cache 分离

## `CoreType.h`

长期将：

```cpp
PipelineResource
```

拆成：

```cpp
NamedPipelineResource
ResolvedPipelineResource
```

热路径只允许 `ResolvedPipelineResource`，从类型系统上禁止 String 进入 RHI。

---

# 28. 建议实施顺序

## Milestone 0：基准

- [ ] 创建 Profile 配置
- [ ] 固定测试场景
- [ ] 添加统计计数器
- [ ] 保存改造前 Diagsession

## Milestone 1：PSO Fast Path

- [ ] Configure 变化时标脏
- [ ] RenderTarget Hash 增量更新
- [ ] Shader Hash 预计算
- [ ] 移除 Shader 反解析和 ResourceMgr 查询
- [ ] Shader 编译移至帧边界
- [ ] 删除 PSO Miss 无效遍历

## Milestone 2：固定槽位资源

- [ ] List 改固定数组
- [ ] Submit 时直接按 Slot/Priority 合并
- [ ] Reset 只清 Mask
- [ ] 移除 Draw 热路径 String

## Milestone 3：Material Cache

- [x] Material Property Version
- [x] Pass Binding Cache
- [x] SetTexture/SetBuffer 标脏
- [x] Material CBuffer 每帧一次上传

## Milestone 4：Draw Packet

- [ ] CommandDraw 持有 MaterialDrawState
- [ ] 移除 Material Queue 和 Mutex
- [ ] 处理 Material/Shader/Layout 生命周期

## Milestone 5：资源追踪和线程

- [x] Set 改 Stamp + Vector
- [x] RenderThread 改 Semaphore
- [ ] 测量编辑器空闲 CPU

## Milestone 6：Property ID 与热重载

- [x] Property Registry
- [x] Shader Binding Layout
- [x] Material 属性改 Property ID
- [x] 热重载生成新 Layout
- [x] Draw State 持有 Layout 引用

## Milestone 7：Bindless

- [ ] Material GPU Data 保存纹理 Index
- [ ] Descriptor Index 延迟回收
- [ ] 普通材质纹理移出 Root Slot

---

# 29. 建议新增统计指标

```cpp
struct RenderSubmissionStats
{
    u64 _draw_command_count = 0u;
    u64 _pso_lookup_count = 0u;
    u64 _pso_cache_hit_count = 0u;
    u64 _pso_cache_miss_count = 0u;
    u64 _pso_dirty_count = 0u;

    u64 _material_capture_count = 0u;
    u64 _material_binding_resolve_count = 0u;
    u64 _material_binding_cache_hit_count = 0u;

    u64 _pipeline_resource_submit_count = 0u;
    u64 _pipeline_resource_override_count = 0u;
    u64 _actual_root_slot_bind_count = 0u;
    u64 _skipped_root_slot_bind_count = 0u;

    u64 _material_cbuffer_upload_count = 0u;
    u64 _material_cbuffer_upload_bytes = 0u;
    u64 _material_cbuffer_cache_hit_count = 0u;

    u64 _resource_mark_request_count = 0u;
    u64 _unique_resource_mark_count = 0u;
};
```

编辑器面板建议显示：

- PSO Hit Rate
- Material Binding Cache Hit Rate
- Root Slot Skip Rate
- Average Resource Submit Per Draw
- Material CBuffer Reuse Rate
- Unique Resource Mark Ratio

---

# 30. 验收标准

## 第一阶段

- `ResourceMgr::Get<Shader>` 不出现在 Draw 热路径
- `std::list<PipelineResource>` 不出现在调用树
- `std::basic_string` 在 Draw 热路径显著下降
- PSO Cache Hit Rate 高于 99%
- PSO Dirty Count 接近实际状态切换次数

## 第二阶段

- `Material::Bind()` 不再加 Mutex
- 相同材质一帧只解析一次 Binding
- 相同材质 PropertyBlock 一帧只上传一次
- Material Binding Cache Hit Rate 高于 95%

## 第三阶段

- `MarkUsedResource` 不再使用树节点分配
- 空场景 RenderThread 接近阻塞状态
- 编辑器静置 CPU 明显下降

## Bindless 阶段

- 普通材质纹理不再产生 Root Descriptor Table Bind
- Root Slot Bind Count 与材质纹理数量基本解耦

---

# 31. 风险与回滚

建议增加 Feature Flag：

```text
EnablePsoDirtyFastPath
EnableResolvedMaterialBindings
EnableMaterialUploadCache
EnableCommandBufferResourceStamp
EnableRenderThreadSemaphore
EnableBindlessMaterialTextures
```

Debug 模式可双路径计算旧、新 PSO Hash 和绑定集合，执行前做一致性断言。

Shader 热重载必须满足：

1. 新 Shader 编译成功
2. 新 Reflection 成功
3. 新 Layout 构建成功
4. 必要 PSO 创建成功或进入 Pending
5. 才替换 Active Variant

失败时保留旧 Shader 和旧 Layout。

---

# 32. 建议提交拆分

1. `profile: add renderer submission counters`
2. `render: add pso dirty fast path`
3. `render: move shader compile processing out of draw path`
4. `render: replace pending pipeline resource list with fixed slots`
5. `render: remove per-slot clear from pso resource reset`
6. `render: cache shader variant pipeline hash`
7. `render: add material pass binding cache`
8. `render: capture material state in draw commands`
9. `render: cache per-frame material constant buffer uploads`
10. `dx12: replace used resource set with command stamp`
11. `render: block gpu command worker on empty queue`
12. `render: add hot-reload-safe shader property registry`
13. `render: migrate material properties to shader property ids`
14. `dx12: add bindless material texture path`

---

# 33. 最终目标架构

```text
Material API
    SetTexture(ShaderPropertyId, Texture*)
    SetFloat(ShaderPropertyId, value)
        ↓
Material Logical Properties
    PropertyId → Value
    Material Version
        ↓ 仅缓存失效时
ShaderBindingLayout
    PropertyId → Slot/Offset/Type
        ↓
MaterialPassBindingCache
    已解析 Slot
    Binding Mask
    Pipeline Shader Hash
        ↓ 记录 Draw 时
MaterialDrawState
    不可变状态快照
    Layout 引用
    Binding Set
    Material Version
        ↓ RenderThread
PSO Dirty Fast Path
    状态未变化：直接复用 Current PSO
    状态变化：一次缓存查询
        ↓
D3D12 Incremental Binding
    PSO / Root Signature / Slot Hash
        ↓
DrawIndexedInstanced / DrawInstanced
```

---

# 34. 结论

AiluEngine 当前不是缺少状态缓存，而是缓存层级偏低：

- D3D12 API 层已经能够跳过部分重复绑定
- CPU 上游仍然为每个 Draw 重建完整描述状态
- 字符串、List 节点、Hash 解包和资源归并消耗被分散到多个小函数
- 因此采样中没有一个极端 Self CPU 热点，但整条提交链累计成本较高

最优先的改造顺序是：

1. `FindMatchPSO()` Dirty Fast Path
2. 固定槽位 Pending Binding
3. 移除 Draw 热路径字符串和 `ResourceMgr` 查询
4. Material Pass Binding Cache
5. Draw Packet 替代 Material Queue + Mutex
6. 材质 CBuffer 帧内复用
7. Resource Stamp + Vector
8. RenderThread 阻塞等待
9. 热重载安全的 ShaderPropertyId/Layout
10. Bindless 材质纹理

完成前两阶段后，应重新采集 Profile 构建的 `.diagsession`，再根据新的热点决定是否继续优化：

- Command 构造与分发
- Upload Buffer
- Descriptor Commit
- Resource State Tracking
- Draw Call 数量和实例化
- RenderGraph Barrier
