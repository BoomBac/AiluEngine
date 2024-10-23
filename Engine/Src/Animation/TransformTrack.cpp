//
// Created by 22292 on 2024/10/12.
//
#include "Animation/TransformTrack.h"
#include "pch.h"

namespace Ailu
{
//    template<>
//    f32 Track<f32,1>::Cast(f32 *data)
//    {
//        return data[0];
//    }
//    template<>
//    Vector3f Track<Vector3f,3>::Cast(f32 *data)
//    {
//        return {data[0],data[1],data[2]};
//    }
//    template<>
//    Quaternion Track<Quaternion,4>::Cast(f32 *data)
//    {
//        return Quaternion::NormalizedQ({data[0],data[1],data[2],data[3]});
//    }

    TransformTrack::TransformTrack() : _id(0)
    {
    }
    u16 TransformTrack::GetId() const
    {
        return _id;
    }
    void TransformTrack::SetId(u16 id)
    {
        _id = id;
    }
    VectorTrack &TransformTrack::GetPositionTrack()
    {
        return _pos_track;
    }
    QuaternionTrack &TransformTrack::GetRotationTrack()
    {
        return _rot_track;
    }
    VectorTrack &TransformTrack::GetScaleTrack()
    {
        return _scale_track;
    }
    bool TransformTrack::IsValid()
    {
        return _pos_track.Size() > 1 ||
               _rot_track.Size() > 1 ||
               _scale_track.Size() > 1;
    }
    f32 TransformTrack::GetStartTime()
    {
        f32 result = 0.0f;
        bool isSet = false;
        if (_pos_track.Size() > 1) {
            result = _pos_track.GetStartTime();
            isSet = true;
        }
        if (_rot_track.Size() > 1) {
            f32 rotationStart = _rot_track.GetStartTime();
            if (rotationStart < result || !isSet) {
                result = rotationStart;
                isSet = true;
            }
        }
        if (_scale_track.Size() > 1) {
            f32 scaleStart = _scale_track.GetStartTime();
            if (scaleStart < result || !isSet) {
                result = scaleStart;
                isSet = true;
            }
        }
        return result;
    }
    f32 TransformTrack::GetEndTime()
    {
        f32 result = 0.0f;
        bool isSet = false;
        if (_pos_track.Size() > 1) {
            result = _pos_track.GetEndTime();
            isSet = true;
        }
        if (_rot_track.Size() > 1) {
            f32 rotationEnd = _rot_track.GetEndTime();
            if (rotationEnd > result || !isSet) {
                result = rotationEnd;
                isSet = true;
            }
        }
        if (_scale_track.Size() > 1) {
            f32 scaleEnd = _scale_track.GetEndTime();
            if (scaleEnd > result || !isSet) {
                result = scaleEnd;
                isSet = true;
            }
        }
        return result;
    }
    Transform TransformTrack::Evaluate(const Transform &ref, f32 time, bool looping)
    {
        Transform result = ref; // Assign default values
        if (_pos_track.Size() > 1) { // Only if valid
            auto res = _pos_track.Evaluate(time, looping);
            memcpy(result._position.data, &res, sizeof(Vector3f));
        }
        if (_rot_track.Size() > 1) { // Only if valid
            auto res = _rot_track.Evaluate(time, looping);
            memcpy(result._rotation._quat.data, &res, sizeof(Quaternion));
        }
        if (_scale_track.Size() > 1) { // Only if valid
            auto res = _scale_track.Evaluate(time, looping);
            memcpy(result._scale.data, &res, sizeof(Vector3f));
        }
        return result;
    }
    void TransformTrack::Resize(u16 frame_num)
    {
        _pos_track.Resize(frame_num);
        _rot_track.Resize(frame_num);
        _scale_track.Resize(frame_num);
    }
}// namespace Ailu
