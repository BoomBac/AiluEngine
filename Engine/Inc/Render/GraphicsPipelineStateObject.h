#pragma once
#ifndef __GFX_PIPELINE_STATE_H__
#define __GFX_PIPELINE_STATE_H__

#include "AlgFormat.h"
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Texture.h"


//ref
//https://zhuanlan.zhihu.com/p/582020846
//https://alpqr.github.io/qtrhi/qrhigraphicspipeline.html#CompareOp-enum
//https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

namespace Ailu
{
    struct GraphicsPipelineStateInitializer
    {
        VertexInputLayout _input_layout;       //0~3 4
        Shader *_p_vertex_shader;              //4~13 10
        Shader *_p_pixel_shader;               // 14~35 22
        ETopology _topology;                   //36~37 2
        BlendState _blend_state;               // 38~40 3
        RasterizerState _raster_state;         // 41 ~ 43 3
        DepthStencilState _depth_stencil_state;// 44~46 3
        RenderTargetState _rt_state;           // 47~49 3

        static GraphicsPipelineStateInitializer GetNormalOpaquePSODesc()
        {
            GraphicsPipelineStateInitializer pso_desc{};
            pso_desc._blend_state = BlendState{};
            pso_desc._depth_stencil_state = TStaticDepthStencilState<true, ECompareFunc::kLess>::GetRHI();
            pso_desc._rt_state = RenderTargetState{};
            pso_desc._topology = ETopology::kTriangle;
            pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kSolid>::GetRHI();
            return pso_desc;
        }

        static GraphicsPipelineStateInitializer GetNormalTransparentPSODesc()
        {
            GraphicsPipelineStateInitializer pso_desc{};
            pso_desc._blend_state = TStaticBlendState<true, EBlendFactor::kSrcAlpha, EBlendFactor::kOneMinusSrcAlpha>::GetRHI();
            pso_desc._rt_state = RenderTargetState{};
            pso_desc._depth_stencil_state = TStaticDepthStencilState<false, ECompareFunc::kAlways>::GetRHI();
            pso_desc._topology = ETopology::kTriangle;
            pso_desc._raster_state = TStaticRasterizerState<ECullMode::kBack, EFillMode::kSolid>::GetRHI();
            return pso_desc;
        }
    };

    using PSOHash = ALHash::Hash<128>;
    class CommandBuffer;
    class GraphicsPipelineStateObject
    {
        struct StateHashStruct
        {
            struct BitDesc
            {
                u8 _pos;
                u8 _size;
            };
            inline static const BitDesc kShader = {0, 46};
            inline static const BitDesc kInputlayout = {46, 4};
            //inline static const BitDesc kTopology = {50, 2};
            inline static const BitDesc kBlendState = {52, 3};
            inline static const BitDesc kRasterState = {55, 3};
            inline static const BitDesc kDepthStencilState = {58, 5};
            inline static const BitDesc kRenderTargetState = {63, 4};
        };

    public:
        static Scope<GraphicsPipelineStateObject> Create(const GraphicsPipelineStateInitializer &initializer);
        static PSOHash ConstructPSOHash(u8 input_layout, u64 shader,u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state);
        static void ConstructPSOHash(PSOHash &hash, u8 input_layout, u64 shader, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state);
        static PSOHash ConstructPSOHash(const GraphicsPipelineStateInitializer &initializer, u16 pass_index = 0, ShaderVariantHash variant_hash = 0);
        static void ExtractPSOHash(const PSOHash &pso_hash, u8 &input_layout, u64 &shader,u8 &blend_state, u8 &raster_state, u8 &ds_state, u8 &rt_state);
        static void ExtractPSOHash(const PSOHash &pso_hash, u64 &shader);

        virtual ~GraphicsPipelineStateObject() = default;
        virtual void Build(u16 pass_index = 0, ShaderVariantHash variant_hash = 0) = 0;
        virtual void Bind(CommandBuffer *cmd) = 0;
        virtual void SetPipelineResource(CommandBuffer *cmd, void *res, const EBindResDescType &res_type, u8 slot = 255) = 0;
        virtual bool IsValidPipelineResource(const EBindResDescType &res_type, u8 slot) const = 0;
        virtual const PSOHash &Hash() = 0;
        virtual const String &Name() const = 0;
        virtual const String &SlotToName(u8 slot) = 0;
        virtual const u8 NameToSlot(const String &name) = 0;
        virtual void SetTopology(ETopology topology) = 0;
        virtual void SetStencilRef(u8 ref) = 0;
        virtual const GraphicsPipelineStateInitializer& StateDescriptor() const = 0;
    protected:
        virtual void BindResource(CommandBuffer *cmd, void *res, const EBindResDescType &res_type, u8 slot = 255) = 0;
        struct BindResDescSlotOffset
        {
            u8 kConstBuffer = 32;
            u8 kTexture2D = 64;
            u8 kTextureCube = 96;
            u8 kSampler = 128;
        };
    };

    class GraphicsPipelineStateMgr
    {
    public:
        static void Init();
        static void Shutdown();
        static GraphicsPipelineStateMgr& Get();
        static void BuildPSOCache();
        static void AddPSO(Scope<GraphicsPipelineStateObject> p_gpso);
        static void EndConfigurePSO(CommandBuffer *cmd);
        static void OnShaderRecompiled(Shader *shader, u16 pass_id, ShaderVariantHash variant_hash);
        static void ConfigureShader(const u64 &shader_hash);
        static void ConfigureVertexInputLayout(const u8 &hash);//0~3 4
        static void ConfigureTopology(const u8 &hash);         //36~37 2
        static void ConfigureBlendState(const u8 &hash);       // 38~40 3
        static void ConfigureRasterizerState(const u8 &hash);  // 41 ~ 43 3
        static void ConfigureDepthStencilState(const u8 &hash);// 44~46 3
        static void ConfigureRenderTarget(const u8 &hash);     // 44~46 3
        static void SetRenderTargetState(EALGFormat::EALGFormat color_format, EALGFormat::EALGFormat depth_format, u8 color_rt_id = 0);
        static void SetRenderTargetState(EALGFormat::EALGFormat color_format, u8 color_rt_id = 0);
        //call before cmd->SetRenderTarget
        static void ResetRenderTargetState();

        static bool IsReadyForCurrentDrawCall();

        static void SubmitBindResource(void *res, const EBindResDescType &res_type, u8 slot, u16 priority);
        static void SubmitBindResource(void *res, const EBindResDescType &res_type, const String &name, u16 priority);
        static void UpdateAllPSOObject();
        
    private:
        Scope<GraphicsPipelineStateObject> _gizmo_line_pso;
        Scope<GraphicsPipelineStateObject> _gizmo_tex_pso;
        Queue<Scope<GraphicsPipelineStateObject>> _update_pso{};
        std::mutex _pso_lock;
        std::map<PSOHash, Scope<GraphicsPipelineStateObject>> _pso_library{};
        u32 s_reserved_pso_id = 32u;
        List<PipelineResourceInfo> _bind_resource_list{};
        bool _is_ready = false;
        RenderTargetState _render_target_state;

        PSOHash _cur_pos_hash{};
        u64 _hash_shader;            // 4~35 32
        u8 _hash_input_layout;       //0~3 4
        u8 _hash_topology;           //36~37 2
        u8 _hash_blend_state;        // 38~40 3
        u8 _hash_raster_state;       // 41 ~ 43 3
        u8 _hash_depth_stencil_state;// 44~46 3
        u8 _hash_rt_state;           // 44~46 3
    };
}// namespace Ailu


#endif// !GFX_PIPELINE_STATE_H__