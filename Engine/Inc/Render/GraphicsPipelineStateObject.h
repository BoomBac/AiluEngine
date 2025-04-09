#pragma once
#ifndef __GFX_PIPELINE_STATE_H__
#define __GFX_PIPELINE_STATE_H__

#include <utility>

#include "AlgFormat.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/Container.hpp"
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
    struct PipelineResource
    {
        inline static const u16 kPriorityGlobal  = 0x0u;
        inline static const u16 kPriorityCmd     = 0x1u;
        inline static const u16 kPriorityLocal   = 0x2u;
        struct AddiInfo
        {
            //目前给upload buffer使用
            u64 _gpu_handle = 0u;
            //texture
            void* _native_res_ptr = nullptr;
            u16 _view_index = Texture::kMainSRVIndex;
            u32 _sub_res = UINT32_MAX;
            bool operator==(const AddiInfo &other) const
            {
                return _gpu_handle == other._gpu_handle && _native_res_ptr == other._native_res_ptr && _view_index == other._view_index && _sub_res == other._sub_res;
            }
        };
        EBindResDescType _res_type = EBindResDescType::kUnknown;
        String _name;
        u16 _priority;
        u16 _slot;//构造时不赋值，实际绑定时由pso mgr/pso赋值
        bool _is_compute = false;
        PipelineResource() = default;
        PipelineResource(GpuResource* res, EBindResDescType resType,String name, u16 priority,bool is_compute = false,u16 register_space = 0u)
            : _p_resource(res), _res_type(resType), _name(std::move(name)), _priority(priority) ,_is_compute(is_compute) {}
        PipelineResource(GpuResource* res, EBindResDescType resType,u16 slot, u16 priority,bool is_compute = false,u16 register_space = 0u)
            : _p_resource(res), _res_type(resType), _slot(slot), _priority(priority) ,_is_compute(is_compute) {}
        bool operator<(const PipelineResource &other) const
        {
            return _priority > other._priority;
        }
        bool operator==(const PipelineResource &other) const
        {
            return _res_type == other._res_type && _priority == other._priority && _is_compute == other._is_compute
                   && _p_resource == other._p_resource && _addi_info == other._addi_info;
        }
        PipelineResource(const PipelineResource &other)
        {
            _p_resource = other._p_resource;
            _res_type = other._res_type;
            _name = other._name;
            _priority = other._priority;
            _slot = other._slot;
            _is_compute = other._is_compute;
            _addi_info = other._addi_info;
        }
        PipelineResource(PipelineResource &&other) noexcept
        {
            _p_resource = other._p_resource;
            _res_type = other._res_type;
            _name = std::move(other._name);
            _priority = other._priority;
            _slot = other._slot;
            _is_compute = other._is_compute;
            _addi_info = other._addi_info;
        }
        PipelineResource& operator=(const PipelineResource &other)
        {
            _p_resource = other._p_resource;
            _res_type = other._res_type;
            _name = other._name;
            _priority = other._priority;
            _slot = other._slot;
            _is_compute = other._is_compute;
            _addi_info = other._addi_info;
            return *this;
        }
        PipelineResource& operator=(PipelineResource &&other) noexcept
        {
            _p_resource = other._p_resource;
            _res_type = other._res_type;
            _name = std::move(other._name);
            _priority = other._priority;
            _slot = other._slot;
            _is_compute = other._is_compute;
            _addi_info = other._addi_info;
            return *this;
        }
        GpuResource* _p_resource;
        AddiInfo _addi_info;
    };
    struct UploadParamsGPSO : public UploadParams
    {
        u16 _pass_index;
        ShaderVariantHash _variant_hash;
        UploadParamsGPSO(u16 pass_index,ShaderVariantHash variant_hash) : _pass_index(pass_index), _variant_hash(variant_hash) {}
    };
    using PSOHash = ALHash::Hash<128>;
    class CommandBuffer;
    class GraphicsPipelineStateObject : public GpuResource
    {
        struct StateHashStruct
        {
            struct BitDesc
            {
                u8 _pos;
                u8 _size;
            };
            inline static const BitDesc kShader = {0, 46};
            inline static const BitDesc kInputLayout = {46, 4};
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

        GraphicsPipelineStateObject(const GraphicsPipelineStateInitializer &initializer);
        virtual ~GraphicsPipelineStateObject() = default;
        /// 填充已经验证的管线资源
        void SetPipelineResource(const PipelineResource& pipeline_res);
        void SetPipelineResource(const String& name,GpuResource* res);
        bool IsValidPipelineResource(const EBindResDescType &res_type, i16 slot) const;
        bool IsValidPipelineResource(const EBindResDescType &res_type, const String& name) const;
        const PSOHash &Hash() const {return _hash;};
        const String &SlotToName(u8 slot) const;
        const i16 NameToSlot(const String &name) const;
        virtual void SetTopology(ETopology topology) {_state_desc._topology = topology;};
        void SetStencilRef(u8 ref) {_state_desc._depth_stencil_state._stencil_ref_value;};
        const GraphicsPipelineStateInitializer& StateDescriptor() const {return _state_desc;};
        u32 GetBindResourceSignature() const {return _bind_res_signature;};
        const Array<PipelineResource,32>& GetBindResource(u16 index = 0) const {return _bind_res;};
    protected:
        u8 _per_frame_cbuf_bind_slot = 255u;
        std::set<String> _defines;
        String _pass_name;
        std::unordered_map<std::string, ShaderBindResourceInfo> *_p_bind_res_desc_infos = nullptr;
        std::unordered_multimap<u8, EBindResDescType> _bind_res_desc_type_lut;
        std::unordered_map<u8, String> _bind_res_name_lut;
        PSOHash _hash;
        GraphicsPipelineStateInitializer _state_desc;
        u8 _stencil_ref = 0u;
        u32 _bind_res_signature = 0u;
        u16 _max_slot = 0u;
        Array<PipelineResource,32> _bind_res;
    };

    class AILU_API GraphicsPipelineStateMgr
    {
    public:
        static void Init();
        static void Shutdown();
        static GraphicsPipelineStateMgr& Get();
        static void BuildPSOCache();
        static void AddPSO(Scope<GraphicsPipelineStateObject> p_gpso);
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
        
        //static void SubmitBindResource(GpuResource *res, const EBindResDescType &res_type, u8 slot, u16 priority);
        //static void SubmitBindResource(GpuResource *res, const EBindResDescType &res_type, const String &name, u16 priority);
        static void SubmitBindResource(PipelineResource resource);
        static void UpdateAllPSOObject();
        GraphicsPipelineStateMgr();
        ~GraphicsPipelineStateMgr();
        GraphicsPipelineStateObject* FindMatchPSO();
        void OnShaderCompiled(Shader *shader, u16 pass_id, ShaderVariantHash variant_hash) {
            _shader_compiled_queue->Push({shader, pass_id, variant_hash});
        };
        
    private:
        struct ShaderCompiledInfo
        {
            Shader* _shader;
            u16 _pass_index;
            ShaderVariantHash _variant_hash;
        };
        void ProcessCompiledShader(const ShaderCompiledInfo& info);
    private:

        Scope<GraphicsPipelineStateObject> _gizmo_line_pso;
        Scope<GraphicsPipelineStateObject> _gizmo_tex_pso;
        Queue<Scope<GraphicsPipelineStateObject>> _update_pso{};
        std::mutex _pso_lock;
        std::map<PSOHash, Scope<GraphicsPipelineStateObject>> _pso_library{};
        u32 s_reserved_pso_id = 32u;
        List<PipelineResource> _bind_resource_list{};
        RenderTargetState _render_target_state;
        LockFreeQueue<ShaderCompiledInfo>* _shader_compiled_queue;
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