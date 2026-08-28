# AiluEngine RenderGraph 资源泄漏与状态错乱修复任务

## 1. 背景

当前 DX12 RenderGraph 在多线程 command list 录制下出现大量 `ResourceStateReconcile`，典型资源包括：

- `_MainLightShadowMap`
- `_AddLightShadowMaps`
- `VolumetricFogAccumTexture`

日志表现为：

```text
DeferredLightingPass / rg=0 / _MainLightShadowMap / COMMON -> SRV
DeferredLightingPass / rg=0 / _AddLightShadowMaps / COMMON -> SRV
DeferredLightingPass / rg=0 / VolumetricFogAccumTexture / COMMON -> SRV

随后真正的 ShadowCastPass:
scheduled=SRV
initial=COMMON
RG resource state mismatch
ResourceStateReconcile: SRV -> COMMON
```

RenderGraph 本身已经正确生成了 Shadow/Fog 的显式 barrier，但实际 Draw/Dispatch 过程中还有资源绕过 RG 声明被绑定，导致
command list 的录制状态时间线与最终提交顺序不一致。

---

## 2. 根因结论

问题核心不是优先修 `ResourceStateReconcile`，而是：

> **RenderGraph 管理或追踪的资源通过 persistent Material / Shader Global 状态逃逸，后续 pass 实际访问了该资源，但没有通过当前 RG pass 声明该访问。**

当前至少存在以下两条明确泄漏链。

### 2.1 DeferredLighting persistent Material 残留 Shadow Map

文件：

```text
Src/Render/Features/CommonPasses.cpp
DeferredLightingPass::OnRecordRenderGraph
```

当前逻辑：

```cpp
const bool use_shadow_maps = rendering_data._camera != nullptr && rendering_data._camera->_is_render_shadow;

if (use_shadow_maps)
{
    builder.Read(rendering_data._rg_handles._main_light_shadow_map);
    builder.Read(rendering_data._rg_handles._addi_shadow_maps);
    builder.Read(rendering_data._rg_handles._point_light_shadow_maps);
}
```

Execute 中：

```cpp
if (data._camera != nullptr && data._camera->_is_render_shadow)
{
    _p_lighting_material->SetTexture("_MainLightShadowMap", ...);
    _p_lighting_material->SetTexture("_AddLightShadowMaps", ...);
    _p_lighting_material->SetTexture("_PointLightShadowMap", ...);
}
```

`_p_lighting_material` 是长生命周期成员。

当 Camera A 开启阴影、Camera B 关闭阴影时：

```text
Camera A:
Material ShadowMap = A 的 RG texture

Camera B:
builder 没有 Read ShadowMap
Execute 也没有覆盖 ShadowMap
Material 继续保留 A 的 texture

DrawFullScreenQuad:
仍然访问旧 ShadowMap
```

因此该资源在当前 pass 中：

```text
IsRenderGraphResource == false
```

最终走普通 DX12 `EnsureResourceState()`，绕过 RG barrier 管理。

---

### 2.2 RG 资源被写入 Shader / ComputeShader Global Registry

当前 RG execute lambda 中存在：

```cpp
Shader::SetGlobalTexture(RenderResourceName::kMainLightShadowMap, main_light_shadow_map);
Shader::SetGlobalTexture(RenderResourceName::kAddLightShadowMap, add_light_shadow_maps);
Shader::SetGlobalTexture(RenderResourceName::kPointLightShadowMap, point_light_shadow_maps);

ComputeShader::SetGlobalTexture(...);
```

以及：

```cpp
Shader::SetGlobalTexture("_VolumetricLightTexture", accum_tex);
```

这些 texture 来自当前 RenderGraph 的 `graph.Resolve(...)`。

但是 Shader Global Registry 是 persistent 状态：

```cpp
ShaderGlobalResourceRegistry s_global_res_registry;
```

ComputeShader 也存在独立 persistent global texture registry。

因此形成：

```text
RG resource
    ↓
Shader Global Registry
    ↓
跨 pass / 跨 camera / 跨 graph 保留 raw pointer
    ↓
后续 Material / ComputeShader 自动解析 global binding
    ↓
当前 pass 未声明 builder.Read()
```

这是本次状态错乱的主要架构原因。

---

