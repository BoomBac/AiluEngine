# AiluEngine 2D 渲染模块开发方案

## 1. 文档目标

为 AiluEngine 增加一套独立的 2D 场景渲染模块，支持在世界空间中渲染 Sprite，并能够复用现有的：

- ECS
- TransformComponent
- Camera
- Material / Shader
- CommandBuffer
- RenderGraph
- RenderFeature / RenderPass
- Texture 资源系统

本阶段目标不是完整复刻 Unity 的 2D Renderer，而是建立一条结构清晰、可扩展且性能合理的基础 Sprite 渲染链路。

首个版本需要支持：

- 世界空间 Sprite
- 正交相机
- 纹理采样
- UV Rect
- 顶点颜色
- Pivot
- Sprite 尺寸
- 水平、垂直翻转
- Sorting Layer
- Order In Layer
- Alpha Blend
- CPU 视锥剔除
- GPU Instancing
- RenderGraph 接入
- ECS 序列化

首个版本暂不实现：

- Tilemap
- Animated Sprite
- Sprite Atlas 自动打包
- 2D Light
- 2D Shadow
- Normal Map
- Pixel Perfect Camera
- Nine Slice
- Sprite Mask
- Stencil
- Y Sort
- Sprite Shape
- 2D 粒子系统

---

# 2. 总体架构

2D 渲染模块应使用独立渲染路径：

```text
SpriteRendererComponent
    ↓
SpriteRenderPass::CollectSprites
    ↓
SpriteRenderData
    ↓
排序与分批
    ↓
SpriteBatcher
    ↓
GPU InstanceBuffer
    ↓
DrawIndexedInstanced
```

渲染管线接入方式：

```text
SpriteRenderFeature
    ↓
Renderer::AddFeature
    ↓
SpriteRenderFeature::AddRenderPasses
    ↓
SpriteRenderPass
```

不要将 Sprite 直接转换为普通 Mesh，也不要复用 StaticMeshComponent。

不要直接使用 UIRenderer 渲染场景 Sprite。

UIRenderer 可以作为动态四边形、Buffer 管理和 CommandBuffer 提交方式的参考，但 Sprite 渲染必须拥有独立的：

- SpriteBatcher
- SpriteRenderPass
- Sprite GPU Buffer
- 排序规则
- 世界空间坐标逻辑

---

# 3. 建议目录结构

新增以下文件：

```text
Engine/Inc/Render/2D/
    Sprite.h
    SpriteRenderData.h
    SpriteBatcher.h
    SpriteRenderPass.h
    SpriteRenderFeature.h

Engine/Src/Render/2D/
    Sprite.cpp
    SpriteBatcher.cpp
    SpriteRenderPass.cpp
    SpriteRenderFeature.cpp
```

新增 Shader：

```text
Engine/Asset/Shaders/
    default_sprite.alasset
    default_sprite.hlsl
```

SpriteRendererComponent 第一阶段可以继续放在：

```text
Engine/Inc/Scene/Component.h
```

后续可以再将组件拆分到独立文件。

---

# 4. Sprite 资源定义

新增 `Sprite` 资源类型。

```cpp
#pragma once

#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Render/Texture.h"

namespace Ailu::Render
{
    ACLASS()
    class AILU_API Sprite : public Object
    {
        GENERATED_BODY()

    public:
        Sprite() = default;

        APROPERTY()
        Ref<Texture2D> _texture;

        APROPERTY()
        Vector4f _uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);

        APROPERTY()
        Vector2f _pivot = Vector2f(0.5f, 0.5f);

        APROPERTY()
        Vector2f _size = Vector2f::kOne;

        APROPERTY()
        f32 _pixels_per_unit = 100.0f;
    };
}
```

字段定义：

| 字段 | 含义 |
|---|---|
| `_texture` | Sprite 使用的纹理 |
| `_uv_rect` | 纹理中的归一化区域，格式为 x、y、width、height |
| `_pivot` | 归一化 Pivot，左上为 `(0, 0)`，中心为 `(0.5, 0.5)` |
| `_size` | Sprite 在世界空间中的默认尺寸 |
| `_pixels_per_unit` | 像素到世界单位的换算参数 |

若当前资源系统暂时不方便增加 Sprite 资产类型，可以先直接在 SpriteRendererComponent 中存储：

```cpp
Ref<Texture2D> _texture;
Vector4f _uv_rect;
```

渲染链跑通后再补充独立 Sprite 资产。

---

# 5. SpriteRendererComponent

