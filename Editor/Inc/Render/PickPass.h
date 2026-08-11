#pragma once
#ifndef __PICK_PASS__
#define __PICK_PASS__
#include <Render/Features/RenderFeature.h>
#include <Render/2D/SpriteBatcher.h>
#include <Render/2D/SpriteRenderData.h>
#include <Scene/Component.h>

namespace Ailu
{
    namespace Render
    {
        class PickPass : public Render::RenderPass
        {
        public:
            PickPass();
            ~PickPass();
            void Setup(RenderTexture *pick_buf, RenderTexture *pick_buf_depth);
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;

        private:
            void DrawLightGizmo(const ECS::TransformComponent& transf, const ECS::LightComponent& comp);
            void CollectSprites(const SceneManagement::Scene &scene, const Camera &camera, bool selected_only);
            void RecordSpritePick(RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data);
            void RecordSpriteSelection(RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &rendering_data, RDG::RGHandle target);

        private:
            Scope<Material> _pick_gen;
            Scope<Material> _select_gen;
            Scope<Material> _sprite_pick_gen;
            Scope<Material> _sprite_select_gen;
            Scope<Material> _editor_outline;
            Scope<Render::SpriteBatcher> _sprite_batcher;
            Vector<Render::SpriteRenderData> _sprite_render_data;
            RenderTexture *_color;
            RenderTexture *_depth;
            RDG::RGHandle _color_handle;
            RDG::RGHandle _depth_handle;
        };
        class PickFeature : public RenderFeature
        {
        public:
            PickFeature();
            ~PickFeature();
            void AddRenderPasses(Renderer &renderer, const RenderingData &rendering_data) final;
            /// @brief 通过读回来的uint值，前24位为entity index，后8位为submesh id
            /// @param x 
            /// @param y 
            /// @param on_value_get 
            void GetPickID(u16 x, u16 y, std::function<void(ECS::Entity,u32)> on_value_get) const;

        private:
            PickPass _pick;
            u32 _pick_id = 0u;
            Ref<RenderTexture> _pick_buf = nullptr;
            Ref<RenderTexture> _pick_buf_depth = nullptr;
            Ref<ComputeShader> _read_pickbuf;
            ComputeShaderKernelId _read_pickbuf_kernel = kInvalidComputeShaderKernelId;
            Ref<GPUBuffer> _readback_buf;
            Vector4f _params;
        };
    }
}// namespace Ailu

#endif// !PICK_PASS__
