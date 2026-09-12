#pragma once
#ifndef __GFX_PIPELINE_STATE_H__
#define __GFX_PIPELINE_STATE_H__

#include <atomic>
#include <utility>

#include "AlgFormat.h"
#include "Framework/Common/Container.hpp"
#include "Framework/Common/Hash.hpp"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/List.h"
#include "Framework/Core/Containers/Array.h"
#include "PipelineState.h"
#include "RenderingStates.h"
#include "CoreType.h"
#include "Shader.h"
#include "Render/FrameDebugger/FrameCaptureReason.h"


//ref
//https://zhuanlan.zhihu.com/p/582020846
//https://alpqr.github.io/qtrhi/qrhigraphicspipeline.html#CompareOp-enum
//https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12

namespace Ailu::Render::FrameDebugger { class FrameCaptureWriter; }

namespace Ailu::Render
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

    struct UploadParamsGPSO : public UploadParams
    {
        u16 _pass_index;
        ShaderVariantHash _variant_hash;
        UploadParamsGPSO(u16 pass_index, ShaderVariantHash variant_hash) : _pass_index(pass_index), _variant_hash(variant_hash) {}
    };

    using PSOHash = ALHash::Hash<128>;
    struct PSOCreateRequest
    {
        PSOHash _hash;
        Shader *_shader = nullptr;
        u16 _pass_index = 0u;
        ShaderVariantHash _variant_hash = 0u;
    };
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
        static Ref<GraphicsPipelineStateObject> Create(const GraphicsPipelineStateInitializer &initializer);
        static PSOHash ConstructPSOHash(u8 input_layout, u64 shader, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state);
        static void ConstructPSOHash(PSOHash &hash, u8 input_layout, u64 shader, u8 blend_state, u8 raster_state, u8 ds_state, u8 rt_state);
        static PSOHash ConstructPSOHash(const GraphicsPipelineStateInitializer &initializer, u16 pass_index = 0, ShaderVariantHash variant_hash = 0);
        static void ExtractPSOHash(const PSOHash &pso_hash, u8 &input_layout, u64 &shader, u8 &blend_state, u8 &raster_state, u8 &ds_state, u8 &rt_state);
        static void ExtractPSOHash(const PSOHash &pso_hash, u64 &shader);

        GraphicsPipelineStateObject(const GraphicsPipelineStateInitializer &initializer);
        virtual ~GraphicsPipelineStateObject() = default;
        bool IsValidPipelineResource(const EBindResDescType &res_type, i16 slot) const;
        bool IsValidPipelineResource(const EBindResDescType &res_type, const String &name) const;
        const PSOHash &Hash() const { return _hash; };
        const String &SlotToName(u8 slot) const;
        const i16 NameToSlot(const String &name) const;
        const GraphicsPipelineStateInitializer &StateDescriptor() const { return _state_desc; };
        u32 GetBindResourceSignature() const { return _bind_res_signature; };

    protected:
        u8 _per_frame_cbuf_bind_slot = 255u;
        std::set<String> _defines;
        String _pass_name;
        std::unordered_map<std::string, ShaderBindResourceInfo> *_p_bind_res_desc_infos = nullptr;
        std::unordered_multimap<i16, EBindResDescType> _bind_res_desc_type_lut;
        std::unordered_map<u8, String> _bind_res_name_lut;
        PSOHash _hash;
        GraphicsPipelineStateInitializer _state_desc;
        u8 _stencil_ref = 0u;
        u32 _bind_res_signature = 0u;
        u16 _max_slot = 0u;
    };

    class AILU_API GraphicsPipelineStateMgr
    {
        friend class CommandRecordingContext;

    public:
        static void Init();
        static void Shutdown();
        static GraphicsPipelineStateMgr &Get();
        static void BuildPSOCache();
        static void AddPSO(Ref<GraphicsPipelineStateObject> p_gpso);
        static void UpdateAllPSOObject();
        GraphicsPipelineStateMgr();
        ~GraphicsPipelineStateMgr();
        GraphicsPipelineStateObject *FindReadyPSO(const PSOHash &hash);
        void RequestPSOCreation(const PSOCreateRequest &request);
        void ProcessPendingPSOCreationRequests();
        void ProcessPendingShaderCompiles();
        void OnShaderCompiled(Shader *shader, u16 pass_id, ShaderVariantHash variant_hash)
        {
            ShaderCompiledInfo compile_info{shader, pass_id, variant_hash};
            while (_shader_compiled_queue.Full())
            {
                std::this_thread::yield();
            }
            _shader_compiled_queue.Push(compile_info);
        };

    private:
        struct ShaderCompiledInfo
        {
            Shader *_shader;
            u16 _pass_index;
            ShaderVariantHash _variant_hash;
        };
        void ProcessCompiledShader(const ShaderCompiledInfo &info);
        GraphicsPipelineStateObject *CreatePSO(const PSOCreateRequest &request);

        Vector<Ref<GraphicsPipelineStateObject>> _update_pso{};
        std::mutex _pso_lock;
        HashMap<PSOHash, Ref<GraphicsPipelineStateObject>, PSOHash::HashFunc> _pso_library{};
        u32 s_reserved_pso_id = 32u;
        Core::LockFreeQueue<ShaderCompiledInfo, 256> _shader_compiled_queue;
        Core::LockFreeQueue<PSOCreateRequest, 1024> _pso_create_queue;
    };

    class AILU_API CommandRecordingContext
    {
    public:
        // 因为 PSO 还没建好而丢掉绘制的累计次数：录制时报 miss 只是异步补建 PSO，
        // 这一帧的绘制是真的丢了。只画一次的地方（资源预览）必须据此重绘，否则只会得到空图。
        static u64 PSOMissCount() { return s_pso_miss_count.load(std::memory_order_relaxed); }
        void Clear();
        void ConfigureShader(Shader* shader, u16 pass_index, ShaderVariantHash variant_hash, const u64 &shader_hash);
        void ConfigureShader(Shader* shader, const u64 &shader_hash);
        void ConfigureVertexInputLayout(const u8 &hash);
        void ConfigureTopology(const u8 &hash);
        void ConfigureBlendState(const u8 &hash);
        void ConfigureRasterizerState(const u8 &hash);
        void ConfigureDepthStencilState(const u8 &hash);
        void SetRenderTargetState(EALGFormat color_format, EALGFormat depth_format, u8 color_rt_id = 0);
        void SetRenderTargetState(EALGFormat color_format, u8 color_rt_id = 0);
        void ResetRenderTargetState();
        void SubmitBindResource(const PipelineResource& resource)
        {
            ++_rendering_states_data.PipelineResourceSubmitCount;
            _resolved_bind_res.emplace_back(resource);
        };
        GraphicsPipelineStateObject *FindMatchPSO();
        CommandRenderingStatesData& RenderingStatesData() { return _rendering_states_data; }
        void AccumulateRenderingStatesData(const CommandRenderingStatesData &data) { _rendering_states_data.Accumulate(data); }
        const Vector<PipelineResource>& ResolvedBindResources() { return _resolved_bind_res; }
        void ClearResolvedBindResources() { _resolved_bind_res.clear(); }
    private:
        void MarkPSODirty()
        {
            _is_pso_dirty = true;
            _pso_request_submitted = false;
        }
#if AILU_ENABLE_FRAME_DEBUGGER
        FrameDebugger::EPsoDirtyReason _capture_pso_dirty_reasons = FrameDebugger::EPsoDirtyReason::kNone;
        FrameDebugger::FrameCaptureWriter *_capture_writer = nullptr;
    public:
        void SetCaptureWriter(FrameDebugger::FrameCaptureWriter *writer) { _capture_writer = writer; }
#endif

    public:
        void MarkPSODirty(FrameDebugger::EPsoDirtyReason reason)
        {
            _is_pso_dirty = true;
            _pso_request_submitted = false;
#if AILU_ENABLE_FRAME_DEBUGGER
            _capture_pso_dirty_reasons = _capture_pso_dirty_reasons | reason;
#endif
        }

#if AILU_ENABLE_FRAME_DEBUGGER
        FrameDebugger::EPsoDirtyReason TakePsoDirtyReasons()
        {
            FrameDebugger::EPsoDirtyReason r = _capture_pso_dirty_reasons;
            _capture_pso_dirty_reasons = FrameDebugger::EPsoDirtyReason::kNone;
            return r;
        }
#endif
        Vector<PipelineResource> _resolved_bind_res;
        RenderTargetState _render_target_state;
        inline static std::atomic<u64> s_pso_miss_count{0u};
        PSOHash _cur_pos_hash{};
        u64 _hash_shader = 0u;            // 4~35 32
        u8 _hash_input_layout = 0u;       //0~3 4
        u8 _hash_topology = 0u;           //36~37 2
        u8 _hash_blend_state = 0u;        // 38~40 3
        u8 _hash_raster_state = 0u;       // 41 ~ 43 3
        u8 _hash_depth_stencil_state = 0u;// 44~46 3
        u8 _hash_rt_state = 0u;           // 44~46 3
        GraphicsPipelineStateObject* _current_pso = nullptr;
        ETopology _current_topology = ETopology::kTriangle;
        u8 _stencil_ref = 0u;
        bool _is_pso_dirty = true;
        bool _pso_request_submitted = false;
        Shader* _current_shader = nullptr;
        u16 _current_pass_index = 0u;
        ShaderVariantHash _current_variant_hash = 0u;
        CommandRenderingStatesData _rendering_states_data;
    };
}// namespace Ailu


#endif// !GFX_PIPELINE_STATE_H__
