#pragma once
#ifndef __RENDER_PASS__
#define __RENDER_PASS__
#include "Objects/Object.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderingData.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "generated/RenderFeature.gen.h"

namespace Ailu
{
    namespace Render
    {
        DECLARE_ENUM(ERenderPassEvent, KBeforeRender = 0, kBeforeShaodwMap = 50, kAfterShadowMap = 100,
                     kBeforeGbuffer = 150, kAfterGbuffer = 200, kBeforeDeferedLighting = 250, kAfterDeferedLighting = 300, kBeforeSkybox = 350, kAfterSkybox = 400,
                     kBeforeTransparent = 450, kAfterTransparent = 500, kBeforePostprocess = 550, kAfterPostprocess = 600, kAfterRender = 650)
        class AILU_API IRenderPass
        {
        public:
            virtual ~IRenderPass() = default;
            /// <summary>
            /// 执行RenderGraph setup工作，声明输入输出
            /// </summary>
            /// <param name="graph"></param>
            virtual void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) = 0;
            virtual void Execute(GraphicsContext *context, RenderingData &rendering_data) = 0;
            virtual void BeginPass(GraphicsContext *context) = 0;
            virtual void EndPass(GraphicsContext *context) = 0;
            virtual const String &GetName() const = 0;
            virtual const bool IsActive() const = 0;
            virtual const void SetActive(bool is_active) = 0;
        };
        ACLASS()
        class AILU_API RenderPass : public Object,public IRenderPass
        {
            GENERATED_BODY();
            DISALLOW_COPY_AND_ASSIGN(RenderPass)
        public:
            RenderPass() : Object(), _is_active(true) {};
            RenderPass(const String &name) : Object(name), _is_active(true) {};
            virtual void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) override {};
            virtual void Execute(GraphicsContext *context, RenderingData &rendering_data) override {};
            virtual void BeginPass(GraphicsContext *context) override {};
            virtual void EndPass(GraphicsContext *context)
            {
                RenderTexture::ResetRenderTarget();
            };
            virtual const String &GetName() const final { return _name; };
            virtual const bool IsActive() const final { return _is_active; };
            virtual const void SetActive(bool is_active) final { _is_active = is_active; };
            ERenderPassEvent::ERenderPassEvent _event = ERenderPassEvent::KBeforeRender;
            bool operator<(const RenderPass &other) const { return _event < other._event; }

        protected:
            bool _is_active;
        };

        class Renderer;
        class AILU_API RenderFeature : public Object
        {
            DISALLOW_COPY_AND_ASSIGN(RenderFeature)
        public:
            RenderFeature(const String &name): Object(name){};
            ~RenderFeature() {};
            virtual void AddRenderPasses(Renderer &renderer, const RenderingData & rendering_data) {};
            bool IsActive() const { return _is_active; };
            void SetActive(bool is_active) { _is_active = is_active; };

        protected:
            bool _is_active;
        };
    }
}// namespace Ailu

#endif// !RENDER_PASS__
