#pragma once
#ifndef __COMMON_PASSES_H__
#define __COMMON_PASSES_H__

#include "Assets/AssetRef.h"
#include "RenderFeature.h"
#include "Render/RenderingData.h"
#include <set>
#include <unordered_map>

namespace Ailu
{
    namespace Render
    {
        class ShadowSystem;

        class ForwardPass : public RenderPass
        {
        public:
            ForwardPass();
            ~ForwardPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Ref<Material> shader_state_mat;
            u16 _error_shader_pass_id, _compiling_shader_pass_id;
            Map<u32, Ref<Material>> _transparent_replacement_materials;
            Shader *_forward_lit_shader;
            Ref<Texture2D> _brdf_lut;
            Ref<RenderTexture> _dummy_main_light_shadow_map;
            Ref<RenderTexture> _dummy_add_light_shadow_maps;
            Ref<RenderTexture> _dummy_point_light_shadow_maps;
        };

        class CopyColorPass : public RenderPass
        {
        public:
            CopyColorPass();
            ~CopyColorPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Material *_p_blit_mat;
            Mesh *_p_quad_mesh;
            Ref<ConstantBuffer> _p_obj_cb;
            Rect _half_sceen_rect;
        };

        class CopyDepthPass : public RenderPass
        {
        public:
            CopyDepthPass();
            ~CopyDepthPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Material *_p_blit_mat;
            Ref<ConstantBuffer> _p_obj_cb;
            RTHandle _depth_tex_handle;
        };

        class ShadowCastPass : public RenderPass
        {
        public:
            explicit ShadowCastPass(ShadowSystem *shadow_system);
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            ShadowSystem *_shadow_system = nullptr;
        };

        class AILU_API CubeMapGenPass : public RenderPass
        {
        public:
            CubeMapGenPass(Texture *src_tex, u16 size = 512u);
            CubeMapGenPass(Material *source_material, u16 size = 512u);
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data);
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
            Ref<RenderTexture> _src_cubemap;
            Ref<RenderTexture> _prefilter_cubemap;
            Ref<RenderTexture> _radiance_map;
            bool IsSourceCubemap() const { return _is_src_cubemap; }

        private:
            void Initialize(Texture *src_tex, Material *source_material, u16 size);
            bool _is_src_cubemap = false;
            bool _is_procedural_source = false;
            Texture *_input_src = nullptr;
            RDG::RGHandle _src_map_handle;
            RDG::RGHandle _radiance_handle;
            RDG::RGHandle _env_handle;
            CBufferPerCameraData _camera_data[6];
            Ref<ConstantBuffer> _per_camera_cb[6];
            Ref<ConstantBuffer> _per_obj_cb;
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
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data);
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:

        };

        class DeferredLightingPass : public RenderPass
        {
        public:
            DeferredLightingPass();
            void SetSurfelGI(class SurfelGIFeature *feature) { _surfel_gi = feature; }
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
            Ref<Material> _p_lighting_material;

        private:
            class SurfelGIFeature *_surfel_gi = nullptr;
            Ref<Texture2D> _brdf_lut;
            AssetRef<ComputeShader> _brdflut_gen;
            Ref<RenderTexture> _dummy_main_light_shadow_map;
            Ref<RenderTexture> _dummy_add_light_shadow_maps;
            Ref<RenderTexture> _dummy_point_light_shadow_map;
            Ref<Texture3D> _dummy_volumetric_light;
        };

        class SkyPass : public RenderPass
        {
        public:
            SkyPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Setup(bool clear_first) { _is_clear = clear_first; }
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            Ref<Material> _p_skybox_material;
            Ref<Material> _p_procedural_material;
            Ref<Material> _p_texture_material;
            Ref<ConstantBuffer> _p_cbuffer;
            bool _is_clear = false;
        };

        class AILU_API GizmoPass : public RenderPass
        {
        public:
            GizmoPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
        };

        class WireFramePass : public RenderPass
        {
        public:
            WireFramePass();
            ~WireFramePass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
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
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;

        private:
            AssetRef<Shader> _ui_default_shader;
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
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
        private:
            Ref<Material> _motion_vector_mat;
        };

        class HZBPass : public RenderPass
        {
        public:
            HZBPass();
            ~HZBPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
            void EndPass(GraphicsContext *context) final;
        private:
            AssetRef<ComputeShader> _hzb_gen;
            ComputeShaderKernelId _hzb_kernel = kInvalidComputeShaderKernelId;
        };

        class DepthOnlyPass : public RenderPass
        {
        public:
            DepthOnlyPass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) final;
        };
    }
}

#endif//
