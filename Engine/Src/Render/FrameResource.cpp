//
// Created by 22292 on 2024/11/7.
//

#include "Inc/Render/FrameResource.h"
#include "Inc/Render/GraphicsContext.h"
#include "Inc/Render/FrameAllocator.h"
#include "RHI/DX12/UploadBuffer.h"

namespace Ailu::Render
{
#pragma region FrameResource
    FrameResource::FrameResource() : Object("FrameResource")
    {
        BufferDesc desc;
        desc._element_num = RenderConstants::kMaxMaterialDataCount;
        desc._element_size = sizeof(MaterialData);
        desc._is_random_write = false;
        desc._size = desc._element_size * desc._element_num;
        desc._target = EGPUBufferTarget::kConstant | EGPUBufferTarget::kStructured;
        _material_buffer = GPUBuffer::Create(desc);
        _material_buffer->Name(std::format("GlobalMaterialBuffer"));
        desc._element_num = RenderConstants::kMaxRenderObjectCount;
        desc._element_size = sizeof(u32);
        desc._is_random_write = false;
        desc._size = desc._element_num * desc._element_size;
        // This is a frame-slot scoped upload buffer.  Keeping it on an upload
        // heap makes every allocation visible to draws recorded in this slot
        // without issuing a GPU copy for each non-contiguous batch.
        desc._target = EGPUBufferTarget::kConstant | EGPUBufferTarget::kStructured;
        _primitive_index_buffer = GPUBuffer::Create(desc);
        _primitive_index_buffer->Name("FramePrimitiveIndexBuffer");
        _primitive_indices.reserve(RenderConstants::kMaxRenderObjectCount);
    }
    FrameResource::~FrameResource()
    {
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
            auto cb = Ref<ConstantBuffer>(ConstantBuffer::Create(RenderConstants::kPerCameraDataSize));
            _camera_cb_refs.emplace_back(cb);
            _camera_cbs.emplace_back(cb.get());
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
            auto cb = Ref<ConstantBuffer>(ConstantBuffer::Create(RenderConstants::kPerSceneDataSize));
            _scene_cb_refs.emplace_back(cb);
            _scene_cbs.emplace_back(cb.get());
            _scene_cb_lut[hash] = _scene_cbs.size() - 1;
            return _scene_cbs.back();
        }
        return _scene_cbs[_scene_cb_lut[hash]];
    }
    GPUBuffer *FrameResource::GetScenePrimitiveBuffer(u64 hash)
    {
        if (!_scene_primitive_buffer_lut.contains(hash))
        {
            BufferDesc desc;
            desc._element_num = RenderConstants::kMaxRenderObjectCount;
            desc._element_size = sizeof(PrimitiveData);
            desc._is_random_write = false;
            desc._size = desc._element_size * desc._element_num;
            desc._target = EGPUBufferTarget::kStructured;
            auto buf = GPUBuffer::Create(desc);
            buf->Name(std::format("ScenePrimitiveBuffer_{}", hash));
            _scene_primitive_buffers.push_back(buf);
            _scene_primitive_buffer_lut[hash] = _scene_primitive_buffers.size() - 1;

            return &*_scene_primitive_buffers.back();
        }
        return &*_scene_primitive_buffers[_scene_primitive_buffer_lut[hash]];
    }
    PrimitiveIndexAllocation FrameResource::AllocatePrimitiveIndices(u32 count)
    {
        AL_ASSERT(_primitive_indices.size() + count <= RenderConstants::kMaxRenderObjectCount);
        const u32 offset = static_cast<u32>(_primitive_indices.size());
        _primitive_indices.resize(offset + count);
        return PrimitiveIndexAllocation{_primitive_indices.data() + offset, offset, count};
    }
    void FrameResource::ResetPrimitiveIndices()
    {
        _primitive_indices.clear();
    }
    void FrameResource::UploadPrimitiveIndices()
    {
        if (!_primitive_indices.empty())
            _primitive_index_buffer->SetData(_primitive_indices);
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
        for (u32 i = 0; i < kFrameResourceSlotCount; ++i)
        {
            _frame_allocators[i] = MakeScope<FrameAllocator>();
            _frame_slot_fence_values[i] = 0u;
        }
    }
    FrameResourceManager::~FrameResourceManager()
    {
    }
    void FrameResourceManager::NewFrame()
    {
        auto &gfx = GraphicsContext::Get();
        if (_has_active_slot)
        {
            _frame_slot_fence_values[_active_slot] = gfx.GetFenceValueCPU();
            _prev_slot = _active_slot;
            _active_slot = (_active_slot + 1u) % kFrameResourceSlotCount;
        }
        else
        {
            _active_slot = 0u;
            _prev_slot = 0u;
            _has_active_slot = true;
        }

        const u64 slot_fence = _frame_slot_fence_values[_active_slot];
        if (slot_fence != 0u && gfx.GetFenceValueGPU() < slot_fence)
        {
            gfx.WaitForFence(slot_fence);
        }

        for (const auto &handle: _pending_texture_frees)
            _texture_pool.Release(handle);
        _pending_texture_frees.clear();
        for (const auto &handle: _pending_buffer_frees)
            _buffer_pool.Release(handle);
        _pending_buffer_frees.clear();

        const u64 cur_frame = gfx.GetFrameCount();
        _active_allocator = _frame_allocators[_active_slot].get();
        _active_allocator->NewFrame(cur_frame);
        // 帧上传缓冲区随帧槽位复用：此时该槽位的上一帧GPU工作已完成，可安全重置
        ResetFrameUpload(_active_slot);
    }
    FrameUploadAllocation FrameResourceManager::AllocFrameUpload(u32 size, u32 alignment)
    {
        if (_frame_upload_buffers[_active_slot] == nullptr)
            _frame_upload_buffers[_active_slot] = MakeScope<RHI::DX12::UploadBuffer>(std::format("FrameUploadBuffer_{}", _active_slot));
        auto *upload_buf = static_cast<RHI::DX12::UploadBuffer *>(_frame_upload_buffers[_active_slot].get());
        auto alloc = upload_buf->Allocate(size, alignment);
        FrameUploadAllocation out;
        out._buffer = upload_buf;
        out._cpu_ptr = alloc.CPU;
        out._gpu_handle = alloc.GPU;
        return out;
    }

    void FrameResourceManager::ResetFrameUpload(u32 frame_slot)
    {
        if (_frame_upload_buffers[frame_slot])
            static_cast<RHI::DX12::UploadBuffer *>(_frame_upload_buffers[frame_slot].get())->Reset();
    }

    void FrameResourceManager::FrameCleanup()
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
                        if (handle._res != nullptr && !handle._res->IsReferenceByGpu())
                        {
                            handle._is_available = true;
                            ++released_count;
                        }
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
                        if (handle._res != nullptr && !handle._res->IsReferenceByGpu())
                        {
                            handle._is_available = true;
                            ++released_count;
                        }
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
        if (handle._res != nullptr)
            _pending_texture_frees.emplace_back(handle);
    }
    void FrameResourceManager::FreeBuffer(BufferHandle handle)
    {
        if (handle._res != nullptr)
            _pending_buffer_frees.emplace_back(handle);
    }
    void FrameResourceManager::CleanupStaleResources()
    {
        _texture_pool.ReleaseUnused();
        _buffer_pool.ReleaseUnused();
    }
#pragma endregion

}// namespace Ailu::Render