新增 Sprite 渲染组件。

```cpp
AENUM()
enum class ESpriteBlendMode
{
    kAlpha,
    kAdditive,
    kMultiply,
    kOpaque
};

struct AILU_API SpriteRendererComponent
{
    DECLARE_CLASS(SpriteRendererComponent)

    Ref<Render::Sprite> _sprite;
    Ref<Render::Material> _material;

    Color _color = Colors::kWhite;

    Vector2f _size = Vector2f::kOne;
    Vector2f _pivot = Vector2f(0.5f, 0.5f);

    i16 _sorting_layer = 0;
    i32 _order_in_layer = 0;

    ESpriteBlendMode _blend_mode = ESpriteBlendMode::kAlpha;

    bool _flip_x = false;
    bool _flip_y = false;
    bool _visible = true;
};
```

需要实现：

```cpp
Archive &operator<<(Archive &ar, const SpriteRendererComponent &component);
Archive &operator>>(Archive &ar, SpriteRendererComponent &component);
```

组件中只保存可序列化的场景数据。

以下内容不得放入组件：

- GPU InstanceBuffer 索引
- Batch 索引
- 当前帧排序结果
- CommandBuffer
- RenderPass 指针
- Draw Call 状态
- 临时世界包围盒缓存

这些数据属于渲染阶段的运行时数据。

---

# 6. RenderingData 扩展

当前 RenderingData 已经保存 Camera 指针，但 SpriteRenderPass 还需要访问当前 Scene。

在 `RenderingData` 中增加：

```cpp
const SceneManagement::Scene *_scene = nullptr;
```

建议位置：

```cpp
struct RenderingData
{
    // Existing fields...

    const SceneManagement::Scene *_scene = nullptr;
    const Camera *_camera = nullptr;
};
```

在 Renderer 每帧准备场景时赋值：

```cpp
_rendering_data._scene = &scene;
_rendering_data._camera = &camera;
```

不要把 Sprite 收集逻辑直接放入 `Renderer::PrepareScene()`。

Sprite 的收集、排序和批处理应由 SpriteRenderPass 自己负责，避免 Renderer 继续承担更多具体渲染模块逻辑。

---

# 7. SpriteRenderData

新增当前帧使用的 CPU 渲染数据结构。

```cpp
#pragma once

#include "GlobalMarco.h"
#include "Framework/Math/Transform.h"
#include "Render/Material.h"
#include "Render/Texture.h"

namespace Ailu::Render
{
    struct SpriteRenderData
    {
        Matrix4x4f _local_to_world = Matrix4x4f::Identity();

        Vector4f _uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
        Color _color = Colors::kWhite;

        Vector2f _size = Vector2f::kOne;
        Vector2f _pivot = Vector2f(0.5f, 0.5f);

        Texture *_texture = nullptr;
        Material *_material = nullptr;

        i16 _sorting_layer = 0;
        i32 _order_in_layer = 0;

        f32 _distance_to_camera = 0.0f;

        u32 _entity_id = 0;
        ESpriteBlendMode _blend_mode = ESpriteBlendMode::kAlpha;

        bool _flip_x = false;
        bool _flip_y = false;
    };
}
```

该结构只用于当前帧，不参与反射和序列化。

---

# 8. GPU 实例数据

Sprite 不应为每个实例重复上传四个顶点和六个索引。

应使用：

- 一个共享 Unit Quad VertexBuffer
- 一个共享 IndexBuffer
- 一个动态 SpriteInstanceBuffer
- DrawIndexedInstanced

实例数据建议如下：

```cpp
struct SpriteInstanceData
{
    Matrix4x4f _local_to_world;

    Vector4f _uv_rect;
    Vector4f _color;

    Vector4f _size_pivot;

    u32 _texture_index;
    u32 _entity_id;
    u32 _flags;
    u32 _padding;
};
```

字段定义：

```text
_size_pivot.xy = Sprite size
_size_pivot.zw = Pivot
```

`_flags` 位定义建议：

```cpp
inline constexpr u32 kSpriteFlagFlipX = 1u << 0u;
inline constexpr u32 kSpriteFlagFlipY = 1u << 1u;
```

若当前 Material 和纹理绑定系统暂时不支持 Bindless Texture，可以先按 Texture 分 Batch，并让每个 Draw Call 绑定一个纹理。

此时 `_texture_index` 可以暂时保留为 0。

---

# 9. Unit Quad 定义

共享 Quad 使用局部坐标：

