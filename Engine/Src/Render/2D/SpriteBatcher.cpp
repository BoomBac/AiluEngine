#include "Render/2D/SpriteBatcher.h"
#include "Render/Material.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/PipelineState.h"
#include "Render/ShaderInterop.h"
#include "pch.h"

namespace Ailu::Render
{
    static const ShaderPropertyId kBaseInstID        = Shader::PropertyID("_base_instance_index");
    static const ShaderPropertyId kSpriteInstancesID = Shader::PropertyID("g_sprite_instances");
    static const ShaderPropertyId kMainTexID         = Shader::PropertyID("_MainTex");

    SpriteBatcher::SpriteBatcher()
    {
    }

    bool SpriteBatcher::IsReadyForRender() const
    {
        if (_vertex_buffer == nullptr || !_vertex_buffer->IsReady())
            return false;
        if (_index_buffer == nullptr || !_index_buffer->IsReady())
            return false;
        if (_instance_buffer == nullptr || !_instance_buffer->IsReady())
            return false;
        return _default_material != nullptr && _default_material->IsReadyForDraw();
    }

    SpriteBatcher::~SpriteBatcher()
    {
        Shutdown();
    }

    void SpriteBatcher::Initialize()
    {
        CreateStaticGeometry();

        _default_material = MakeRef<Material>(
            ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_sprite.alasset"),
            "DefaultSpriteMaterial");
        _default_material->SetTexture("_MainTex", Texture2D::s_p_default_white);

        _per_obj_cb = ConstantBuffer::Create(sizeof(CBufferPerObjectData));
        memset(_per_obj_cb->GetData(), 0, sizeof(CBufferPerObjectData));

        _instance_capacity = 256u;
    }

    void SpriteBatcher::Shutdown()
    {
        _vertex_buffer.reset();
        _index_buffer.reset();
        _instance_buffer.reset();
        _per_obj_cb.reset();
        _default_material.reset();
        _instance_data.clear();
        _batches.clear();
    }

    void SpriteBatcher::CreateStaticGeometry()
    {
        // POSITION on stream 0, TEXCOORD on stream 1
        VertexBufferLayout layout{
            {EVertexSemantic::kPosition, EShaderDateType::kFloat2, 0},
            {EVertexSemantic::kTexcoord0, EShaderDateType::kFloat2, 1}
        };

        auto vb = VertexBuffer::Create(layout, "SpriteUnitQuadVB");
        vb->SetStream((u8 *)kSpritePositions, sizeof(kSpritePositions), 0, false);
        vb->SetStream((u8 *)kSpriteUVs, sizeof(kSpriteUVs), 1, false);
        _vertex_buffer = std::move(vb);
        GraphicsContext::Get().CreateResource(_vertex_buffer.get());

        // Create index buffer
        _index_buffer = IndexBuffer::Create(kSpriteIndices, 6, "SpriteUnitQuadIB", false);
        GraphicsContext::Get().CreateResource(_index_buffer.get());
    }

    void SpriteBatcher::Build(const Vector<SpriteRenderData> &render_data)
    {
        _instance_data.clear();
        _batches.clear();

        if (render_data.empty())
            return;

        BuildInstanceData(render_data);
        BuildBatches(render_data);

        // Upload instance data to GPU
        if (!_instance_data.empty())
        {
            _instance_buffer->SetData(reinterpret_cast<const u8 *>(_instance_data.data()),
                                      static_cast<u32>(_instance_data.size() * sizeof(SpriteInstanceData)));
        }
    }

    void SpriteBatcher::Render(CommandBuffer *cmd, RenderTexture *color_target, RenderTexture *depth_target)
    {
        if (_batches.empty())
            return;

        cmd->SetRenderTarget(color_target, depth_target);

        // Bind the instance buffer globally for the shader
        //Shader::SetGlobalBuffer("g_sprite_instances", _instance_buffer);

        for (const auto &batch : _batches)
        {
            if (batch._instance_count == 0)
                continue;

            Material *mat = batch._key._material != nullptr ? batch._key._material : _default_material.get();

            Texture *tex = batch._key._texture;
            if (tex != nullptr)
                mat->SetTexture(kMainTexID, tex);
            mat->SetBuffer(kSpriteInstancesID, _instance_buffer.get());
            mat->SetInt(kBaseInstID, batch._instance_offset);
            cmd->DrawIndexedInstanced(_vertex_buffer.get(), _index_buffer.get(), nullptr, mat, 0, batch._instance_count, 0, 0,
                                      6);
        }
    }

