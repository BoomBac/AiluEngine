//
// Created by 22292 on 2024/11/7.
//

#ifndef AILU_FRAMERESOURCE_H
#define AILU_FRAMERESOURCE_H
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Buffer.h"

namespace Ailu
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
    private:
        Vector<ConstantBuffer *> _obj_cbs;
        Vector<ConstantBuffer *> _mat_cbs;
        Vector<ConstantBuffer *> _camera_cbs;
        Vector<ConstantBuffer *> _scene_cbs;
        Map<u64,u64> _camera_cb_lut;
        Map<u64,u64> _scene_cb_lut;
    };

}// namespace Ailu

#endif//AILU_FRAMERESOURCE_H
