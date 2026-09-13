# AiluEngine Postprocess Stack / Volume / Color Pipeline 重构任务包

## 1. 背景

当前 AiluEngine 的颜色管线总体已经采用：

```text
sRGB Asset
    ↓ GPU sRGB Decode
Scene Linear
    ↓
Lighting / GI / Fog / TAA / Bloom
    ↓
HDR Scene Color
    ↓
Tone Mapping
    ↓
Display Linear
    ↓
FP16 scRGB SwapChain
```

运行时 `Color` 采用 Linear，JSON 序列化时转为 sRGB，编辑器 ColorPicker 同样在 UI sRGB 与 Runtime Linear 之间转换，这部分设计保持不变。

纹理导入也已经根据 `_is_srgb` 创建 `*_SRGB` 格式，由 GPU 采样时自动完成 sRGB → Linear。

当前主要问题集中在 Postprocess：

- `PostProcessPass` 同时负责 Bloom、Blur、Compose、Tone Mapping。
- ACES Tone Mapping 被写在 Bloom Compose shader 中。
- Bloom 和 Tone Mapping 无法独立开关。
- Camera 的 `_is_enable_postprocess` 实际同时控制了 Bloom 和最终 Tone Mapping。
- RDG / 非 RDG 存在大量重复后处理逻辑。
- 后处理参数直接保存在 Pass 内，无法作为场景配置。
- 缺少明确的 Postprocess Stack。
- 缺少场景级 Postprocess Volume。
- Output Transform 与 Tone Mapping 概念没有明确分层。
- sRGB 纹理 mip 生成流程需要检查并修正 Gamma-space filtering。

---

## 2. 本次目标

一次性建立下面的架构：

```text
Scene
 └─ PostprocessVolumeComponent
       ↓
ResolvePostprocessSettings()
       ↓
RenderingData::_postprocess_settings
       ↓
PostprocessStack
 ├─ BloomPass
 └─ ToneMappingPass
       ↓
Display Linear
       ↓
Presentation / UI
       ↓
OutputTransform
       ↓
SwapChain
```

第一版 Volume **只实现 Global Volume**。

暂不实现：

```text
Local Volume Bounds
Volume Blend
Blend Distance
多个 Volume 参数插值
Camera 与 Volume 空间关系
复杂 Effect Registry
动态 Effect Graph
```

但数据结构需允许未来自然扩展。

---

## 3. 核心原则

### 3.1 Scene Rendering 全程保持 Linear HDR

在 Tone Mapping 之前：

```text
Albedo
Lighting
Emission
Fog
TAA
Bloom
GI
```

全部保持 Scene Linear。

禁止为了修亮度在中途加入：

```hlsl
pow(color, 1.0 / 2.2)
ApplySRGBCurve()
```

当前 FP16 SwapChain 是 scRGB Linear 输出，因此最终也不要额外执行 sRGB Gamma Encode。

### 3.2 Tone Mapping 与 Bloom 完全分离

当前：

```text
Bloom Compose
    ↓
ACESFilm()
```

必须拆成：

```text
BloomPass
Scene HDR + Bloom HDR
        ↓
HDR Linear

ToneMappingPass
HDR Linear
        ↓
Display Linear
```

Bloom 禁止再执行：

```hlsl
ACESFilm()
```

### 3.3 PostprocessStack 只负责编排

`PostprocessStack` 不实现具体效果算法。

职责仅为：

```text
输入颜色
↓
Bloom
↓
Tone Mapping
↓
输出颜色
```

第一版使用固定执行顺序，不引入：

```text
IPostprocessEffect
virtual effect interface
effect registry
effect dependency graph
runtime priority sorting
```

避免过度设计。

---

## 4. 新增数据结构

建议放在适合 Scene / Render 公共访问的位置。

