//
// Created by 22292 on 2024/10/12.
//
#include "Animation/Track.hpp"

namespace Ailu
{
//    template class Track<f32, 1>;
//    template class Track<Vector3f, 3>;
//    template class Track<Quaternion, 4>;
//    namespace TrackHelpers
//    {
//        inline void Neighborhood(const float &a, float &b) {}
//        inline void Neighborhood(const Vector3f &a, Vector3f &b) {}
//        inline void Neighborhood(const Quaternion &a, Quaternion &b)
//        {
//            if (Quaternion::Dot(a, b) < 0)
//                b = -b;
//        }
//        inline float AdjustHermiteResult(float f) {
//            return f;
//        }
//        inline Vector3f AdjustHermiteResult(const Vector3f& v) {
//            return v;
//        }
//        inline Quaternion AdjustHermiteResult(const Quaternion& q) {
//            return Quaternion::NormalizedQ(q);
//        }
//    };// End Track Helpers namespace
//    template<typename T,u32 N>
//    Track<T,N>::Track()
//    {
//        _interpolation_type = EInterpolationType::kLinear;
//    }
//    template<typename T, u32 N>
//    f32 Track<T, N>::GetEndTime()
//    {
//        return _frames.back()._time;
//    }
//    template<typename T, u32 N>
//    f32 Track<T, N>::GetStartTime()
//    {
//        return _frames.front()._time;
//    }
//    template<typename T, u32 N>
//    i32 Track<T, N>::FrameIndex(f32 time, bool is_looping)
//    {
//        if (is_looping)
//        {
//            f32 start_time = _frames[0]._time;
//            f32 end_time = _frames[_frames.size() - 1]._time;
//            f32 duration = end_time - start_time;
//            time = fmodf(time - start_time, end_time-start_time) ;
//            if (time < 0.0f)
//                time += end_time - start_time;
//            time += start_time;
//        }
//        else
//        {
//            if (time <= _frames[0]._time)
//                return 0;
//            if (time >= _frames[_frames.size() - 2]._time) //插值需要最后一帧的数据
//                return (u32)_frames.size() - 1;
//            for (i32 i = (int)_frames.size() - 1; i >= 0; --i)
//            {
//                if (time > _frames[i]._time)
//                    return i + 1;
//            }
//        }
//        return -1;
//    }
//    template<typename T, u32 N>
//    f32 Track<T, N>::AdjustTimeToFitTrack(f32 time, bool is_looping)
//    {
//        i32 size = (i32)_frames.size();
//        if (size <= 1)
//            return 0.f;
//        f32 start_time = _frames[0]._time;
//        f32 end_time = _frames[_frames.size() - 1]._time;
//        f32 duration = end_time - start_time;
//        if (duration <= 0.0f)
//            return 0.0f;
//        if (is_looping)
//        {
//            time = fmodf(time - start_time, end_time - start_time);
//            if (time < 0.0f)
//                time += end_time - start_time;
//            time += start_time;
//        }
//        else
//        {
//            time = std::clamp(time, start_time, end_time);
//        }
//        return time;
//    }
//    template<typename T, u32 N>
//    T Track<T, N>::Evaluate(f32 time, bool is_looping)
//    {
//        if (_interpolation_type == EInterpolationType::kConstant)
//            return EvaluateConstant(time, is_looping);
//        else if (_interpolation_type == EInterpolationType::kLinear)
//            return EvaluateLinear(time, is_looping);
//        return EvaluateCubic(time, is_looping);
//    }
//    template<typename T, u32 N>
//    T Track<T, N>::Hermite(float t, const T &p1, const T &s1, const T &p2, const T &s2)
//    {
//        float tt = t * t;
//        float ttt = tt * t;
//        T p2_ = p2;
//        TrackHelpers::Neighborhood(p1, p2_);
//        float h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
//        float h2 = -2.0f * ttt + 3.0f * tt;
//        float h3 = ttt - 2.0f * tt + t;
//        float h4 = ttt - tt;
//        T result = p1 * h1 + p2 * h2 + s1 * h3 + s2 * h4;
//        return TrackHelpers::AdjustHermiteResult(result);
//    }
    template<>
    f32 Track<f32,1>::Cast(f32 *data)
    {
        return data[0];
    }
    template<>
    VectorFrame Track<VectorFrame,3>::Cast(f32 *data)
    {
        VectorFrame v;
        v._value[0] = data[0];
        v._value[1] = data[1];
        v._value[2] = data[2];
        return VectorFrame{};//{data[0],data[1],data[2]};
    }
    template<>
    QuaternionFrame Track<QuaternionFrame ,4>::Cast(f32 *data)
    {
        auto q = Quaternion::NormalizedQ({data[0],data[1],data[2],data[3]});
        return QuaternionFrame{};//{};
    }
//    template<typename T, u32 N>
//    T Track<T, N>::EvaluateConstant(f32 time, bool is_looping)
//    {
//        i32 frame_index = FrameIndex(time, is_looping);
//        if (frame_index < 0 || frame_index >= (i32)_frames.size())
//            return T();
//        return Cast(&_frames[frame_index]._value[0]);
//    }
//    template<typename T, u32 N>
//    T Track<T, N>::EvaluateLinear(f32 time, bool is_looping)
//    {
//        i32 cur_frame = FrameIndex(time, is_looping);
//        if (cur_frame < 0 || cur_frame >= (i32)_frames.size() - 1)
//            return T();
//        i32 next_frame = cur_frame + 1;
//        f32 track_time = AdjustTimeToFitTrack(time, is_looping);
//        f32 cur_time = _frames[cur_frame]._time;
//        f32 frame_delta = _frames[next_frame]._time - cur_time;
//        if (frame_delta <= 0.0f)
//            return T();
//        f32 t = (track_time - cur_time) / frame_delta;
//        T start = Cast(&_frames[cur_frame]._value[0]);
//        T end = Cast(&_frames[next_frame]._value[0]);
//        return Lerp(start, end, t);
//    }
//    template<typename T, u32 N>
//    T Track<T, N>::EvaluateCubic(f32 time, bool is_looping)
//    {
//        i32 cur_frame = FrameIndex(time, is_looping);
//        if (cur_frame < 0 || cur_frame >= (i32)_frames.size() - 1)
//            return T();
//        i32 next_frame = cur_frame + 1;
//        f32 track_time = AdjustTimeToFitTrack(time, is_looping);
//        f32 cur_time = _frames[cur_frame]._time;
//        f32 frame_delta = _frames[next_frame]._time - cur_time;
//        if (frame_delta <= 0.0f)
//            return T();
//        f32 t = (track_time - cur_time) / frame_delta;
//        u64 flt_size = sizeof(f32);
//        T p1 = Cast(&_frames[cur_frame]._value[0]);
//        T slope1,slope2;
//        memcpy(&slope1,_frames[cur_frame]._out,N * flt_size);
//        slope1 = slope1 * frame_delta;
//        T p2 = Cast(&_frames[next_frame]._value[0]);
//        memcpy(&slope2, _frames[next_frame]._in, N * flt_size);
//        slope2 = slope2 * frame_delta;
//        return Hermite(t, p1, slope1, p2, slope2);
//    }
}