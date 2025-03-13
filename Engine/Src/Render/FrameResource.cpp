//
// Created by 22292 on 2024/11/7.
//

#include "Inc/Render/FrameResource.h"

namespace Ailu
{
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
}// namespace Ailu
