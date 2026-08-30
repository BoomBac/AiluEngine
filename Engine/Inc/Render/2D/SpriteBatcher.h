#pragma once
#ifndef __SPRITE_BATCHER_H__
#define __SPRITE_BATCHER_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "SpriteRenderData.h"

namespace Ailu::Render
{
    class CommandBuffer;
    class RenderTexture;
    class Material;
    class VertexBuffer;
    class IndexBuffer;
    class GPUBuffer;
    class ConstantBuffer;

    class AILU_API SpriteBatcher
    {
    public:
        SpriteBatcher();
        ~SpriteBatcher();

        void Initialize();
        void Shutdown();

        void Build(const Vector<SpriteRenderData> &render_data);
        void Render(CommandBuffer *cmd, RenderTexture *color_target, RenderTexture *depth_target);
        void RenderWithMaterial(CommandBuffer *cmd, RenderTexture *color_target, RenderTexture *depth_target, Material *material);
        void RenderWithMaterial(CommandBuffer *cmd, Material *material);

        GPUBuffer *InstanceBuffer() const { return _instance_buffer.get(); }

        void Clear();
        static Vector<SpriteBatch> BuildBatchesForTesting(const Vector<SpriteRenderData> &render_data);

    private:
        void CreateStaticGeometry();
        void EnsureInstanceCapacity(u32 required_count);
        void BuildInstanceData(const Vector<SpriteRenderData> &render_data);
        void BuildBatches(const Vector<SpriteRenderData> &render_data);

    private:
        Ref<VertexBuffer> _vertex_buffer;
        Ref<IndexBuffer> _index_buffer;
        Ref<GPUBuffer> _instance_buffer;

        Ref<ConstantBuffer> _per_obj_cb;

        Vector<SpriteInstanceData> _instance_data;
        Vector<SpriteBatch> _batches;

        Ref<Material> _default_material;

        u32 _instance_capacity = 0u;
    };
}// namespace Ailu::Render

#endif// !__SPRITE_BATCHER_H__