    void SpriteBatcher::RenderWithMaterial(CommandBuffer *cmd, RenderTexture *color_target, RenderTexture *depth_target,
                                           Material *material)
    {
        if (_batches.empty() || material == nullptr)
            return;

        cmd->SetRenderTarget(color_target, depth_target);
        RenderWithMaterial(cmd, material);
    }

    void SpriteBatcher::RenderWithMaterial(CommandBuffer *cmd, Material *material)
    {
        if (_batches.empty() || material == nullptr)
            return;

        for (const auto &batch : _batches)
        {
            if (batch._instance_count == 0)
                continue;

            if (batch._key._texture != nullptr)
                material->SetTexture(kMainTexID, batch._key._texture);
            material->SetBuffer(kSpriteInstancesID, _instance_buffer.get());
            material->SetInt(kBaseInstID, batch._instance_offset);
            cmd->DrawIndexedInstanced(_vertex_buffer.get(), _index_buffer.get(), nullptr, material, 0, batch._instance_count, 0, 0,
                                      6);
        }
    }

    void SpriteBatcher::Clear()
    {
        _instance_data.clear();
        _batches.clear();
    }

    void SpriteBatcher::EnsureInstanceCapacity(u32 required_count)
    {
        if (_instance_capacity >= required_count && _instance_buffer != nullptr)
            return;

        u32 new_capacity = std::max(required_count, std::max(256u, _instance_capacity * 2u));
        _instance_data.reserve(new_capacity);
        _instance_capacity = new_capacity;

        // Allocate instance buffer
        BufferDesc desc;
        desc._element_num = new_capacity;
        desc._element_size = sizeof(SpriteInstanceData);
        desc._size = new_capacity * sizeof(SpriteInstanceData);
        desc._target = EGPUBufferTarget::kStructured;
        _instance_buffer = GPUBuffer::Create(desc, "SpriteInstanceBuffer");
        GraphicsContext::Get().CreateResource(_instance_buffer.get());
    }

    void SpriteBatcher::BuildInstanceData(const Vector<SpriteRenderData> &render_data)
    {
        EnsureInstanceCapacity((u32)render_data.size());

        for (const auto &sprite : render_data)
        {
            SpriteInstanceData inst;
            inst._local_to_world = sprite._local_to_world;
            inst._uv_rect = sprite._uv_rect;
            inst._color = Vector4f(sprite._color.r, sprite._color.g, sprite._color.b, sprite._color.a);
            inst._size_pivot = Vector4f(sprite._size.x, sprite._size.y, sprite._pivot.x, sprite._pivot.y);
            inst._texture_index = 0u;
            inst._entity_id = sprite._entity_id;

            inst._flags = 0u;
            if (sprite._flip_x)
                inst._flags |= kSpriteFlagFlipX;
            if (sprite._flip_y)
                inst._flags |= kSpriteFlagFlipY;

            inst._padding = 0u;

            _instance_data.push_back(inst);
        }
    }

    void SpriteBatcher::BuildBatches(const Vector<SpriteRenderData> &render_data)
    {
        _batches = BuildBatchesForTesting(render_data);
    }

    Vector<SpriteBatch> SpriteBatcher::BuildBatchesForTesting(const Vector<SpriteRenderData> &render_data)
    {
        Vector<SpriteBatch> batches;
        batches.reserve(render_data.size());

        for (u32 i = 0; i < render_data.size(); ++i)
        {
            const auto &sprite = render_data[i];

            SpriteBatchKey key;
            key._material = sprite._material;
            key._texture = sprite._texture;
            key._blend_mode = sprite._blend_mode;

            if (batches.empty() || !(batches.back()._key == key))
            {
                SpriteBatch batch;
                batch._key = key;
                batch._instance_offset = i;
                batch._instance_count = 1u;
                batches.push_back(batch);
            }
            else
            {
                batches.back()._instance_count++;
            }
        }

        return batches;
    }

}// namespace Ailu::Render