## 3. 一个额外的重要细节：`SetTexture(name, nullptr)` 不能解决问题

当前 `Material::SetTexture()` 会记录 null：

```cpp
_bind_textures_by_id[property_id] = texture;
```

但是 `CaptureDrawState()` 中：

```cpp
auto apply_resource = [&](ShaderPropertyId property_id, GpuResource *resource, u16 priority)
{
    ...
    apply_resolved_resource(...);
};
```

而：

```cpp
apply_resolved_resource(..., resource, ...)
{
    if (resource == nullptr)
        return;
}
```

之后 global resource 仍然会被应用。

所以：

```cpp
_p_lighting_material->SetTexture("_MainLightShadowMap", nullptr);
```

**目前并不能屏蔽同名 Shader Global Texture。**

不要把简单 `SetTexture(nullptr)` 当成本次修复。

---

# 4. 修复目标

完成后满足以下原则：

> **任何 GPU 对 RenderGraph resource 的访问，都必须能从当前 RenderGraph pass 的 input/output 声明中追溯到。**

以及：

> **RenderGraph resource 禁止直接进入普通 persistent Shader / ComputeShader Global Registry。**

RenderGraph graph-local global resource 如未来确实需要，应走单独的 RG-aware global API，而不是复用普通
`Shader::SetGlobalTexture()`。

---

# 5. 实现阶段

按下面顺序实现。

---

# P0：先修正确性，不重构整个 RenderGraph

这是本次必须完成的部分。

## P0-1. DeferredLighting 每次完整覆盖条件资源

修改：

```text
Src/Render/Features/CommonPasses.cpp
DeferredLightingPass::OnRecordRenderGraph
```

### Shadow

保留当前：

```cpp
if (use_shadow_maps)
{
    builder.Read(...);
}
```

Execute 中改成：

```cpp
if (data._camera != nullptr && data._camera->_is_render_shadow)
{
    _p_lighting_material->SetTexture(
        "_MainLightShadowMap", graph.Resolve<Texture>(data._rg_handles._main_light_shadow_map));
    _p_lighting_material->SetTexture(
        "_AddLightShadowMaps", graph.Resolve<Texture>(data._rg_handles._addi_shadow_maps));
    _p_lighting_material->SetTexture(
        "_PointLightShadowMap", graph.Resolve<Texture>(data._rg_handles._point_light_shadow_maps));
}
else
{
    _p_lighting_material->SetTexture("_MainLightShadowMap", <compatible_dummy_shadow_texture>);
    _p_lighting_material->SetTexture("_AddLightShadowMaps", <compatible_dummy_shadow_array>);
    _p_lighting_material->SetTexture("_PointLightShadowMap", <compatible_dummy_shadow_cube_array>);
}
```

要求：

1. 不允许 false 分支什么都不做。
2. 不使用 `nullptr` 作为主要修复，因为当前 local null 不会屏蔽 global binding。
3. dummy resource 必须是 persistent / non-RG resource。
4. 如果引擎已有对应默认 shadow texture，直接复用；否则增加最小兼容 dummy resource。

### Volumetric Fog

当前 builder 已经：

```cpp
if (rendering_data._rg_handles._volumetric_fog_accum.IsValid())
    builder.Read(rendering_data._rg_handles._volumetric_fog_accum);
```

但 DeferredLighting 并没有显式把它绑定到 Material，而是依赖：

```cpp
Shader::SetGlobalTexture("_VolumetricLightTexture", accum_tex);
```

必须改成显式 local binding：

```cpp
Texture *volumetric_light = nullptr;

if (data._rg_handles._volumetric_fog_accum.IsValid())
    volumetric_light = graph.Resolve<Texture>(data._rg_handles._volumetric_fog_accum);

_p_lighting_material->SetTexture(
    "_VolumetricLightTexture",
    volumetric_light != nullptr ? volumetric_light : <compatible_dummy_volumetric_texture>);
```

这样：

```text
builder.Read(handle)
    ↓
graph.Resolve(handle)
    ↓
Material local binding
```

形成完整闭环。

### AO

当前 DeferredLighting 已经正确：

```cpp
builder.Read(_ao_tex);
...
material.SetTexture("_OcclusionTex", graph.Resolve(...));
```

保持即可。

