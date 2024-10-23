//
// Created by 22292 on 2024/10/12.
//

#ifndef AILU_TRANSFORMTRACK_H
#define AILU_TRANSFORMTRACK_H
#include "Framework/Math/Transform.h"
#include "Track.hpp"
namespace Ailu
{
    class AILU_API TransformTrack
    {
    public:
        TransformTrack();
        [[nodiscard]] u16 GetId() const;
        void SetId(u16 id);
        VectorTrack& GetPositionTrack();
        QuaternionTrack& GetRotationTrack();
        VectorTrack& GetScaleTrack();
        f32 GetStartTime();
        f32 GetEndTime();
        bool IsValid();
        Transform Evaluate(const Transform& ref, f32 time, bool looping);
        void Resize(u16 frame_num);
    private:
        u16 _id;
        VectorTrack _pos_track;
        QuaternionTrack _rot_track;
        VectorTrack _scale_track;
    };
}
#endif//AILU_TRANSFORMTRACK_H