```cpp
struct SpriteVertex
{
    Vector2f _position;
    Vector2f _uv;
};
```

顶点：

```cpp
static const SpriteVertex kSpriteVertices[] = {
    {{0.0f, 0.0f}, {0.0f, 0.0f}},
    {{1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.0f, 1.0f}, {0.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 1.0f}}
};
```

索引：

```cpp
static const u16 kSpriteIndices[] = {
    0u, 1u, 2u,
    1u, 3u, 2u
};
```

顶点着色器中根据 Pivot 和 Size 转换：

```text
local_position = unit_position - pivot
local_position *= size
world_position = local_to_world * local_position
clip_position = view_projection * world_position
```

这样 SpriteRendererComponent 的 TransformComponent 仍然负责：

- 世界位置
- 世界旋转
- 世界缩放
- 父子层级

Sprite 自身只负责二维形状尺寸和 Pivot。

---

# 10. SpriteBatchKey 和 SpriteBatch

新增批次 Key：

```cpp
struct SpriteBatchKey
{
    Material *_material = nullptr;
    Texture *_texture = nullptr;
    ESpriteBlendMode _blend_mode = ESpriteBlendMode::kAlpha;

    bool operator==(const SpriteBatchKey &other) const
    {
        return _material == other._material && _texture == other._texture && _blend_mode == other._blend_mode;
    }
};
```

新增批次：

```cpp
struct SpriteBatch
{
    SpriteBatchKey _key;

    u32 _instance_offset = 0;
    u32 _instance_count = 0;
};
```

第一版按以下条件拆 Batch：

- Material 不同
- Texture 不同
- Blend Mode 不同

相同 Batch 使用一次：

```cpp
DrawIndexedInstanced(6u, instance_count, 0u, 0, instance_offset);
```

---

# 11. Sprite 排序规则

第一版不使用 Transform Z 直接作为主要排序规则。

排序键为：

```text
sorting_layer
order_in_layer
material
texture
entity_id
```

建议实现：

```cpp
std::stable_sort(_render_data.begin(), _render_data.end(),
                 [](const SpriteRenderData &lhs, const SpriteRenderData &rhs)
                 {
                     if (lhs._sorting_layer != rhs._sorting_layer)
                         return lhs._sorting_layer < rhs._sorting_layer;

                     if (lhs._order_in_layer != rhs._order_in_layer)
                         return lhs._order_in_layer < rhs._order_in_layer;

                     if (lhs._material != rhs._material)
                         return lhs._material < rhs._material;

                     if (lhs._texture != rhs._texture)
                         return lhs._texture < rhs._texture;

                     return lhs._entity_id < rhs._entity_id;
                 });
```

视觉顺序主要由以下字段明确控制：

```text
_sorting_layer
_order_in_layer
```

同一排序层级中再按 Material 和 Texture 合批。

后续若需要真实半透明深度排序，可以增加独立排序模式：

```cpp
enum class ESpriteSortMode
{
    kLayer,
    kDistance,
    kCustomAxis
};
```

本阶段不实现。

---

# 12. SpriteBatcher

新增 SpriteBatcher。

```cpp
#pragma once

#include "GlobalMarco.h"
#include "Render/Buffer.h"
#include "Render/CommandBuffer.h"
#include "Render/Material.h"
#include "Render/Texture.h"
#include "SpriteRenderData.h"

namespace Ailu::Render
{
    class AILU_API SpriteBatcher
    {
    public:
        SpriteBatcher();
        ~SpriteBatcher();

        void Initialize();
        void Shutdown();

        void Build(const Vector<SpriteRenderData> &render_data);
        void Render(CommandBuffer *cmd, RenderTexture *color_target, RenderTexture *depth_target);

        void Clear();

    private:
        void CreateStaticGeometry();
        void EnsureInstanceCapacity(u32 required_count);
        void BuildInstanceData(const Vector<SpriteRenderData> &render_data);
        void BuildBatches(const Vector<SpriteRenderData> &render_data);

    private:
        Ref<VertexBuffer> _vertex_buffer;
        Ref<IndexBuffer> _index_buffer;
        Ref<StructuredBuffer> _instance_buffer;

        Vector<SpriteInstanceData> _instance_data;
        Vector<SpriteBatch> _batches;

        u32 _instance_capacity = 0u;
    };
}
```

职责：

