#pragma once
#ifndef __RENDER_PASS__
#define __RENDER_PASS__
#include "Objects/Object.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderingData.h"
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
            ~IRenderPass() = default;
            virtual void Execute(GraphicsContext *context, RenderingData &rendering_data) = 0;
            virtual void BeginPass(GraphicsContext *context) = 0;
            virtual void EndPass(GraphicsContext *context) = 0;
            virtual const String &GetName() const = 0;
            virtual const bool IsActive() const = 0;
            virtual const void SetActive(bool is_active) = 0;
        };
        ACLASS()
        class AILU_API RenderPass : public IRenderPass, public Object
        {
            GENERATED_BODY();
            DISALLOW_COPY_AND_ASSIGN(RenderPass)
        public:
            RenderPass(const String &name) : Object(name), _is_active(true) {};
            virtual void Execute(GraphicsContext *context, RenderingData &rendering_data) override {};
            virtual void BeginPass(GraphicsContext *context) override {};
            virtual void EndPass(GraphicsContext *context);
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
            RenderFeature(const String &name);
            ~RenderFeature();
            virtual void AddRenderPasses(Renderer &renderer, RenderingData &rendering_data) {};
            bool IsActive() const { return _is_active; };
            void SetActive(bool is_active) { _is_active = is_active; };

        protected:
            bool _is_active;
        };

        class ForwardPass : public RenderPass
        {
        public:
            ForwardPass();
            ~ForwardPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Ref<Material> shader_state_mat;
            u16 _error_shader_pass_id, _compiling_shader_pass_id;
            Map<u32, Ref<Material>> _transparent_replacement_materials;
            Shader *_forward_lit_shader;
        };

        class CopyColorPass : public RenderPass
        {
        public:
            CopyColorPass();
            ~CopyColorPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Material *_p_blit_mat;
            Mesh *_p_quad_mesh;
            ConstantBuffer *_p_obj_cb;
            Rect _half_sceen_rect;
        };

        class CopyDepthPass : public RenderPass
        {
        public:
            CopyDepthPass();
            ~CopyDepthPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Material *_p_blit_mat;
            ConstantBuffer *_p_obj_cb;
            RTHandle _depth_tex_handle;
        };

        class ShadowCastPass : public RenderPass
        {
        public:
            ShadowCastPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Scope<RenderTexture> _p_mainlight_shadow_map;
            Scope<RenderTexture> _p_addlight_shadow_maps;
            Scope<RenderTexture> _p_point_light_shadow_maps;
        };

        class AILU_API CubeMapGenPass : public RenderPass
        {
        public:
            CubeMapGenPass(Texture *src_tex, u16 size = 512u);
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
            Scope<RenderTexture> _src_cubemap;
            Scope<RenderTexture> _prefilter_cubemap;
            Scope<RenderTexture> _radiance_map;

        private:
            bool _is_src_cubemap = false;
            Texture *_input_src = nullptr;
            CBufferPerCameraData _camera_data[6];
            Scope<ConstantBuffer> _per_camera_cb[6];
            Scope<ConstantBuffer> _per_obj_cb;
            Matrix4x4f _world_mat;
            Rect _cubemap_rect;
            Rect _ibl_rect;
            Vector<Ref<Material>> _reflection_prefilter_mateirals;
            Material *_p_gen_material;
            Material *_p_filter_material;
        };
        // 32-bit: standard gbuffer layout
        //ds 24-8
        //g0 lighting-accumulation24 intensity8
        //g1 normalx 16 normaly 16
        //g2 motion vector xy 16 spec-power/intensity 8
        //g3 diffuse-albedo 24 sun-occlusiton 8
        class DeferredGeometryPass : public RenderPass
        {
        public:
            DeferredGeometryPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Array<Rect, 4> _rects;
        };

        class DeferredLightingPass : public RenderPass
        {
        public:
            DeferredLightingPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
            Ref<Material> _p_lighting_material;

        private:
            Ref<Texture2D> _brdf_lut;
            Ref<ComputeShader> _brdflut_gen;
        };

        class SkyboxPass : public RenderPass
        {
        public:
            SkyboxPass();
            void Setup(bool clear_first) { _is_clear = clear_first; }
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Ref<Material> _p_skybox_material;
            Scope<ConstantBuffer> _p_cbuffer;
            Ref<ComputeShader> _p_lut_gen;
            bool _is_clear = false;
            Vector2Int _transmittance_lut_size = Vector2Int(256, 64);
            Vector2Int _mult_scatter_lut_size = Vector2Int(32, 32);
            Vector2Int _sky_lut_size = Vector2Int(192, 192);
            Ref<RenderTexture> _tlut;
            Ref<RenderTexture> _ms_lut;
        };

        class AILU_API GizmoPass : public RenderPass
        {
        public:
            GizmoPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Vector<Scope<ConstantBuffer>> _p_cbuffers;
        };

        class WireFramePass : public RenderPass
        {
        public:
            WireFramePass();
            ~WireFramePass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Ref<Shader> _wireframe_shader;
            Ref<Material> _wireframe_mat;
            std::unordered_map<String, Ref<Material>> _wireframe_mats;
            std::set<WString> _wireframe_shaders;
        };

        class GUIPass : public RenderPass
        {
        public:
            GUIPass();
            ~GUIPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Ref<Shader> _ui_default_shader;
            Ref<VertexBuffer> _vbuf;
            Ref<IndexBuffer> _ibuf;
            Ref<Material> _ui_default_mat;
            Ref<ConstantBuffer> _obj_cb;
        };

        class MotionVectorPass : public RenderPass
        {
        public:
            MotionVectorPass();
            ~MotionVectorPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
        private:
            Ref<Material> _motion_vector_mat;
        };
    }
}// namespace Ailu

#endif// !RENDER_PASS__
