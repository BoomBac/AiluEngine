# AiluEngine RenderGraph Compile Cache 改造任务

## 目标

目前 `RenderGraph` 每帧都会重新构建并执行 `Compile()`。

希望改为：

- RenderGraph **结构未变化时复用编译结果**
- 只有 Pass / Resource dependency / access topology 发生变化时重新 Compile
- 分离：
  - 静态 Graph 拓扑
  - 每帧动态资源和参数
- 不重写现有 RenderGraph，只在当前架构上做最小改造

---

# 1. 先消除当前重复 Compile

目前流程中：

```text
OnRecordRenderGraph
→ Compile
→ Add FinalBlit
→ Add PreparePresentation
→ Execute
→ Compile again
```

因为 `AddPass()` 会设置：

```cpp
_is_compiled = false;
```

### 修改

删除 `BeginScene()` / Record 完普通 Pass 后的显式：

```cpp
_rd_graph->Compile();
```

统一让所有 Pass，包括：

```text
FinalBlit
PreparePresentation
```

全部添加完成后，由 `Execute()` 触发一次 Compile。

### 验收

每次完整 Render：

```text
Compile <= 1 次
```

---

# 2. CompiledBarrier 不再保存物理资源指针

当前：

```cpp
struct CompiledResourceBarrier
{
    GpuResource *_resource;
    EResourceState _before;
    EResourceState _after;
    u32 _sub_resource;
};
```

这会导致 Compile Result 与每帧 transient physical resource 强绑定。

### 修改为

```cpp
struct CompiledResourceBarrier
{
    RGHandle _handle;
    EResourceState _before = EResourceState::kCommon;
    EResourceState _after = EResourceState::kCommon;
    u32 _sub_resource = kTotalSubRes;
};
```

Execute 时再 Resolve：

```cpp
auto *resource = Resolve<GpuResource>(barrier._handle);
cmd->ResourceBarrier(resource, barrier._before, barrier._after, barrier._sub_resource);
```

### 要求

Compile Result 中尽量不能保存：

```text
GpuResource*
Texture*
RenderTexture*
Buffer*
```

等可能随帧变化的物理对象。

---

# 3. Compile 阶段禁止创建 Physical Resource

当前 `CompileResourceBarriers()` 中存在：

```cpp
CreatePhysicalResources(handle);
```

### 修改

Compile 只计算：

```text
dependency
topological order
resource lifetime
resource state transition
barrier plan
```

不要创建/租赁真正的 transient resource。

Physical resource 的：

```text
创建
复用
resize
Resolve
```

放到 Execute 前的 Prepare 阶段或 Execute 阶段。

目标生命周期：

```text
Compile
    ↓
logical graph only

PrepareFrame
    ↓
resolve / allocate physical resources

Execute
```

---

# 4. EndFrame 不再销毁 Graph 编译结果

当前 `EndFrame()` 会清空：

```text
_passes
_sorted_passes
_compiled_passes
_transient_resources
resource versions
```

导致下一帧必然重建。

### 修改原则

将数据分成两类。

## Persistent Graph Data

结构不变时跨帧保留：

```text
Pass declarations
Resource nodes
Resource versions
Dependency graph
Sorted passes
Compiled passes
Compiled barriers
```

## Frame Data

每帧允许清理：

```text
physical transient resource binding
external resource binding
temporary execution state
frame-local resource allocation
```

`EndFrame()` 只清理 Frame Data。

增加独立的 Graph reset/rebuild 接口，例如：

```cpp
void ResetGraph();
```

只有 Graph Dirty 时才真正销毁结构数据。

---

# 5. 增加 Graph Dirty 机制

增加类似：

```cpp
bool _graph_dirty = true;
```

或者沿用 `_is_compiled`，但要明确：

```text
Frame End ≠ Graph Dirty
```

Graph Structure 改变时：

```cpp
_graph_dirty = true;
```

Execute：

```cpp
if (_graph_dirty)
{
    Compile();
    _graph_dirty = false;
}
```

---

# 6. Graph Dirty 的判定范围

不是只有 Pass 数量变化才算 Dirty。

以下情况均属于 Graph Structure 改变：

```text
Add / Remove Pass

Read / Write dependency 改变

Resource access 类型改变

Subresource topology 改变

Mip 数导致 Pass 数量变化

AO/Fog 等可选 dependency 开关

Render feature 开关导致 Pass topology 改变
```

例如：

```text
SSAO on/off
GI on/off
Fog on/off
Cloud tile mode
debug pass
Bloom iteration count
HZB pass count
```

都可以暂时直接触发完整 Graph rebuild。

第一版不需要实现复杂增量 Compile。

---

# 7. 避免 Runtime Condition 改变 Graph

尽量把：

```cpp
if (runtime_data.empty())
    return; // 不 AddPass
```

改成：

```text
Pass 始终存在
Execute 时发现无数据直接 return
```

例如 Sprite Pass：

```cpp
if (_render_data.empty())
    return;
```

优先改成 Graph 中始终存在 Sprite Pass：

```cpp
Execute(...)
{
    if (_render_data.empty())
        return;

    ...
}
```