1. 创建共享 Quad VertexBuffer。
2. 创建共享 Quad IndexBuffer。
3. 管理动态 InstanceBuffer。
4. 将 SpriteRenderData 转换为 SpriteInstanceData。
5. 根据 BatchKey 生成 SpriteBatch。
6. 在 Render 中按批次绑定 Material 和 Texture。
7. 执行 DrawIndexedInstanced。
8. 每帧结束后清空临时数组，但保留 GPU Buffer 容量。

InstanceBuffer 扩容策略建议：

```cpp
new_capacity = std::max(required_count, std::max(256u, _instance_capacity * 2u));
```

不要每帧重新创建 Buffer。

---

# 13. SpriteRenderPass

新增 SpriteRenderPass。

```cpp
#pragma once

#include "Render/Features/RenderFeature.h"
#include "SpriteBatcher.h"
#include "SpriteRenderData.h"

namespace Ailu::Render
{
    class AILU_API SpriteRenderPass : public RenderPass
    {
    public:
        SpriteRenderPass();

        void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) override;
        void Execute(GraphicsContext *context, RenderingData &rendering_data) override;

    private:
        void CollectSprites(const SceneManagement::Scene &scene, const Camera &camera);
        void SortSprites();
        void BuildBatches();
        bool IsVisible(const SpriteRenderData &sprite, const Camera &camera) const;

    private:
        Scope<SpriteBatcher> _batcher;
        Vector<SpriteRenderData> _render_data;
    };
}
```

构造函数设置事件：

```cpp
SpriteRenderPass::SpriteRenderPass()
    : RenderPass("SpriteRenderPass")
{
    _event = ERenderPassEvent::kBeforePostprocess;
    _batcher = MakeScope<SpriteBatcher>();
    _batcher->Initialize();
}
```

更推荐为 Sprite 增加独立事件：

```cpp
kBeforeTransparent = 450,
kBeforeSprite = 460,
kAfterSprite = 490,
kAfterTransparent = 500
```

并将 SpriteRenderPass 设置为：

```cpp
_event = ERenderPassEvent::kBeforeSprite;
```

最终管线顺序：

```text
Opaque 3D
Deferred Lighting
Skybox
Transparent 3D
World-space Sprite
Post-process
Screen-space UI
```

Sprite 应在 PostProcess 前渲染，使其参与：

- Bloom
- Color Grading
- Tone Mapping
- TAA 或其他后处理

---

# 14. Sprite 收集

SpriteRenderPass 每帧从 ECS 中收集：

```cpp
void SpriteRenderPass::CollectSprites(const Scene &scene, const Camera &camera)
{
    _render_data.clear();

    auto &registry = scene.GetRegister();
    u64 entity_index = 0u;

    for (auto &sprite_renderer : registry.View<ECS::SpriteRendererComponent>())
    {
        const ECS::Entity entity = registry.GetEntity<ECS::SpriteRendererComponent>(entity_index);
        const auto *transform =
            registry.GetComponent<ECS::SpriteRendererComponent, ECS::TransformComponent>(entity_index);

        ++entity_index;

        if (!transform || !sprite_renderer._visible || !sprite_renderer._sprite ||
            !sprite_renderer._sprite->_texture)
        {
            continue;
        }

        SpriteRenderData render_data;
        render_data._local_to_world = transform->GetWorldMatrix();
        render_data._uv_rect = sprite_renderer._sprite->_uv_rect;
        render_data._color = sprite_renderer._color;
        render_data._size = sprite_renderer._size;
        render_data._pivot = sprite_renderer._pivot;
        render_data._texture = sprite_renderer._sprite->_texture.get();
        render_data._material = sprite_renderer._material.get();
        render_data._sorting_layer = sprite_renderer._sorting_layer;
        render_data._order_in_layer = sprite_renderer._order_in_layer;
        render_data._blend_mode = sprite_renderer._blend_mode;
        render_data._flip_x = sprite_renderer._flip_x;
        render_data._flip_y = sprite_renderer._flip_y;
        render_data._entity_id = static_cast<u32>(entity);

        const Vector3f world_position = transform->GetPosition();
        render_data._distance_to_camera = Length(world_position - camera.Position());

        if (!IsVisible(render_data, camera))
            continue;

        _render_data.emplace_back(std::move(render_data));
    }
}
```

注意：

- Registry API 名称根据当前实际接口调整。
- Material 为空时使用默认 Sprite Material。
- Size 为零时可以回退到 Sprite 资源的默认 `_size`。
- Pivot 可以优先使用组件值，也可以直接使用 Sprite 资源值。

建议规则：