```cpp
AENUM()
enum class EToneMappingMode
{
    kNone,
    kAces
};

ASTRUCT()
struct BloomSettings
{
    GENERATED_BODY()

    APROPERTY()
    bool _enabled = true;

    APROPERTY()
    f32 _intensity = 0.35f;

    APROPERTY()
    f32 _radius = 0.005f;
};

ASTRUCT()
struct ToneMappingSettings
{
    GENERATED_BODY()

    APROPERTY()
    EToneMappingMode _mode = EToneMappingMode::kAces;

    APROPERTY()
    f32 _exposure = 1.0f;
};

ASTRUCT()
struct PostprocessSettings
{
    GENERATED_BODY()

    APROPERTY()
    BloomSettings _bloom;

    APROPERTY()
    ToneMappingSettings _tone_mapping;
};
```

第一版不要添加大量尚未使用的参数。

未来 Color Grading / Vignette 等继续添加到 `PostprocessSettings` 即可。

---

## 5. PostprocessVolumeComponent

新增 ECS 组件：

```cpp
ASTRUCT()
struct PostprocessVolumeComponent
{
    GENERATED_BODY()

    DECLARE_COMPONENT(PostprocessVolumeComponent, "Ailu.ECS.PostprocessVolumeComponent")

    APROPERTY()
    bool _is_global = true;

    APROPERTY()
    i32 _priority = 0;

    APROPERTY()
    f32 _blend_weight = 1.0f;

    APROPERTY()
    f32 _blend_distance = 0.0f;

    APROPERTY()
    PostprocessSettings _settings;
};
```

第一版实际生效字段：

```text
_is_global
_priority
_settings
```

以下字段只为后续 Volume Blend 预留：

```text
_blend_weight
_blend_distance
```

不要实现对应 Blend 逻辑。

---

## 6. Global Volume 解析

新增一个轻量解析函数，例如：

```cpp
const ECS::PostprocessVolumeComponent* ResolveGlobalPostprocessVolume(const Scene& scene);
```

筛选条件：

```text
Entity enabled
Component enabled
_is_global == true
```

存在多个 Global Volume 时：

```text
_priority 最大者生效
```

同 Priority 时必须保证稳定结果，例如：

```text
Entity ID 更小者优先
```

不要依赖 ECS View 的偶然遍历顺序。

未来接口可演进为：

```cpp
PostprocessSettings ResolvePostprocessSettings(const Scene& scene, const Camera& camera);
```

但本次不实现局部 Volume。

---

## 7. RenderingData

为每个 Camera 渲染准备最终后处理参数：

```cpp
PostprocessSettings _postprocess_settings;
```

推荐流程：

```text
Renderer::BeginScene
    ↓
Resolve Global Volume
    ↓
copy PostprocessSettings
    ↓
RenderingData
```

如果场景不存在 Global Volume：

```text
使用默认 PostprocessSettings
```

不要让 `PostprocessStack` 自己扫描 Scene。

---

## 8. PostprocessStack

用新的 `PostprocessStack` 替代当前职责过重的 `PostProcessPass`。

建议：

```cpp
class PostprocessStack : public RenderPass
{
public:
    PostprocessStack();

    void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;

private:
    BloomPass _bloom;
    ToneMappingPass _tone_mapping;
};
```

核心逻辑：

```cpp
void PostprocessStack::OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data)
{
    const auto& settings = rendering_data._postprocess_settings;
    auto color = rendering_data._rg_handles._color_target;

    if (settings._bloom._enabled)
        color = _bloom.Record(graph, color, rendering_data, settings._bloom);

    if (settings._tone_mapping._mode != EToneMappingMode::kNone)
        color = _tone_mapping.Record(graph, color, rendering_data, settings._tone_mapping);

    rendering_data._rg_handles._color_target = color;
}
```

重点：

> 每个 Effect 接收一个 `RGHandle`，返回新的 `RGHandle`。

禁止 Effect 随意修改全局：

```cpp
rendering_data._rg_handles._color_target
```

这样数据依赖明确：

```text
source
→ effect
→ output
→ next effect
```

---

## 9. BloomPass

从当前 `PostProcessPass` 中完整迁移：

