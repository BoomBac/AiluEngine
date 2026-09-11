#pragma once
#ifndef __FRAME_RESOURCE_H__
#define __FRAME_RESOURCE_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/ReflectionMacros.h"
#include "Objects/Object.h"
#include "CoreType.h"
#include "RendererAPI.h"
#include "generated/GpuResource.gen.h"
namespace Ailu::Render
{

    struct NativeHandle
    {
        RendererAPI::ERenderAPI _type = RendererAPI::ERenderAPI::kNone;
        void* _res = nullptr;

        template<typename T>
        T* As() const
        {
            return reinterpret_cast<T*>(_res);
        }

        explicit operator bool() const
        {
            return _res != nullptr;
        }
    };

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

    struct BuildParams
    {
        virtual ~BuildParams() = default;
        bool _is_update = false;
    };

    class RHICommandBuffer;

    class GraphicsContext;
    ACLASS()
    class AILU_API GpuResource : public Object
    {
        GENERATED_BODY()
    public:
        static u64 TotalMemSize() { return s_total_mem_size; }
    public:
        GpuResource();
        virtual ~GpuResource() override;
        virtual void RequireState(RHICommandBuffer *rhi_cmd, EResourceState state, u32 sub_res = kTotalSubRes)
        {
            AL_ASSERT(rhi_cmd != nullptr);
        }
        virtual void UavBarrier(RHICommandBuffer *rhi_cmd)
        {
            AL_ASSERT(rhi_cmd != nullptr);
        }
        virtual NativeHandle NativeResource() {AL_ASSERT(true); return {};}
        void Apply();
        void ApplySync();
        void Upload(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params);
        void Bind(RHICommandBuffer* rhi_cmd, const BindParams& params);
        u64 GetSize() const {return _mem_size;}
        u64 GetFenceValue() const {return _fence_value;}
        void Track(u64 fence = 0u);
        void SetCreatedFence(u64 fence)
        {
            _created_fence = fence;
            _is_ready_for_rendering = false;
        }
        bool MarkUsedByCommand(u64 command_epoch);
        bool IsReferenceByGpu() const;
        EGpuResType GetResourceType() const {return _res_type;}
        bool IsReady();
    public:
        //GpuResUsageTrack _usage_track;
    protected:
        virtual void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params){};
        virtual void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params);
    protected:
        inline static u64 s_total_mem_size = 0u;
        u64 _mem_size = 0u;
        u64 _fence_value = 0u;
        u64 _created_fence = ~u64(0);
        u64 _last_marked_command_epoch = 0u;
        EGpuResType _res_type;
        bool _is_ready_for_rendering = false;
    };
}// namespace Ailu

#endif// !FRAME_RESOURCE_H__