---

## P0-2. RG ShadowCastPass 不再向普通 Shader Global Registry 发布 RG texture

修改：

```text
Src/Render/Features/CommonPasses.cpp
ShadowCastPass::OnRecordRenderGraph
```

删除 RG Execute lambda 中：

```cpp
Shader::SetGlobalTexture(RenderResourceName::kMainLightShadowMap, main_light_shadow_map);
Shader::SetGlobalTexture(RenderResourceName::kAddLightShadowMap, add_light_shadow_maps);
Shader::SetGlobalTexture(RenderResourceName::kPointLightShadowMap, point_light_shadow_maps);

ComputeShader::SetGlobalTexture(RenderResourceName::kMainLightShadowMap, main_light_shadow_map);
ComputeShader::SetGlobalTexture(RenderResourceName::kAddLightShadowMap, add_light_shadow_maps);
ComputeShader::SetGlobalTexture(RenderResourceName::kPointLightShadowMap, point_light_shadow_maps);
```

注意：

- **只删除 RenderGraph 路径中的 global 发布。**
- Legacy/non-RG `ShadowCastPass::Execute()` 中对 persistent shadow resource 的原有 global 行为暂时保留。
- 不要为了本任务删除普通非 RG 环境贴图、LTC LUT 等 persistent global resource。

---

## P0-3. VolumetricFog 显式绑定 Shadow Map

修改：

```text
Src/Render/Features/VolumetricFog.cpp
VolumetricFogPass::OnRecordRenderGraph
VolumetricFog_LightInject
```

当前 setup 已经：

```cpp
builder.Read(rendering_data._rg_handles._main_light_shadow_map);
```

但是 execute 没有显式：

```cpp
_volumetric_fog->SetTexture(..., "_MainLightShadowMap", ...);
```

之前依赖 ShadowCastPass 写 global。

删除 Shadow global 后，必须补成本 pass local binding：

```cpp
auto *main_light_shadow_map =
    graph.Resolve<Texture>(data._rg_handles._main_light_shadow_map);

_volumetric_fog->SetTexture(
    _light_injection_kernel,
    RenderResourceName::kMainLightShadowMap,
    main_light_shadow_map);
```

如果 shader 实际 property name 不同，以 shader binding 定义为准。

核心要求：

```text
builder.Read(_main_light_shadow_map)
    ↓
graph.Resolve(_main_light_shadow_map)
    ↓
ComputeShader::SetTexture(kernel, ...)
```

不能继续依赖普通 global registry。

---

## P0-4. VolumetricFog 不再向普通 Shader Global Registry 发布 accum texture

删除：

```cpp
Shader::SetGlobalTexture("_VolumetricLightTexture", accum_tex);
```

位置：

```text
VolumetricFog_LightIntegration execute lambda
```

因为 DeferredLighting 已在 P0-1 改为直接：

```cpp
builder.Read(_volumetric_fog_accum)
graph.Resolve(...)
material.SetTexture(...)
```

因此不再需要该 global side effect。

---

## P0-5. 扫描所有 RenderGraph execute lambda 中的 `SetGlobalTexture`

对以下模式做一次集中扫描：

```text
Shader::SetGlobalTexture(...)
ComputeShader::SetGlobalTexture(...)
cmd->SetGlobalTexture(...)
```

尤其关注参数来自：

```cpp
graph.Resolve(...)
```

或明显属于：

```cpp
rendering_data._rg_handles
```

的情况。

规则：

### 必须整改

如果资源由当前 RG 管理/追踪，且 global binding 是为了给后续 RG pass 使用：

```text
producer pass:
SetGlobalTexture(resource)

consumer pass:
隐式使用 global
```

改成：

```text
consumer setup:
builder.Read(handle)

consumer execute:
graph.Resolve(handle)
SetTexture local
```

### 暂时保留

以下不属于本次强制整改范围：

- Engine persistent default texture
- LTC LUT
- 普通资源管理器加载的长期纹理
- Legacy/non-RG renderer 路径
- 明确不是由当前 RenderGraph handle 产生的普通 global resource

不要无差别删除所有 Global Texture API。

---

# P1：增加 RenderGraph 未声明资源访问验证

P0 修正已知问题后，再增加 Debug validation，防止同类问题再次出现。