```text
组件 _size 为非零：使用组件值
组件 _size 为零：使用 Sprite::_size

组件 Pivot 由 Sprite 资源默认初始化，但允许实例覆盖
```

---

# 15. 视锥剔除

第一版使用 CPU Frustum Cull。

为 Sprite 计算世界空间 AABB。

局部四角：

```cpp
const Vector2f min_position = -sprite._pivot * sprite._size;
const Vector2f max_position = (Vector2f::kOne - sprite._pivot) * sprite._size;
```

局部空间四点：

```cpp
Vector3f local_points[4] = {
    {min_position.x, min_position.y, 0.0f},
    {max_position.x, min_position.y, 0.0f},
    {min_position.x, max_position.y, 0.0f},
    {max_position.x, max_position.y, 0.0f}
};
```

将四点乘 `_local_to_world` 后生成 AABB。

然后使用 Camera Frustum 与 AABB 求交。

若当前 Camera 没有公开 Frustum Cull API，可以第一阶段暂时返回 true，先确保渲染链跑通，再补充剔除。

不要在第一版接入现有 3D BVH。

---

# 16. SpriteRenderFeature

新增 Feature。

```cpp
#pragma once

#include "Render/Features/RenderFeature.h"
#include "SpriteRenderPass.h"

namespace Ailu::Render
{
    class AILU_API SpriteRenderFeature : public RenderFeature
    {
    public:
        SpriteRenderFeature();

        void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) override;

    private:
        Scope<SpriteRenderPass> _sprite_render_pass;
    };
}
```

实现：

```cpp
SpriteRenderFeature::SpriteRenderFeature()
    : RenderFeature("SpriteRenderFeature")
{
    _sprite_render_pass = MakeScope<SpriteRenderPass>();
    _is_active = true;
}

void SpriteRenderFeature::AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data)
{
    if (!_is_active || !rendering_data._scene || !rendering_data._camera)
        return;

    renderer.EnqueuePass(_sprite_render_pass.get());
}
```

---

# 17. Renderer 注册

在 Renderer 中增加：

```cpp
RenderFeature *_sprite_renderer = nullptr;
```

在构造函数中创建：

```cpp
_owned_features.emplace_back(MakeScope<SpriteRenderFeature>());
_sprite_renderer = _owned_features.back().get();

_features.push_back(_sprite_renderer);
_sprite_renderer->SetActive(true);
```

若 `MakeScope` 无法直接转换到当前 `_owned_features` 类型，则按照现有 TemporalAA、SSAO 等 Feature 的创建方式保持一致。

不要在 Renderer 中直接增加：

```cpp
Scope<SpriteRenderPass> _sprite_pass;
```

Sprite 应完整通过 SpriteRenderFeature 管理。

---

# 18. RenderGraph 接入

SpriteRenderPass 需要：

- 写入 Camera Color Target
- 可选读取 Camera Depth Target
- 不写 GBuffer
- 不生成独立颜色纹理
- 不参与 Shadow Pass

伪代码：

```cpp
struct SpritePassData
{
    RDG::RGHandle _color;
    RDG::RGHandle _depth;
};

void SpriteRenderPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
{
    if (!rendering_data._scene || !rendering_data._camera)
        return;

    CollectSprites(*rendering_data._scene, *rendering_data._camera);

    if (_render_data.empty())
        return;

    SortSprites();
    _batcher->Build(_render_data);

    graph.AddPass<SpritePassData>(
        "SpriteRenderPass",
        [&](SpritePassData &pass_data, RDG::RenderGraphBuilder &builder)
        {
            pass_data._color = builder.WriteTexture(rendering_data._rg_handles._color_target);
            pass_data._depth = builder.ReadTexture(rendering_data._rg_handles._depth_target);
        },
        [&](const SpritePassData &pass_data, RDG::RenderGraphContext &context)
        {
            RenderTexture *color_target = context.Resolve<RenderTexture>(pass_data._color);
            RenderTexture *depth_target = context.Resolve<RenderTexture>(pass_data._depth);

            _batcher->Render(context._cmd, color_target, depth_target);
        });
}
```

实际函数名需要根据当前 RenderGraph API 调整。

关键资源依赖必须保持：

```text
Camera Color：Read/Write
Camera Depth：Read
```

如果第一版 Sprite 不进行 Depth Test，则可以暂时不读取 Depth。

---

# 19. 非 RenderGraph 回退

保留 Execute：