```text
Bloom mip 创建
Downsample
Upsample
Bloom Compose
Bloom materials
Bloom 参数
```

接口建议：

```cpp
class BloomPass
{
public:
    RDG::RGHandle Record(
        RDG::RenderGraph& graph,
        RDG::RGHandle source,
        const RenderingData& rendering_data,
        const BloomSettings& settings);
};
```

输出必须仍然是：

```text
Scene Linear HDR
```

Bloom Compose 改为类似：

```hlsl
float3 scene_color = SampleSceneColor();
float3 bloom_color = SampleBloom();

float3 color = scene_color + bloom_color * _BloomIntensity;

return float4(color, 1.0f);
```

具体算法可以保留现有效果，不要求本次重写 Bloom 算法。

---

## 10. 修复 Bloom 当前隐患

当前代码存在：

```cpp
f32 blur_radius = _upsample_radius / (f32)i;
```

而循环从：

```cpp
i = 0
```

开始。

需要一并修复。

不要通过 epsilon 掩盖。

根据实际算法重新定义 mip level 与 radius 的关系，例如：

```cpp
const f32 mip_scale = static_cast<f32>(i + 1u);
const f32 blur_radius = settings._radius / mip_scale;
```

---

## 11. ToneMappingPass

新增独立：

```cpp
class ToneMappingPass
{
public:
    RDG::RGHandle Record(
        RDG::RenderGraph& graph,
        RDG::RGHandle source,
        const RenderingData& rendering_data,
        const ToneMappingSettings& settings);
};
```

处理：

```text
Scene Linear HDR
↓
Exposure
↓
ACES
↓
Display Linear
```

例如：

```hlsl
float3 color = SampleSceneColor();

color *= _Exposure;

#if defined(TONE_MAPPING_ACES)
color = ACESFilm(color);
#endif

return float4(color, 1.0f);
```

不要在这里做：

```hlsl
ApplySRGBCurve()
pow(color, 1.0 / 2.2)
```

当前 SwapChain 为 FP16 scRGB，因此输出仍然是 Linear。

---

## 12. ACES 从 Bloom Shader 移除

当前 ACES 位于 Bloom Compose 中。

删除：

```hlsl
c = ACESFilm(c);
```

Bloom shader 只负责 Bloom。

ACES 放入独立：

```text
tone_mapping.hlsl
```

或者合适的独立 PostProcess shader。

---

## 13. Postprocess 总开关

当前 Renderer 使用：

```cpp
if (cam._is_enable_postprocess)
{
    _render_passes.emplace_back(_copycolor_pass.get());
    _render_passes.emplace_back(_postprocess_pass.get());
}
```

需要重新整理语义。

Camera 可以保留：

```cpp
bool _postprocess_enabled = true;
```

但它只能作为：

```text
是否执行 PostprocessStack
```

的 Debug / Camera 级总开关。

具体效果参数全部来自：

```text
PostprocessVolumeComponent
```

禁止再次把：

```text
Bloom parameter
Exposure
ToneMapping mode
```

放回 Camera。

---

## 14. CopyColorPass 清理

当前 Postprocess 前存在额外：

```text
CopyColorPass
```

这次检查它是否仍然必要。

如果只是为了：

```text
source/destination 不能相同
```

优先让：

```text
BloomPass
ToneMappingPass
```

各自显式创建输出 handle。

例如：

```text
SceneColorA
→ BloomColor
→ ToneMappedColor
```

避免通过一个通用 Copy Pass 隐式解决 ping-pong。

如果 TAA 或其他系统确实依赖 `_color_tex`，保留必要 Copy，但职责必须明确，不要继续作为 Postprocess 的隐式输入准备步骤。

---

## 15. RDG 为唯一正式实现

当前 `PostProcessPass` 同时存在：

```cpp
OnRecordRenderGraph()
Execute()
```

两套几乎独立的后处理实现。

本次既然 AiluEngine 已经以 RDG 为正式路线，建议：

> Postprocess 新架构只维护 RDG 实现。

