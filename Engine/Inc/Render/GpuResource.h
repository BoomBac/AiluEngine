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
#include <deque>
#include <memory>
#include <mutex>
#include <type_traits>
#include "generated/GpuResource.gen.h"
namespace Ailu::Render
{
    inline constexpr u32 kInvalidGpuHandleIndex = UINT32_MAX;

    struct GpuResourceTag {};
    struct BufferTag {};
    struct TextureTag {};
    struct ShaderTag {};

    template<typename tag_t>
    struct GpuHandle
    {
        u32 _index = kInvalidGpuHandleIndex;
        u32 _generation = 0u;

        constexpr bool IsValid() const { return _index != kInvalidGpuHandleIndex; }
        constexpr bool operator==(const GpuHandle &) const = default;
    };

    using GpuResourceHandle = GpuHandle<GpuResourceTag>;
    using BufferHandle = GpuHandle<BufferTag>;
    using GpuTextureHandle = GpuHandle<TextureTag>;
    using ShaderHandle = GpuHandle<ShaderTag>;

    static_assert(std::is_trivially_copyable_v<GpuResourceHandle>);

    class GpuResource;
    enum class EGpuResourceSlotState : u8
    {
        kFree,
        kAlive,
    };

    struct GpuResourceSlot
    {
        u32 _generation = 1u;
        EGpuResourceSlotState _state = EGpuResourceSlotState::kFree;
        std::unique_ptr<GpuResource> _resource;
    };

    struct RetiredGpuResource
    {
        u64 _fence = 0u;
        std::unique_ptr<GpuResource> _resource;
    };

    class AILU_API GpuResourceRegistry
    {
    public:
        GpuResourceRegistry() = default;
        GpuResourceRegistry(const GpuResourceRegistry &) = delete;
        GpuResourceRegistry &operator=(const GpuResourceRegistry &) = delete;

        static GpuResourceRegistry &Get();

        GpuResourceHandle Add(std::unique_ptr<GpuResource> resource);
        GpuResource *Resolve(GpuResourceHandle handle);
        const GpuResource *Resolve(GpuResourceHandle handle) const;
        template<typename tag_t>
        GpuResource *Resolve(GpuHandle<tag_t> handle)
        {
            return Resolve(GpuResourceHandle{handle._index, handle._generation});
        }
        template<typename tag_t>
        const GpuResource *Resolve(GpuHandle<tag_t> handle) const
        {
            return Resolve(GpuResourceHandle{handle._index, handle._generation});
        }
        void Retire(GpuResourceHandle handle, u64 fence);
        void Release(GpuResourceHandle handle);
        Vector<GpuResourceHandle> TakePendingReleases();
        void Collect(u64 completed_fence);
        void Clear();

    private:
        Vector<GpuResourceSlot> _slots;
        Vector<u32> _free_indices;
        std::deque<RetiredGpuResource> _retired_resources;
        std::mutex _pending_release_mutex;
        Vector<GpuResourceHandle> _pending_releases;
    };


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
        GpuResourceHandle Handle() const { return _handle; }
        template<typename tag_t>
        GpuHandle<tag_t> TypedHandle() const { return {_handle._index, _handle._generation}; }
        void SetHandle(GpuResourceHandle handle) { _handle = handle; }
        void SetCreatedFence(u64 fence)
        {
            _created_fence = fence;
            _is_ready_for_rendering = false;
        }
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
        u64 _created_fence = ~u64(0);
        GpuResourceHandle _handle;
        EGpuResType _res_type;
        bool _is_ready_for_rendering = false;
    };

    template<typename resource_t>
    Ref<resource_t> AdoptGpuResource(resource_t *resource)
    {
        static_assert(std::is_base_of_v<GpuResource, resource_t>);
        if (resource == nullptr)
            return nullptr;

        const GpuResourceHandle handle = GpuResourceRegistry::Get().Add(std::unique_ptr<GpuResource>(resource));
        return Ref<resource_t>(resource, [handle](resource_t *)
        {
            GpuResourceRegistry::Get().Release(handle);
        });
    }
}// namespace Ailu

#endif// !FRAME_RESOURCE_H__