```cpp
void SpriteRenderPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
{
    if (!rendering_data._scene || !rendering_data._camera)
        return;

    CollectSprites(*rendering_data._scene, *rendering_data._camera);

    if (_render_data.empty())
        return;

    SortSprites();
    _batcher->Build(_render_data);

    auto cmd = CommandBufferPool::Get("SpriteRenderPass");

    RenderTexture *color_target =
        g_pRenderTexturePool->Get(rendering_data._camera_color_target_handle);

    RenderTexture *depth_target =
        g_pRenderTexturePool->Get(rendering_data._camera_depth_target_handle);

    _batcher->Render(cmd.get(), color_target, depth_target);

    context->ExecuteCommandBuffer(cmd);
    CommandBufferPool::Release(cmd);
}
```

具体接口按当前 CommandBufferPool 和 RenderTexturePool 实现调整。

---

# 20. Sprite Shader

## 20.1 Vertex Shader 输入

```hlsl
struct appdata
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    uint instance_id : SV_InstanceID;
};
```

实例数据：

```hlsl
struct sprite_instance_data
{
    float4x4 local_to_world;
    float4 uv_rect;
    float4 color;
    float4 size_pivot;
    uint texture_index;
    uint entity_id;
    uint flags;
    uint padding;
};
```

Buffer：

```hlsl
StructuredBuffer<sprite_instance_data> g_sprite_instances;
```

顶点输出：

```hlsl
struct varyings
{
    float4 position_cs : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint texture_index : TEXCOORD1;
};
```

## 20.2 Vertex Shader

```hlsl
varyings vert(appdata input)
{
    sprite_instance_data instance = g_sprite_instances[input.instance_id];

    float2 unit_position = input.position;
    float2 uv = input.uv;

    if ((instance.flags & 1u) != 0u)
    {
        unit_position.x = 1.0f - unit_position.x;
        uv.x = 1.0f - uv.x;
    }

    if ((instance.flags & 2u) != 0u)
    {
        unit_position.y = 1.0f - unit_position.y;
        uv.y = 1.0f - uv.y;
    }

    float2 size = instance.size_pivot.xy;
    float2 pivot = instance.size_pivot.zw;

    float2 local_position = (unit_position - pivot) * size;
    float4 world_position = mul(instance.local_to_world, float4(local_position, 0.0f, 1.0f));

    varyings output;
    output.position_cs = mul(g_matrix_vp, world_position);
    output.uv = instance.uv_rect.xy + uv * instance.uv_rect.zw;
    output.color = instance.color;
    output.texture_index = instance.texture_index;

    return output;
}
```

矩阵乘法方向必须与 AiluEngine 当前 Shader 约定保持一致。

## 20.3 Pixel Shader

若第一版按纹理拆 Batch：

```hlsl
Texture2D _main_tex;
SamplerState g_linear_clamp_sampler;

float4 frag(varyings input) : SV_Target
{
    float4 texture_color = _main_tex.Sample(g_linear_clamp_sampler, input.uv);
    return texture_color * input.color;
}
```

若使用 Bindless Texture：

```hlsl
float4 frag(varyings input) : SV_Target
{
    float4 texture_color = g_textures[input.texture_index].Sample(g_linear_clamp_sampler, input.uv);
    return texture_color * input.color;
}
```

---

# 21. 默认渲染状态

默认 Sprite Material：

```text
Cull Mode: None
Depth Test: LessEqual
Depth Write: Off
Blend: SrcAlpha, InvSrcAlpha
```

默认使用 Straight Alpha。

本阶段不要同时支持 Straight Alpha 与 Premultiplied Alpha 混用。

Blend Mode 对应状态：

```text
kAlpha
SrcAlpha, InvSrcAlpha

kAdditive
SrcAlpha, One

kMultiply
DstColor, Zero

kOpaque
One, Zero
Depth Write 可选开启
```

若当前 Material 系统无法在运行时切换 Blend State，则为每种 Blend Mode 创建独立默认 Material，或者按 Blend Mode 创建独立 PSO。

---

# 22. 默认材质

新增：

```text
Shaders/default_sprite.alasset
Shaders/default_sprite.hlsl
```

SpriteRendererComponent 的 `_material` 为空时使用默认材质：

```cpp
_default_material =
    MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/default_sprite.alasset"),
                      "DefaultSpriteMaterial");
```

默认纹理为空时：

- 不提交该 Sprite；或
- 使用默认白色纹理。

建议使用默认白色纹理，方便纯色 Quad 调试。

---

# 23. 正交相机

