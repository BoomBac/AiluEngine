#pragma once
#ifndef __FRAME_RESOURCE_H__
#define __FRAME_RESOURCE_H__
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "CoreType.h"
namespace Ailu::Render
{

    class VertexBufferLayout;
    struct BindParams
    {
        u16 _slot = 0u;
        u16 _register = 0u;
        bool _is_compute_pipeline = false;
        bool _is_random_access = false;
        union
        {
            struct
            {
                u32 _view_idx;
                u32 _sub_res;
            } _texture_binder;
            struct
            {
                const VertexBufferLayout* _layout;
            } _vb_binder;
            struct
            {
                u64 _gpu_ptr;
            } _ub_binder;
            struct
            {
                u64 _is_uav;
            } _buffer_binder;
        } _params;
    };
    struct UploadParams
    {
        virtual ~UploadParams() = default;
    };

    class RHICommandBuffer;

    class GraphicsContext;
    class AILU_API GpuResource : public Object
    {
    public:
        static u64 TotalMemSize() { return s_total_mem_size; }
        GpuResource();
        virtual ~GpuResource() override;
        virtual void StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res)
        {
            AL_ASSERT(true);
        };
        virtual void* NativeResource() {return nullptr;};
        void Apply();
        void Upload(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params);
        void Bind(RHICommandBuffer* rhi_cmd, const BindParams& params);
        u64 GetSize() const {return _mem_size;}
        u64 GetFenceValue() const {return _fence_value;}
        void Track(u64 fence = 0u);
        bool IsReferenceByGpu() const;
        EGpuResType GetResourceType() const {return _res_type;}
        bool IsReady() const {return _is_ready_for_rendering;}
    public:
        //GpuResUsageTrack _usage_track;
    protected:
        virtual void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params){};
        virtual void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params);
    protected:
        inline static u64 s_total_mem_size = 0u;
        u64 _mem_size = 0u;
        u64 _fence_value = 0u;
        EResourceState _state;
        EGpuResType _res_type;
        bool _is_ready_for_rendering = false;
    };
}// namespace Ailu

#endif// !FRAME_RESOURCE_H__