旧的非 RDG `Execute()`：

- 如果 Renderer 已不再需要，删除。
- 如果暂时还必须保留，只允许做兼容 wrapper。
- 禁止维护两套 Bloom/ToneMapping 算法。

目标是以后修改后处理只改一处。

---

## 16. Output Transform

新增概念，但本次不要过度实现。

建议定义：

```cpp
AENUM()
enum class EDisplayOutput
{
    kSdrSrgb,
    kHdrScRgb,
    kHdr10Pq
};
```

职责：

```text
Display Linear
↓
Output Transform
↓
SwapChain Encoding
```

当前模式：

```text
kHdrScRgb
```

输出转换为：

```cpp
output = input;
```

即 Identity。

明确禁止 ToneMappingPass 处理：

```text
sRGB Gamma
PQ
Rec.2020
SwapChain Format
```

这些属于 Presentation 层。

本次可以只预留结构，不需要实现 HDR10。

---

## 17. SceneView / UI 语义

保持：

```text
Scene HDR
↓
PostprocessStack
↓
Display Linear
↓
SceneView
```

正常 SceneView 不显示 Raw HDR。

如果未来需要：

```text
Raw HDR
Bloom Debug
Exposure Debug
False Color
```

应做独立 Debug View，而不是关闭 ToneMapping 后让 UI target 自动 clamp。

---

## 18. UI Composite HDR 限制

当前 UI 针对 SwapChain 会创建 `ERenderTargetFormat::kDefault` 的临时 Composite Target，该 target 为 UNORM，因此无法保存 `>1` HDR 数值。

本次不要求重构整个 UI Renderer，但至少：

- 明确这一限制。
- 正常 SceneView 输入必须已经 Tone Mapped。
- 不允许依赖 UI UNORM clamp 充当 Tone Mapping。

后续 HDR UI 再单独处理。

---

## 19. Texture sRGB Mipmap 修复

当前 TextureImporter 会先把普通纹理归一化到：

```text
R8G8B8A8_UNORM
```

然后做 Resize / GenerateMipMaps，最后才转换或压缩为 sRGB 格式。

需要验证并修复：

> `_is_srgb == true` 的 Color Texture 必须在线性空间完成 resize/mipmap filtering。

正确语义：

```text
sRGB source
↓
decode to Linear
↓
resize / mip filtering
↓
encode back to sRGB
↓
BC*_SRGB / RGBA8_SRGB
```

Normal / Metallic / Roughness / LUT 等：

```text
保持 Linear filtering
```

不要影响数据纹理。

---

## 20. Material Color 序列化检查

普通 `Color` 已经：

```text
Runtime Linear
JSON sRGB
```

但 Material Asset 的 shader vector property 当前通过 `AssetNamedVectorProperty` 保存。

需要检查：

```text
Shader Property Type == Color
```

的属性保存/加载时是否正确进行：

```text
Linear ↔ sRGB
```

不能因为落在 `Vector4f` 容器中绕开 `SerializerWrapper<Color>`。

要求：

```text
Color Property:
JSON = sRGB
Runtime = Linear

Vector Property:
JSON = Raw Linear Numeric Value
Runtime = Same
```

二者必须区分。

---

## 21. Scene / Prefab 序列化

`PostprocessVolumeComponent` 必须完整接入：

```text
SceneEntityDocument
EntitySerializer
Scene Load
Prefab Save
Prefab Load / Instantiate
Component enabled state
```

要求：

```text
Scene 保存/重开
Prefab 保存/实例化
Play Mode
```

参数一致。

---

## 22. Inspector

新增 `PostprocessVolumeComponent` Inspector。

第一版至少显示：

```text
Postprocess Volume
├─ Global
├─ Priority
├─ Bloom
│  ├─ Enabled
│  ├─ Intensity
│  └─ Radius
└─ Tone Mapping
   ├─ Mode
   └─ Exposure
```

`Blend Weight / Blend Distance` 如果显示，可以标成 Disabled / Future。