Sprite 渲染直接使用现有 Camera 的 ViewProjection。

需要确认 Camera 支持 Orthographic Projection。

若尚未支持，增加：

```cpp
enum class EProjectionType
{
    kPerspective,
    kOrthographic
};
```

Camera 字段：

```cpp
EProjectionType _projection_type = EProjectionType::kPerspective;
f32 _orthographic_size = 5.0f;
```

正交投影约定：

```text
orthographic_size 表示垂直方向的一半尺寸
vertical_size = orthographic_size * 2
horizontal_size = vertical_size * aspect
```

生成：

```cpp
OrthographicOffCenterLH(
    -horizontal_size * 0.5f,
    horizontal_size * 0.5f,
    -vertical_size * 0.5f,
    vertical_size * 0.5f,
    near_plane,
    far_plane);
```

具体使用 LH 或 RH、Reversed-Z 与矩阵格式需遵循现有 Camera 实现。

---

# 24. 编辑器支持

第一版完成渲染链后，为 SpriteRendererComponent 增加 Inspector。

字段：

```text
Sprite
Material
Color
Size
Pivot
Sorting Layer
Order In Layer
Blend Mode
Flip X
Flip Y
Visible
```

Scene View Gizmo：

- 显示 Sprite 世界空间边界
- 显示 Pivot
- 选中时绘制矩形框

本阶段不实现纹理切片编辑器和 Sprite Atlas 编辑器。

---

# 25. 实现步骤

## 阶段一：硬编码 Quad

目标：

- 新增 SpriteRenderFeature
- 新增 SpriteRenderPass
- 新增 SpriteBatcher
- 新增默认 Sprite Shader
- 在世界原点绘制一个硬编码 Sprite
- 支持正交相机
- 支持 RenderGraph

验收条件：

- Game View 能看到 Sprite
- Scene View 能看到 Sprite
- Resize 后正常
- Sprite 参与后处理
- RenderGraph 无资源状态错误
- DX12 Debug Layer 无错误

该阶段不接 ECS。

---

## 阶段二：接入 ECS

目标：

- 新增 SpriteRendererComponent
- 实现序列化
- SpriteRenderPass 遍历 ECS
- 读取 TransformComponent 世界矩阵
- 支持多个 Sprite

验收条件：

- Entity 增加 SpriteRendererComponent 后可显示
- Transform 平移有效
- Transform 旋转有效
- Transform 缩放有效
- 父子层级有效
- Scene 保存与加载有效

---

## 阶段三：排序与 Instancing

目标：

- SpriteRenderData 排序
- SpriteBatchKey
- SpriteBatch
- GPU InstanceBuffer
- DrawIndexedInstanced

验收条件：

- 相同 Material、Texture 和 Blend Mode 的 Sprite 合并为一个 Draw Call
- Sorting Layer 正确
- Order In Layer 正确
- 不同纹理正确拆 Batch
- 不同 Blend Mode 正确拆 Batch
- 1000 个 Sprite 正常渲染

---

## 阶段四：剔除

目标：

- 计算 Sprite 世界 AABB
- Camera Frustum Cull
- 不可见 Sprite 不进入 InstanceBuffer

验收条件：

- 相机外 Sprite 不提交
- 旋转和缩放后的 Sprite 包围盒正确
- 剔除不会导致边缘闪烁

---

## 阶段五：编辑器支持

目标：

- Inspector
- Sprite 资源选择
- Scene View Bounds
- Pivot Gizmo

验收条件：

- 能在编辑器中创建 Sprite Entity
- 能修改排序、颜色、尺寸和 Pivot
- 修改后实时刷新

---

# 26. 性能要求

首个版本目标：

```text
1000 个相同纹理 Sprite：
1 个 Draw Call
1 次 InstanceBuffer 更新

1000 个 Sprite、10 张纹理：
最多约 10 个 Draw Call

每帧不得为每个 Sprite 创建：
Mesh
Material
VertexBuffer
IndexBuffer
ConstantBuffer
CommandBuffer
```

CPU 临时数组应复用容量：

```cpp
_render_data.clear();
_instance_data.clear();
_batches.clear();
```

不要每帧主动 shrink。

InstanceBuffer 只在容量不足时扩容。

---

# 27. 错误处理

以下情况跳过 Sprite：

```text
SpriteRendererComponent 不可见
Sprite 为空
Texture 为空且无默认纹理
TransformComponent 不存在
Material Shader 无效
Texture 未完成加载
```

调试版本可以输出一次 Warning，但不要每帧重复刷日志。

