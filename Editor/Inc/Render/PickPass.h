#pragma once
#ifndef __PICK_PASS__
#define __PICK_PASS__
#include <Render/Pass/RenderPass.h>
#include <Scene/Component.h>

namespace Ailu
{
    namespace Editor
    {
        class PickPass : public RenderPass
        {
        public:
            PickPass();
            ~PickPass();
            void Setup(RenderTexture *pick_buf, RenderTexture *pick_buf_depth);
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;

        private:
            void DrawLightGizmo(const Transform &transf, ECS::LightComponent *comp);

        private:
            Scope<Material> _pick_gen;
            Scope<Material> _select_gen;
            Scope<Material> _editor_outline;
            RenderTexture *_color;
            RenderTexture *_depth;
        };
        class PickFeature : public RenderFeature
        {
        public:
            PickFeature();
            ~PickFeature();
            void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) final;
            u32 GetPickID(u16 x, u16 y) const;

        private:
            PickPass _pick;
            u32 _pick_id = 0u;
            Ref<RenderTexture> _pick_buf = nullptr;
            Ref<RenderTexture> _pick_buf_depth = nullptr;
            Ref<ComputeShader> _read_pickbuf;
            GPUBuffer *_readback_buf;
            Vector4f _params;
        };
    };// namespace Editor
}// namespace Ailu

#endif// !PICK_PASS__