## P1-1. RenderGraph 增加物理资源查询接口

修改：

```text
Inc/Render/RenderGraph/RenderGraph.h
Src/Render/RenderGraph/RenderGraph.cpp
```

增加 Debug-friendly 查询，例如：

```cpp
bool ContainsResource(const GpuResource *resource) const;
```

语义：

```text
resource 是否属于/被当前 RenderGraph 注册过
```

必须检查：

```cpp
_transient_resources
_external_resources
```

对应 `ResourceNode::GetResource()`。

建议只用于 Debug/validation，不要求优化关键路径。

如果方便诊断，可同时提供：

```cpp
RGHandle FindResourceHandle(const GpuResource *resource) const;
StringView FindResourceName(const GpuResource *resource) const;
```

不是强制。

---

## P1-2. Draw 时验证 Material 最终绑定资源

当前：

```text
CommandBuffer::Impl::PushMaterialState()
    ↓
Material::CaptureDrawState()
    ↓
MaterialDrawState / PipelineBindingSnapshot
```

这里已经能拿到最终真正会绑定到 GPU 的资源 snapshot。

增加 Debug-only 验证：

```text
如果：
    _render_graph != nullptr

并且：
    bound_resource 属于当前 RenderGraph

但是：
    bound_resource 不在当前 command buffer 的 _render_graph_resources

则：
    LOG_ERROR + AL_ASSERT
```

错误信息至少包含：

```text
Undeclared RenderGraph resource access
command_buffer=<name>
resource=<debug name>
resource_ptr=<ptr>
binding_slot=<slot>
source=material_draw
```

当前 `_render_graph_resources` 正是由：

```cpp
RenderGraph::Execute()
```

根据 pass：

```cpp
_input_access_records
_output_access_records
```

调用：

```cpp
cmd->UseRenderGraphResource(...)
```

注册的，因此它可以直接代表：

> 当前 pass 已声明的 RG resource 集合。

### 判定逻辑

必须是：

```text
graph.ContainsResource(bound_resource)
&& !current_pass_declared_resources.contains(bound_resource)
```

不能简单判断：

```text
!current_pass_declared_resources.contains(bound_resource)
```

否则普通 persistent Material texture 也会被误报。

---

## P1-3. Compute Dispatch 做同样验证

当前：

```cpp
ComputeShader::CaptureDispatchState(...)
```

会产生：

```cpp
ComputeDispatchSnapshot
```

其中 `_entries` 已包含最终的：

```cpp
GpuResource *_resource
```

在：

```text
CommandBuffer::Impl::Dispatch(...)
```

capture 完成后加入同样 Debug validation：

```text
graph.ContainsResource(bound_resource)
&& !declared_resources.contains(bound_resource)
```

错误信息：

```text
Undeclared RenderGraph resource access
command_buffer=<name>
resource=<debug name>
binding_slot=<slot>
source=compute_dispatch
```

这一步非常重要。

本次 Volumetric Fog 的 Shadow Map 就属于 compute shader global 隐式绑定问题，仅验证 Material 不够。

---

## P1-4. 不要把验证放到 `EnsureResourceState()` 才报

`D3DCommandBuffer::EnsureResourceState()` 可以保留额外 warning，但它已经太晚。

最理想的报错点是：

```text
Material / Compute binding snapshot 刚生成
```

这样能够直接定位：

```text
哪个 Draw / Dispatch 绑定了 undeclared RG resource
```

而不是等到：

```text
D3DContext ResourceStateReconcile
```

才发现状态不一致。

---

# P2：可选但推荐，增加 RG-aware Global Texture API

P0 + P1 完成后再做。

目标参考 Unity RenderGraph：

```text
SetGlobalTextureAfterPass()
UseGlobalTexture()
```

不需要照搬 Unity API 名称，但语义保持一致。

---

## P2-1. 不允许 RG global registry 存裸 `Texture*`

建议增加 graph-local registry，例如：

```cpp
struct RGGlobalTextureBinding
{
    RGHandle _handle;
    RenderPass *_producer = nullptr;
};
```

RenderGraph 内保存：

```cpp
HashMap<ShaderPropertyId, RGGlobalTextureBinding> _global_textures;
```

graph `EndFrame()` 后清空。