可按 Entity ID 记录已输出 Warning 的对象。

---

# 28. 测试场景

创建一个 2D 测试场景：

```text
Orthographic Camera
    Position: (0, 0, -10)
    Looking Direction: +Z 或遵循现有坐标系

Sprite A
    Position: (-2, 0, 0)
    Sorting Layer: 0
    Order: 0

Sprite B
    Position: (0, 0, 0)
    Sorting Layer: 0
    Order: 1

Sprite C
    Position: (2, 0, 0)
    Rotation: 45°
    Scale: (2, 1, 1)

Sprite Parent
    Position: (0, 2, 0)

    Sprite Child
        Local Position: (1, 0, 0)
```

测试项：

1. 纹理显示正确。
2. UV 无上下颠倒。
3. Pivot 正确。
4. Flip X 正确。
5. Flip Y 正确。
6. Alpha Blend 正确。
7. 父子 Transform 正确。
8. Sorting Layer 正确。
9. Order In Layer 正确。
10. PostProcess 影响 Sprite。
11. Scene View 和 Game View 行为一致。
12. Resize 后 Sprite 比例正确。
13. Camera Orthographic Size 修改后显示正确。
14. 1000 Sprite Instancing 正常。
15. RenderDoc 中 Draw Call 和 Instance Count 正确。

---

# 29. 不应采用的实现

禁止以下实现方式：

```text
每个 Sprite 创建一个 Mesh
每个 Sprite 创建一个 Material 实例
每个 Sprite 独立 Draw Call
将 SpriteRendererComponent 伪装成 StaticMeshComponent
将 Sprite 数据写入 GBuffer
直接调用 UIRenderer 绘制世界 Sprite
将 Sprite 收集逻辑堆入 Renderer::PrepareScene
每帧创建和销毁 GPU Buffer
使用 Transform Z 同时承担所有排序语义
第一阶段实现 Tilemap、2D Light 和 Atlas 打包
```

---

# 30. 首批修改清单

必须修改：

```text
Engine/Inc/Render/RenderingData.h
Engine/Inc/Render/Renderer.h
Engine/Src/Render/Renderer.cpp
Engine/Inc/Scene/Component.h
对应 Component 序列化实现
RenderPass Event 定义
构建脚本或 CMake 文件
```

必须新增：

```text
Engine/Inc/Render/2D/Sprite.h
Engine/Inc/Render/2D/SpriteRenderData.h
Engine/Inc/Render/2D/SpriteBatcher.h
Engine/Inc/Render/2D/SpriteRenderPass.h
Engine/Inc/Render/2D/SpriteRenderFeature.h

Engine/Src/Render/2D/Sprite.cpp
Engine/Src/Render/2D/SpriteBatcher.cpp
Engine/Src/Render/2D/SpriteRenderPass.cpp
Engine/Src/Render/2D/SpriteRenderFeature.cpp

Sprite Shader 和 Shader Asset
```

---

# 31. 推荐的第一提交范围

第一次提交只实现：

```text
RenderingData 增加 Scene 指针
SpriteRenderFeature
SpriteRenderPass
SpriteBatcher
共享 Quad
默认 Sprite Shader
硬编码世界空间 Sprite
RenderGraph 接入
```

第一次提交不要实现：

```text
ECS 组件
序列化
编辑器 Inspector
Sprite 资产
剔除
复杂排序
多 Blend Mode
```

第一提交验收通过后，再进行第二提交：

```text
SpriteRendererComponent
ECS 收集
Transform 接入
默认材质
单纹理 Batch
```

第三提交：

```text
Instancing
Sorting Layer
Order In Layer
Frustum Cull
编辑器支持
```

---

# 32. 最终验收标准

完成后，AiluEngine 应具备以下能力：

1. Scene Entity 可以挂载 SpriteRendererComponent。
2. Sprite 可以在世界空间中被正交相机渲染。
3. Sprite 使用 TransformComponent 的世界变换。
4. Sprite 参与现有后处理。
5. Sprite 不进入 GBuffer 和阴影管线。
6. 相同状态 Sprite 可以使用 GPU Instancing。
7. Sprite 可以通过 Sorting Layer 和 Order In Layer 控制顺序。
8. Sprite 可以正常序列化和反序列化。
9. Sprite 渲染逻辑通过 RenderFeature 接入，不污染 Renderer 主流程。
10. Sprite 模块能够作为后续 Tilemap、Animated Sprite 和 2D Light 的基础。