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
        Vector<IConstantBuffer*>* GetObjCB();
        IConstantBuffer* GetObjCB(u32 index);
        IConstantBuffer* GetMatCB(u32 index);
        IConstantBuffer* GetCameraCB(u64 hash);
        IConstantBuffer* GetSceneCB(u64 hash);
    private:
        Vector<IConstantBuffer*> _obj_cbs;
        Vector<IConstantBuffer*> _mat_cbs;
        Vector<IConstantBuffer*> _camera_cbs;
        Vector<IConstantBuffer*> _scene_cbs;
        Map<u64,u64> _camera_cb_lut;
        Map<u64,u64> _scene_cb_lut;
    };

}// namespace Ailu

#endif//AILU_FRAMERESOURCE_H
