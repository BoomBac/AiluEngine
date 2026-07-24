#include "Render/2D/Sprite.h"
#include "Render/2D/SpriteRenderPass.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/ResourcePool.h"
#include "Scene/Scene.h"
#include "pch.h"

namespace Ailu::Render
{
    SpriteRenderPass::SpriteRenderPass()
        : RenderPass("SpriteRenderPass")
    {
        _event = ERenderPassEvent::kBeforeSprite;
        _batcher = MakeScope<SpriteBatcher>();
        _batcher->Initialize();
    }

    void SpriteRenderPass::OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        if (!rendering_data._scene || !rendering_data._camera)
            return;

        CollectSprites(*rendering_data._scene, *rendering_data._camera);

        if (_render_data.empty())
            return;

        SortSprites();
        _batcher->Build(_render_data);

        graph.AddPass(
            "SpriteRenderPass",
            RDG::PassDesc(),
            [&](RDG::RenderGraphBuilder &builder)
            {
                // Write to color target
                rendering_data._rg_handles._color_target =
                    builder.Write(rendering_data._rg_handles._color_target);
                // Read depth (optional, for depth testing)
                builder.Read(rendering_data._rg_handles._depth_target);
            },
            [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                RenderTexture *color_target =
                    graph.Resolve<RenderTexture>(data._rg_handles._color_target);
                RenderTexture *depth_target =
                    graph.Resolve<RenderTexture>(data._rg_handles._depth_target);

                _batcher->Render(cmd, color_target, depth_target);
            });
    }

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

    void SpriteRenderPass::CollectSprites(const SceneManagement::Scene &scene, const Camera &camera)
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

            if (!transform || !sprite_renderer._visible)
                continue;

            // Sprite pointer is stored directly on the component
            const Sprite *sprite = sprite_renderer._sprite;
            if (!sprite)
                continue;

            // Sprite stores texture as direct Ref<Texture2D>
            Texture *texture_ptr = sprite->_texture.get();
            if (!texture_ptr)
                continue;

            SpriteRenderData render_data;
            render_data._local_to_world = transform->GetWorldMatrix();
            render_data._uv_rect = sprite->_uv_rect;
            render_data._color = sprite_renderer._color;
            render_data._size = sprite->_size;
            render_data._pivot = sprite->_pivot;
            render_data._texture = texture_ptr;
            render_data._material = sprite_renderer._material.get();
            render_data._sorting_layer = sprite_renderer._sorting_layer;
            render_data._order_in_layer = sprite_renderer._order_in_layer;
            render_data._blend_mode = sprite_renderer._blend_mode;
            render_data._flip_x = sprite_renderer._flip_x;
            render_data._flip_y = sprite_renderer._flip_y;
            render_data._entity_id = static_cast<u32>(entity);

            const Vector3f world_position = transform->GetPosition();
            render_data._distance_to_camera = Magnitude(world_position - camera.Position());

            if (!IsVisible(render_data, camera))
                continue;

            _render_data.emplace_back(std::move(render_data));
        }
    }

    void SpriteRenderPass::SortSprites()
    {
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
    }

    bool SpriteRenderPass::IsVisible(const SpriteRenderData &sprite, const Camera &camera) const
    {
        // Phase 1: always visible (culling is Phase 4)
        return true;
    }

}// namespace Ailu::Render