更推荐第一版直接隐藏，等真正实现 Local Volume 时再开放。

---

## 23. 代码清理

完成后删除 `PostProcessPass` 中不再使用的：

```text
Blur state
Bloom compose ACES
noise texture
unused quad mesh
unused ConstantBuffer
legacy duplicated code
dead fields
dead materials
```

原则：

> 新 PostprocessStack 自身尽量无 GPU 算法状态，资源归具体 Effect 所有。

---

## 24. 推荐文件组织

可以按现有 `Render/Features` 结构：

```text
Engine/Inc/Render/Features/
    PostprocessStack.h
    BloomPass.h
    ToneMappingPass.h

Engine/Src/Render/Features/
    PostprocessStack.cpp
    BloomPass.cpp
    ToneMappingPass.cpp

Engine/Res/Shaders/hlsl/PostProcess/
    bloom.hlsl
    tone_mapping.hlsl
```

Volume Settings 可以放：

```text
Scene/Component.h
```

如果后续内容逐渐增多，再单独拆：

```text
Render/PostprocessSettings.h
```

第一版不要为了文件漂亮过度切分。

---

## 25. 最终执行链

目标链路必须清晰为：

```text
Albedo sRGB Texture
       ↓ hardware decode
Linear Material Input
       ↓
GBuffer Linear
       ↓
Deferred / Forward Lighting
       ↓
HDR Scene Linear
       ↓
TAA / Fog / GI / Sprite
       ↓
PostprocessStack
       ├─ Bloom
       │    HDR → HDR
       │
       └─ Tone Mapping
            HDR → Display Linear
       ↓
Presentation Texture
       ↓
SceneView / UI
       ↓
Output Transform
       ↓
FP16 scRGB SwapChain
```

---

## 26. 验收标准

- 白色 Albedo + 白色 Directional Light 的结果不会因为本次 Gamma 重构发生异常跳变；其亮度问题仍由 Light Intensity / Lambert `1/PI` 单独处理。
- Bloom On / Off 不再影响 Tone Mapping。
- Tone Mapping `kNone` 可以看到 Raw HDR 结果，用于 Debug。
- Tone Mapping `kAces` 独立正常工作。
- Exposure 正确作用于 Tone Mapping 输入。
- 不存在 `ApplySRGBCurve()` / `pow(1/2.2)` 被错误加入 FP16 scRGB 输出链。
- Scene 中一个 Global `PostprocessVolumeComponent` 可以控制 Bloom / Exposure / Tone Mapping。
- 多个 Global Volume 时 Priority 行为稳定。
- 保存/重新加载 Scene 后 Volume 参数一致。
- Prefab 中 Volume 可以正确序列化。
- Bloom 不再包含 ACES。
- Bloom Compose alpha 输出为 `1.0f`。
- 修复 Bloom `i == 0` 除零问题。
- 新 Postprocess 只有一套正式 RDG 实现。
- Color Texture mipmap 在线性颜色空间正确过滤。
- Material Shader `Color` 属性保存为 sRGB、运行时保持 Linear。
- RenderDoc 中 ToneMapping 前 SceneColor 可以明显存在 `>1` HDR 值。
- ToneMapping 后进入当前 scRGB Presentation 的数据保持 Linear。

---

## 27. 非目标

本次明确不要做：

```text
Local Postprocess Volume
Collider / Box Volume
Volume Blend
Blend Curve
Volume Layer Mask
Camera Volume Mask
Effect 动态插件系统
Postprocess Graph
HDR10/PQ 完整实现
Auto Exposure Histogram
LUT Color Grading
复杂 ACES 1.x pipeline
```

这些后续可以在当前架构上自然增加。

---

## 28. 一句话架构约束

> **AiluEngine 后处理统一采用“Scene Linear HDR → PostprocessStack → Display Linear → Output Transform”的模型；Volume 负责配置，Stack 负责编排，Effect 负责算法，任何单个 Effect 都不得承担显示输出颜色空间转换。**
