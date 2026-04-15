//
// Created by 22292 on 2024/11/7.
//

#ifndef AILU_FRAMERESOURCE_H
#define AILU_FRAMERESOURCE_H
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Buffer.h"
#include "Texture.h"
#include "ResourcePool.h"

namespace Ailu::Render
{
    class FrameResource : public Object
    {
        DISALLOW_COPY_AND_ASSIGN(FrameResource)
    public:
        FrameResource();
        ~FrameResource() override;
        Vector<ConstantBuffer *>* GetObjCB();
        ConstantBuffer * GetObjCB(u32 index);
        ConstantBuffer * GetMatCB(u32 index);
        ConstantBuffer * GetCameraCB(u64 hash);
        ConstantBuffer * GetSceneCB(u64 hash);
        GPUBuffer *GetSceneInstanceBuffer(u64 hash);
        GPUBuffer *GetMaterialBuffer() {return _material_buffer ? _material_buffer.get() : nullptr; };
    private:
        Vector<ConstantBuffer *> _obj_cbs;
        Vector<ConstantBuffer *> _mat_cbs;
        Vector<ConstantBuffer *> _camera_cbs;
        Vector<ConstantBuffer *> _scene_cbs;
        Vector<Ref<GPUBuffer>> _scene_instance_buffers;
        Ref<GPUBuffer> _material_buffer;//for ray tracing material data
        Map<u64,u64> _camera_cb_lut;
        Map<u64,u64> _scene_cb_lut;
        Map<u64, u64> _scene_inst_buffer_lut;
    };
    class FrameAllocator;
    class FrameResourceManager
    {
    public:
        using TexturePool = THashableResourcePool<TextureDesc,Texture>;
        using BufferPool = THashableResourcePool<BufferDesc,GPUBuffer>;
        using TextureHandle = TexturePool::PoolResourceHandle;
        using BufferHandle = BufferPool::PoolResourceHandle;
        inline static constexpr u32 kMaxResourceStaleFrame = 15u;
        inline static constexpr u32 kFrameResourceSlotCount = RenderConstants::kFrameCount + 1u;
    public:
        static FrameResourceManager &Get();
        static void Init();
        static void Shutdown();
        FrameResourceManager();
        ~FrameResourceManager();
        void NewFrame();
        void FrameCleanup();
        TextureHandle AllocTexture(TextureDesc desc);
        BufferHandle AllocBuffer(BufferDesc desc);
        void FreeTexture(TextureHandle handle);
        void FreeBuffer(BufferHandle handle);
        void CleanupStaleResources();
        FrameAllocator* GetActiveFrameAllocator() const { return _active_allocator; }
        u32 GetActiveFrameSlot() const { return _active_slot; }
        u32 GetPreviousFrameSlot() const { return _prev_slot; }
    private:
        TexturePool _texture_pool;
        BufferPool _buffer_pool;
        Array<Scope<FrameAllocator>, kFrameResourceSlotCount> _frame_allocators{};
        Array<u64, kFrameResourceSlotCount> _frame_slot_fence_values{};
        FrameAllocator* _active_allocator = nullptr;
        u32 _active_slot = 0u;
        u32 _prev_slot = 0u;
        bool _has_active_slot = false;
    };

}// namespace Ailu

#endif//AILU_FRAMERESOURCE_H