---

## P2-2. Builder API

可增加类似：

```cpp
void SetGlobalTextureAfterPass(ShaderPropertyId property_id, RGHandle handle);
RGHandle ReadGlobalTexture(ShaderPropertyId property_id);
```

或更符合当前 Ailu API 的命名。

要求：

### Producer

```cpp
builder.SetGlobalTextureAfterPass(property_id, handle);
```

必须让 RG 知道：

```text
哪个 pass 生产了这个 global binding
```

### Consumer

```cpp
RGHandle handle = builder.ReadGlobalTexture(property_id);
```

内部等价于：

```text
取到 handle
builder.Read(handle)
```

Execute 阶段再 Resolve 后绑定。

---

## P2-3. 普通 Shader Global 与 RG Global 明确分层

最终规则：

```text
Shader::SetGlobalTexture(Texture*)
    → persistent / non-RG resource

RenderGraph global API
    → current graph resource
```

Debug 模式下：

如果在 RG execute 期间调用普通：

```cpp
Shader::SetGlobalTexture(...)
ComputeShader::SetGlobalTexture(...)
```

并且传入资源属于当前 graph：

```text
直接 LOG_ERROR / ASSERT
```

例如：

```text
RenderGraph resource cannot escape through persistent shader global registry
pass=ShadowCastPass
resource=_MainLightShadowMap
```

---

# 6. ResourceStateReconcile 的处理

本任务 **不要先删除或弱化**：

```text
D3DContext ResourceStateReconcile
```

当前 reconcile 机制仍然负责：

```text
并行 command list 录制时
record-time initial state
与
submit-time scheduled state
之间的衔接
```

本次大量 reconcile 的异常来源是：

```text
undeclared RG access
```

P0/P1 完成后再观察 reconcile 数量。

预期：

- `_MainLightShadowMap` 不再因为 unrelated `DeferredLightingPass` command list 重复出现 `SRV -> COMMON`。
- `_AddLightShadowMaps` 不再每个 command list 都 reconcile 全部 array slice。
- `VolumetricFogAccumTexture` 不再在未声明 fog 的 DeferredLighting 中被隐式转为 SRV。
- 真正由于 parallel recording 边界产生的少量 reconcile 可以继续存在。

---

# 7. 修正状态日志显示

当前日志使用：

```cpp
LOG_WARNING("state=0x{:X}", static_cast<u32>(state));
```

但当前自定义 Logger 并未真正解析 `{:X}`，只会找到 `{}` 范围并直接 stream value。

因此：

```text
0x192
```

实际很可能是：

```text
decimal 192 = hex 0xC0
```

即：

```text
D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
```

同理：

```text
0x16
```

实际是 decimal 16，即 hex `0x10 = DEPTH_WRITE`。

本任务无需重构 Logger format 系统。

简单处理即可：

```cpp
LOG_WARNING("state={}", static_cast<u32>(state));
```

或者显式使用支持 hex 的格式化结果后再传给 logger。

目标只是避免诊断日志继续把十进制值伪装成十六进制。

---

# 8. 建议修改文件

必须重点检查：

```text
Src/Render/Features/CommonPasses.cpp
Src/Render/Features/VolumetricFog.cpp
Src/Render/Material.cpp
Src/Render/Shader.cpp
Src/Render/CommandBuffer.cpp
Inc/Render/RenderGraph/RenderGraph.h
Src/Render/RenderGraph/RenderGraph.cpp
Src/RHI/DX12/D3DCommandBuffer.cpp
Src/RHI/DX12/D3DContext.cpp
Inc/Framework/Common/Log.h
```

其中：

### P0 主要修改

```text
CommonPasses.cpp
VolumetricFog.cpp
```

### P1 主要修改

```text
RenderGraph.h/.cpp
CommandBuffer.cpp
```

必要时只增加少量 snapshot 遍历辅助函数。

### P2

按实际代码结构决定是否扩展：

```text
RenderGraphBuilder
RenderGraph
Shader global API
```

---

# 9. 验收场景

至少跑以下场景。

## Case A：多个 Camera，阴影配置不同

```text
Camera A: shadow on
Camera B: shadow off
Camera C: shadow on
```

要求：

Camera B 的 `DeferredLightingPass`：

