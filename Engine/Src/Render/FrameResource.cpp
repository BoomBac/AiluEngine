//
// Created by 22292 on 2024/11/7.
//

#include "Inc/Render/FrameResource.h"
#include "Inc/Render/GraphicsContext.h"

namespace Ailu::Render
{
#pragma region FrameResource
    FrameResource::FrameResource() : Object("FrameResource")
    {
        for (u32 i = 0; i < RenderConstants::kMaxRenderObjectCount; ++i)
        {
            _obj_cbs.push_back(ConstantBuffer::Create(RenderConstants::kPerObjectDataSize));
        }
    }
    FrameResource::~FrameResource()
    {
        for (auto cb: _obj_cbs)
            DESTORY_PTR(cb);
        for (auto cb: _camera_cbs)
            DESTORY_PTR(cb);
        for (auto cb: _mat_cbs)
            DESTORY_PTR(cb);
        for (auto cb: _scene_cbs)
            DESTORY_PTR(cb);
    }
    ConstantBuffer *FrameResource::GetObjCB(u32 index)
    {
        AL_ASSERT(index < _obj_cbs.size());
        return _obj_cbs[index];
    }
    Vector<ConstantBuffer *>* FrameResource::GetObjCB()
    {
        return &_obj_cbs;
    }
    ConstantBuffer *FrameResource::GetMatCB(u32 index)
    {
        AL_ASSERT(index < _mat_cbs.size());
        return _mat_cbs[index];
    }
    ConstantBuffer *FrameResource::GetCameraCB(u64 hash)
    {
        if (!_camera_cb_lut.contains(hash))
        {
            _camera_cbs.push_back(ConstantBuffer::Create(RenderConstants::kPerCameraDataSize));
            _camera_cbs.back()->Name(std::format("CameraCB_{}", hash));
            _camera_cb_lut[hash] = _camera_cbs.size() - 1;
            return _camera_cbs.back();
        }
        return _camera_cbs[_camera_cb_lut[hash]];
    }
    ConstantBuffer *FrameResource::GetSceneCB(u64 hash)
    {
        if (!_scene_cb_lut.contains(hash))
        {
            _scene_cbs.push_back(ConstantBuffer::Create(RenderConstants::kPerSceneDataSize));
            _scene_cb_lut[hash] = _scene_cbs.size() - 1;
            return _scene_cbs.back();
        }
        return _scene_cbs[_scene_cb_lut[hash]];
    }
    GPUBuffer *FrameResource::GetSceneInstanceBuffer(u64 hash)
    {
        if (!_scene_inst_buffer_lut.contains(hash))
        {
            BufferDesc desc;
            desc._element_num = RenderConstants::kMaxRenderObjectCount;
            desc._element_size = sizeof(ObjectInstanceData);
            desc._is_random_write = false;
            desc._size = desc._element_size * desc._element_num;
            desc._target = EGPUBufferTarget::kConstant | EGPUBufferTarget::kStructured;
            auto buf = GPUBuffer::Create(desc);
            buf->Name(std::format("SceneInstanceBuffer_{}", hash));
            _scene_instance_buffers.push_back(buf);
            _scene_inst_buffer_lut[hash] = _scene_instance_buffers.size() - 1;

            return &*_scene_instance_buffers.back();
        }
        return &*_scene_instance_buffers[_scene_inst_buffer_lut[hash]];
    }
#pragma endregion

#pragma region FrameResourceManager
    static FrameResourceManager* g_frame_resource_manager = nullptr;
    void FrameResourceManager::Init()
    {
        AL_ASSERT(g_frame_resource_manager == nullptr);
        g_frame_resource_manager = AL_NEW(FrameResourceManager);
        LOG_INFO("FrameResourceManager initialized.");
    }
    void FrameResourceManager::Shutdown()
    {
        AL_DELETE(g_frame_resource_manager);
        LOG_INFO("FrameResourceManager shutdown.");
    }
    FrameResourceManager& FrameResourceManager::Get()
    {
        return *g_frame_resource_manager;
    }
    FrameResourceManager::FrameResourceManager()
    {
    }
    FrameResourceManager::~FrameResourceManager()
    {
    }
    void FrameResourceManager::Tick()
    {
        const u64 cur_frame = GraphicsContext::Get().GetFrameCount();
        u32 released_count = 0;
        if (cur_frame % 120 == 0u)
        {
            for (auto &it: _texture_pool)
            {
                auto &[hash, handle] = it;
                if (handle._last_access_frame_count)
                {
                    if (cur_frame - handle._last_access_frame_count > kMaxResourceStaleFrame)
                    {
                        handle._is_available = true;
                        ++released_count;
                    }
                }
            }
            for (auto &it: _buffer_pool)
            {
                auto &[hash, handle] = it;
                if (handle._last_access_frame_count)
                {
                    if (cur_frame - handle._last_access_frame_count > kMaxResourceStaleFrame)
                    {
                        handle._is_available = true;
                        ++released_count;
                    }
                }
            }
        }
        if (cur_frame % 1000 == 0)
        {
            CleanupStaleResources();
        }
    }

    FrameResourceManager::TextureHandle FrameResourceManager::AllocTexture(TextureDesc desc)
    {
        auto it = _texture_pool.Get(desc);
        Texture *out_texture = nullptr;
        if (it.has_value())
        {
            return it.value();
        }
        else
        {
            Ref<Texture> new_res = nullptr;
            if (desc._is_color_target || desc._is_depth_target)
            {
                new_res = RenderTexture::Create(desc);
            }
            else
            {
                if (desc._dimension == ETextureDimension::kTex2D)
                {
                    new_res = Texture2D::Create(desc);
                }
                else if (desc._dimension == ETextureDimension::kTex3D)
                {
                    new_res = Texture3D::Create(desc);
                }
                else
                {
                    AL_ASSERT_MSG(false, "Unsupported texture dimension!");
                }
            }
            if (new_res)
            {
                return _texture_pool.Add(desc, new_res);
            }
        }
        return FrameResourceManager::TextureHandle{0, nullptr};
    }
    FrameResourceManager::BufferHandle FrameResourceManager::AllocBuffer(BufferDesc desc)
    {
        auto it = _buffer_pool.Get(desc);
        if (it.has_value())
        {
            return it.value();
        }
        else
        {
            Ref<GPUBuffer> new_buf = GPUBuffer::Create(desc);
            return _buffer_pool.Add(desc, new_buf);
        }
        return FrameResourceManager::BufferHandle{0, nullptr};
    }
    void FrameResourceManager::FreeTexture(TextureHandle handle)
    {
        _texture_pool.Release(handle);
    }
    void FrameResourceManager::FreeBuffer(BufferHandle handle)
    {
        _buffer_pool.Release(handle);
    }
    void FrameResourceManager::CleanupStaleResources()
    {
        _texture_pool.ReleaseUnused();
        _buffer_pool.ReleaseUnused();
    }
#pragma endregion

}// namespace Ailu::Render
