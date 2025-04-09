#pragma once
#ifndef __FRAME_RESOURCE_H__
#define __FRAME_RESOURCE_H__
#include "GlobalMarco.h"
#include "Objects/Object.h"
namespace Ailu
{
    const static u32 kTotalSubRes = 0XFFFFFFFF;
    enum class EResourceState
    {
        kCommon = 0,
        kVertexAndConstantBuffer = 0x1,
        kIndexBuffer = 0x2,
        kRenderTarget = 0x4,
        kUnorderedAccess = 0x8,
        kDepthWrite = 0x10,
        kDepthRead = 0x20,
        kNonPixelShaderResource = 0x40,
        kPixelShaderResource = 0x80,
        kStreamOut = 0x100,
        kIndirectArgument = 0x200,
        kCopyDest = 0x400,
        kCopySource = 0x800,
        kResolveDest = 0x1000,
        kResolveSource = 0x2000,
        kRaytracingAccelerationStructure = 0x400000,
        kShadingRateSource = 0x1000000,
        kGenericRead = ((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800,
        kAllShaderResource = (0x40 | 0x80),
        kPresent = 0,
        kPredication = 0x200,
        kVideoDecodeRead = 0x10000,
        kVideoDecodeWrite = 0x20000,
        kVideoProcessRead = 0x40000,
        kVideoProcessWrite = 0x80000,
        kVideoEncodeRead = 0x200000,
        kVideoEncodeWrite = 0x800000
    };
    enum class EGpuResType
    {
        kBuffer,
        kTexture,
        kRenderTexture,
        kVertexBuffer,
        kIndexBUffer,
        kConstBuffer,
        kGraphicsPSO,
    };
    enum class EGpuResUsage
    {
        kRead      = 0x01,
        kWrite     = 0x02,
        kReadWrite = 0x03
    };

    struct BindParams
    {
        virtual ~BindParams() = default;
        u16 _slot = 0u;
        u16 _register = 0u;
        bool _is_compute_pipeline = false;
    };
    struct UploadParams
    {
        virtual ~UploadParams() = default;
    };
    class GpuResUsageTrack
    {
    public:
        void PushUsage(const String& name,EGpuResUsage usage,u64 fence_value)
        {
            _usages.emplace_back(name, usage, fence_value);
        }
        void Clear() {_usages.clear();}
        const auto begin() const{return _usages.begin();}
        const auto end() const{return _usages.end();}
    private:
        struct Usage
        {
            String _cmd_name;
            EGpuResUsage _usage;
            u64 _fence_value;
        };
        Vector<Usage> _usages;
    };
    class RHICommandBuffer;
    class GraphicsContext;
    class GpuResource : public Object
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
        void Bind(RHICommandBuffer* rhi_cmd,BindParams* params);
        u32 GetSize() const {return _mem_size;}
        u64 GetFenceValue() const {return _fence_value;}
        void Track(u64 fence = 0u);
        bool IsReferenceByGpu() const;
        EGpuResType GetResourceType() const {return _res_type;}
        bool IsReady() const {return _is_ready_for_rendering;}
    public:
        //GpuResUsageTrack _usage_track;
    protected:
        virtual void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params){};
        virtual void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params);
    protected:
        inline static u64 s_total_mem_size = 0u;
        u32 _mem_size = 0u;
        u64 _fence_value = 0u;
        EResourceState _state;
        EGpuResType _res_type;
        bool _is_ready_for_rendering = false;
    };
}// namespace Ailu

#endif// !FRAME_RESOURCE_H__