```text
不得绑定 Camera A/C 的 RG shadow texture
```

日志中不得出现：

```text
DeferredLightingPass
rg=0
_MainLightShadowMap
```

或：

```text
DeferredLightingPass
rg=0
_AddLightShadowMaps
```

---

## Case B：Volumetric Fog on/off 混合

```text
Camera A: fog on
Camera B: fog off
```

Camera B 的 DeferredLighting：

```text
不得绑定 Camera A 的 VolumetricFogAccumTexture
```

不得出现：

```text
DeferredLightingPass
rg=0
VolumetricFogAccumTexture
```

---

## Case C：开启 graphics job / parallel recording

保持当前多线程 command list 录制。

确认：

```text
ShadowCastPass
VolumetricFog
SSAO
DeferredLighting
FinalBlit
```

均正常。

不以“关闭 graphics job 后不报错”作为修复完成标准。

---

## Case D：Debug validation 故意制造错误

临时在某个 RG pass 中：

```text
不声明 builder.Read(texture)
但实际 Material / ComputeShader 绑定该 RG texture
```

要求在 Draw/Dispatch capture 阶段立即报：

```text
Undeclared RenderGraph resource access
```

然后恢复测试代码。

---

# 10. 验收指标

P0 完成后：

- [ ] DeferredLighting 不再残留上一 Camera 的 shadow RG texture。
- [ ] DeferredLighting 显式绑定 volumetric fog RG resource。
- [ ] VolumetricFog LightInject 显式绑定 main shadow map。
- [ ] RG ShadowCastPass 不再调用普通 Shader/ComputeShader global texture API 发布 shadow RG resource。
- [ ] RG VolumetricFog 不再调用普通 Shader global texture API 发布 accum RG resource。
- [ ] 阴影、SSAO、Fog、Deferred Lighting 画面正常。
- [ ] graphics job 开启时画面正常。

P1 完成后：

- [ ] Material draw 能检测 undeclared RG resource。
- [ ] Compute dispatch 能检测 undeclared RG resource。
- [ ] 普通 persistent texture 不会被误报。
- [ ] Debug 错误信息能指出 command buffer / resource / slot / draw or dispatch。
- [ ] 正常场景下无 undeclared RG resource 报错。

状态日志：

- [ ] 不再把 decimal state 伪装成 `0x...`。
- [ ] `_MainLightShadowMap` / `_AddLightShadowMaps` / `VolumetricFogAccumTexture` 的异常 reconcile 数量显著下降。
- [ ] 不要求 `ResourceStateReconcile` 总数绝对为 0。

---

# 11. 不要做的事情

本任务禁止用以下方式“修好”表象：

1. 不要关闭 graphics job。
2. 不要强制所有 command list 串行。
3. 不要删除所有 `ResourceStateReconcile`。
4. 不要让 `EnsureResourceState()` 对所有 RG resource 都强行信任 global state。
5. 不要把所有资源统一粗暴 transition 到 COMMON。
6. 不要每个 pass 结束都强制资源回 COMMON 来掩盖错误依赖。
7. 不要仅通过 `SetTexture(name, nullptr)` 清 stale binding。
8. 不要无差别删除所有 Shader Global Texture；只限制 RG resource 逃逸。
9. 不要为了修本问题重写整个 RenderGraph 编译器。
10. 不要把 validation 只做成日志后继续静默运行；Debug 下应能 assert。

---

# 12. 最终架构约束

实现完成后，在代码注释或 RenderGraph 文档中加入以下约束：

> **任何 GPU 对 RenderGraph resource 的访问，都必须由当前 pass 的 RenderGraph access declaration 覆盖。Material、ComputeShader、Shader Global、CommandBuffer 等绑定路径不得绕过该声明。**

以及：

> **RenderGraph resource 不得存入跨 pass / 跨 camera / 跨 graph 的普通 persistent global resource registry。需要 graph-global 语义时，必须使用 RenderGraph-aware global binding API。**

本次修复的重点不是让 reconcile “更聪明”，而是让：

```text
RG 声明的资源访问
==
Draw / Dispatch 最终实际绑定的资源访问
```

只有这两者一致，后续 barrier 编译、并行 command list 录制和 submission-time state reconcile 才能可靠工作。
