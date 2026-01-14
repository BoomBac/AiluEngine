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
    private:
        Vector<ConstantBuffer *> _obj_cbs;
        Vector<ConstantBuffer *> _mat_cbs;
        Vector<ConstantBuffer *> _camera_cbs;
        Vector<ConstantBuffer *> _scene_cbs;
        Vector<Ref<GPUBuffer>> _scene_instance_buffers;
        Map<u64,u64> _camera_cb_lut;
        Map<u64,u64> _scene_cb_lut;
        Map<u64, u64> _scene_inst_buffer_lut;
    };

    class FrameResourceManager
    {
    public:
        using TexturePool = THashableResourcePool<TextureDesc,Texture>;
        using BufferPool = THashableResourcePool<BufferDesc,GPUBuffer>;
        using TextureHandle = TexturePool::PoolResourceHandle;
        using BufferHandle = BufferPool::PoolResourceHandle;
        inline static constexpr u32 kMaxResourceStaleFrame = 3u;
    public:
        static FrameResourceManager &Get();
        static void Init();
        static void Shutdown();
        FrameResourceManager();
        ~FrameResourceManager();
        void Tick();
        TextureHandle AllocTexture(TextureDesc desc);
        BufferHandle AllocBuffer(BufferDesc desc);
        void FreeTexture(TextureHandle handle);
        void FreeBuffer(BufferHandle handle);
        void CleanupStaleResources();
    private:
        TexturePool _texture_pool;
        BufferPool _buffer_pool;
    };

}// namespace Ailu

#endif//AILU_FRAMERESOURCE_H