这样普通场景数据变化不会导致 Graph Dirty。

只对容易处理的 Pass 修改，不要求这次一次性覆盖所有 RenderPass。

---

# 8. 修复 Execute Callback 捕获 Frame Data

检查所有：

```cpp
builder.AddPass(..., [xxx](...) {});
```

如果 lambda 捕获的是每帧变化数据，例如：

```text
width
height
frame index
temporal offset
ping-pong state
dynamic parameter
```

不能让它永久冻结在首次 Graph Build 的值。

典型包括：

```text
VolumetricCloud cur_offset
SSAO width / height
GI tile frame
Temporal resource ping-pong
```

### 推荐方式

执行时从：

```text
RenderingData
RenderPass member state
RenderGraph frame context
```

读取最新值。

例如不要：

```cpp
const auto cur_offset = GetFrameOffset();

builder.AddPass(..., [cur_offset](...) {
    Use(cur_offset);
});
```

改为：

```cpp
builder.AddPass(..., [this](...) {
    const auto cur_offset = GetFrameOffset();
    Use(cur_offset);
});
```

前提是该成员生命周期稳定。

---

# 9. Temporal Ping-Pong 暂时处理

当前 Fog / Cloud / GI 等可能每帧交换：

```text
Texture A
Texture B
```

如果现有 `Import(Texture*)` 会把真实 Texture identity 固化到 RGHandle，则会阻碍 Graph Cache。

### 第一版方案

增加 external resource rebinding 能力。

Graph 中保持逻辑 Handle：

```text
CloudHistory
CloudCurrent
```

每帧仅重新绑定：

```text
CloudHistory → A/B
CloudCurrent → B/A
```

不要因为 A/B 交换重新 Compile。

可以增加类似：

```cpp
BindExternalResource(RGHandle handle, GpuResource *resource);
```

具体接口根据现有 RenderGraph 风格实现，不强制命名。

---

# 10. Resize 处理原则

窗口尺寸变化不应默认导致整张 Graph Compile。

例如：

```text
1920x1080
→
1600x900
```

如果只改变 transient texture 的 width/height：

```text
重新申请 physical resource
不要重新计算 dependency / barrier topology
```

但如果尺寸变化会影响：

```text
Mip count
Bloom pass count
HZB pass count
```

则允许：

```text
MarkGraphDirty()
```

第一版可以保守一些，确保正确性优先。

---

# 11. 暂不实现的内容

本任务不要扩展为大型 RG 重构。

暂不要求：

```text
多 Graph Variant Cache
Graph Hash
增量 Compile
自动 diff Pass topology
跨 Camera 完整缓存
复杂 transient aliasing 优化
UE RDG 风格完整 parameter system
```

如果不同 Camera 当前 topology 不一致，第一版允许：

```text
检测 topology/config 变化
→ MarkGraphDirty
→ rebuild
```

后续再考虑：

```text
GraphVariantKey → CompiledGraph
```

---

# 推荐执行顺序

按以下顺序修改：

```text
1. 删除重复 Compile
2. CompiledBarrier: GpuResource* → RGHandle
3. Compile 中移除 physical resource 创建
4. 拆分 EndFrame / ResetGraph 生命周期
5. 增加 GraphDirty
6. 处理 dynamic lambda capture
7. 支持 external resource rebinding
8. 处理 temporal ping-pong
9. 优化部分 runtime conditional Pass
10. 测试 resize / feature toggle
```

不要一开始重构所有 RenderPass。

---

# 验收标准

## 普通稳定场景

连续运行：

```text
Frame 1:
Build Graph
Compile
Execute

Frame 2:
PrepareFrame
Execute

Frame 3:
PrepareFrame
Execute
```

Frame 2+ 不再 Compile。

---

## Dynamic Frame Data

以下变化不应触发 Compile：

```text
camera transform
object transform
draw count
sprite count
frame index
temporal offset
ping-pong A/B
普通 texture physical resize
```

前提是不改变 Graph topology。

---

## Structure Change

以下变化允许触发一次重新 Compile：

```text
SSAO enable/disable
Fog enable/disable
GI enable/disable
新增/删除 RenderPass
Read/Write dependency 改变
Bloom/HZB pass count 改变
debug pass enable/disable
```

---

## Resize

普通 resize：

```text
不使用旧 physical resource
resource size 正确
barrier 正确
无 validation error
```

如果 resize 改变 mip/pass topology：

```text
重新 Compile 一次
```

---

# 调试信息

建议增加简单统计：

```text
RenderGraph Compile Count
RenderGraph Compile Reason
Pass Count
Compiled Barrier Count
```

例如：

```text
[RenderGraph] Compile #4
Reason: SSAO topology changed
Passes: 37
```

方便确认是否还有意外的每帧 Compile。

---

# 核心约束

最终保证：

```text
Compile 只处理 Logical Graph
Execute 处理 Physical Resource
Frame Data 不应污染 Cached Graph
EndFrame 不等于 Destroy Graph
```

不要为了 Compile Cache 重写现有 RenderGraph。