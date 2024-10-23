//
// Created by 22292 on 2024/10/12.
//
#pragma once
#ifndef AILU_TRACK_HPP
#define AILU_TRACK_HPP
#include "Curve.hpp"
#include "GlobalMarco.h"
namespace Ailu
{
    /*
     * 存储曲线一帧的数据，使用组合来实现不同类型的帧：标量/位置等
    */
    template<u32 N>
    class Frame
    {
    public:
        f32 _value[N];
        f32 _in[N];
        f32 _out[N];
        f32 _time;
        template<class T>
        Frame<N> operator*(T scale) const {
            auto ret = *this;
            for(u32 i = 0; i < N; ++i)
            {
                ret._value[i] = _value[i] * scale;
            }
            return ret;
        };
//        template<class T>
//        Frame<N> operator+(T scale) const {
//            f32 value[N];
//            for(u32 i = 0; i < N; ++i)
//                value[i] = _value[i] + scale;
//        };
//        template<class T>
        Frame<N> operator+(Frame<N> v) const {
            auto ret = *this;
            for(u32 i = 0; i < N; ++i)
                ret._value[i] = _value[i] + v._value[i];
            return ret;
        };
    };
    using ScalarFrame = Frame<1>;
    using VectorFrame = Frame<3>;
    using QuaternionFrame = Frame<4>;

    template<typename T, u32 N>
    class Track
    {
    public:
        Track();
        void Resize(u32 new_size) {_frames.resize(new_size);};
        [[nodiscard]] u32 Size() const {return _frames.size();};
        [[nodiscard]] EInterpolationType::EInterpolationType InterpolationType() const {return _interpolation_type;};
        void SetInterpolation(EInterpolationType::EInterpolationType interp) { _interpolation_type = interp;};
        f32 GetStartTime();
        f32 GetEndTime();
        T Evaluate(f32 time, bool is_looping);
        Frame<N> &operator[](u32 index) {return _frames[index];};
        const Frame<N> &operator[](u32 index) const {return _frames[index];};
        i32 FrameIndex(f32 time, bool is_looping);
        //将一个任意时间转化为轨道内的时间,一般当动画的播放时间改变时调用
        f32 AdjustTimeToFitTrack(f32 time, bool is_looping);
        //用于将frame中的数据转化为轨道实际使用的类型
        T Cast(f32* data);
    protected:
        T EvaluateConstant(f32 time, bool is_looping);
        T EvaluateLinear(f32 time, bool is_looping);
        T EvaluateCubic(f32 time, bool is_looping);
        T Hermite(float t, const T& p1, const T& s1,const T& p2, const T& s2);
    protected:
        Vector<Frame<N>> _frames;
        EInterpolationType::EInterpolationType _interpolation_type;
    };

    using ScalarTrack = Track<ScalarFrame, 1>;
    using VectorTrack = Track<VectorFrame, 3>;
    using QuaternionTrack = Track<QuaternionFrame, 4>;

    namespace TrackHelpers
    {
        static f32 ToScale(const ScalarFrame &t)
        {
            return t._value[0];
        };
        static Vector3f ToVector(const VectorFrame &t)
        {
            return Vector3f{t._value[0], t._value[1], t._value[2]};
        }
        static Quaternion ToQuaternion(const QuaternionFrame &t)
        {
            return Quaternion{t._value[0], t._value[1], t._value[2], t._value[3]};
        }
        static ScalarFrame FromScale(f32 value)
        {
            ScalarFrame ret;
            memset(&ret,0,sizeof(ScalarFrame));
            ret._value[0] = value;
            return ret;
        }
        static VectorFrame FromVector(const Vector3f &v)
        {
            VectorFrame ret;
            memset(&ret,0,sizeof(VectorFrame));
            ret._value[0] = v.x;
            ret._value[1] = v.y;
            ret._value[2] = v.z;
            return ret;
        }
        static QuaternionFrame FromQuaternion(const Quaternion &q)
        {
            QuaternionFrame ret;
            memset(&ret,0,sizeof(QuaternionFrame));
            ret._value[0] = q.x;
            ret._value[1] = q.y;
            ret._value[2] = q.z;
            ret._value[3] = q.w;
            return ret;
        }

        inline void Neighborhood(const float &a, float &b) {}
        inline void Neighborhood(const Vector3f &a, Vector3f &b) {}
        inline void Neighborhood(const Quaternion &a, Quaternion &b)
        {
            if (Quaternion::Dot(a, b) < 0)
                b = -b;
        }
        inline void Neighborhood(const VectorFrame& a,VectorFrame& b){}
        inline void Neighborhood(const QuaternionFrame& a,QuaternionFrame& b)
        {
            Quaternion qa,qb;
            memcpy(qa._quat.data,a._value,sizeof(Quaternion));
            memcpy(qb._quat.data,b._value,sizeof(Quaternion));
            Neighborhood(qa,qb);
            memcpy(b._value,qb._quat.data,sizeof(Quaternion));
        }
        inline float AdjustHermiteResult(float f) {
            return f;
        }
        inline Vector3f AdjustHermiteResult(const Vector3f& v) {
            return v;
        }
        inline VectorFrame AdjustHermiteResult(const VectorFrame& v) {
            return v;
        }
        inline Quaternion AdjustHermiteResult(const Quaternion& q) {
            return Quaternion::NormalizedQ(q);
        }
        inline QuaternionFrame AdjustHermiteResult(const QuaternionFrame& q) {
            Quaternion qa;
            memcpy(qa._quat.data,q._value,sizeof(Quaternion));
            qa = AdjustHermiteResult(qa);
            QuaternionFrame ret = q;
            memcpy(ret._value,qa._quat.data,sizeof(Quaternion));
            return ret;
        }
        inline f32 Lerp(const f32 & a,const f32& b,f32 t)
        {
            return Math::Lerp(a,b,t);
        }
        inline Vector3f Lerp(const Vector3f & a,const Vector3f& b,f32 t)
        {
            return Math::Lerp(a,b,t);
        }
        inline Quaternion Lerp(const Quaternion & a,const Quaternion& b,f32 t)
        {
            return Math::Lerp(a,b,t);
        }
        inline QuaternionFrame Lerp(const QuaternionFrame & a,const QuaternionFrame& b,f32 t)
        {
            Quaternion qa,qb;
            memcpy(qa._quat.data,a._value,sizeof(Quaternion));
            memcpy(qb._quat.data,b._value,sizeof(Quaternion));
            Quaternion qc = Lerp(qa,qb,t);
            QuaternionFrame ret;
            memcpy(ret._value,qc._quat.data,sizeof(Quaternion));
            return ret;
        }
        inline VectorFrame Lerp(const VectorFrame & a,const VectorFrame& b,f32 t)
        {
            Vector3f qa,qb;
            memcpy(qa.data,a._value,sizeof(Vector3f));
            memcpy(qb.data,b._value,sizeof(Vector3f));
            Vector3f qc = Lerp(qa,qb,t);
            VectorFrame ret;
            memcpy(ret._value,qc.data,sizeof(Vector3f));
            return ret;
        }
    };// End Track Helpers namespace
    template<typename T,u32 N>
    Track<T,N>::Track()
    {
        _interpolation_type = EInterpolationType::kLinear;
    }
    template<typename T, u32 N>
    f32 Track<T, N>::GetEndTime()
    {
        return _frames.back()._time;
    }
    template<typename T, u32 N>
    f32 Track<T, N>::GetStartTime()
    {
        return _frames.front()._time;
    }
    template<typename T, u32 N>
    i32 Track<T, N>::FrameIndex(f32 time, bool is_looping)
    {
        if (is_looping)
        {
            f32 start_time = _frames[0]._time;
            f32 end_time = _frames[_frames.size() - 1]._time;
            f32 duration = end_time - start_time;
            time = fmodf(time - start_time, end_time-start_time) ;
            if (time < 0.0f)
                time += end_time - start_time;
            time += start_time;
        }
        else
        {
            if (time <= _frames[0]._time)
                return 0;
            if (time >= _frames[_frames.size() - 2]._time) //插值需要最后一帧的数据
                return (u32)_frames.size() - 1;
        }
        for (i32 i = (int)_frames.size() - 1; i >= 0; --i)
        {
            if (time > _frames[i]._time)
                //return i + 1;
                return i;
        }
        return 0;
    }
    template<typename T, u32 N>
    f32 Track<T, N>::AdjustTimeToFitTrack(f32 time, bool is_looping)
    {
        i32 size = (i32)_frames.size();
        if (size <= 1)
            return 0.f;
        f32 start_time = _frames[0]._time;
        f32 end_time = _frames[_frames.size() - 1]._time;
        f32 duration = end_time - start_time;
        if (duration <= 0.0f)
            return 0.0f;
        if (is_looping)
        {
            time = fmodf(time - start_time, end_time - start_time);
            if (time < 0.0f)
                time += end_time - start_time;
            time += start_time;
        }
        else
        {
            time = std::clamp(time, start_time, end_time);
        }
        return time;
    }
    template<typename T, u32 N>
    T Track<T, N>::Evaluate(f32 time, bool is_looping)
    {
        if (_interpolation_type == EInterpolationType::kConstant)
            return EvaluateConstant(time, is_looping);
        else if (_interpolation_type == EInterpolationType::kLinear)
            return EvaluateLinear(time, is_looping);
        return EvaluateCubic(time, is_looping);
    }
    template<typename T, u32 N>
    T Track<T, N>::Hermite(float t, const T &p1, const T &s1, const T &p2, const T &s2)
    {
        float tt = t * t;
        float ttt = tt * t;
        T p2_ = p2;
        TrackHelpers::Neighborhood(p1, p2_);
        float h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
        float h2 = -2.0f * ttt + 3.0f * tt;
        float h3 = ttt - 2.0f * tt + t;
        float h4 = ttt - tt;
        T result = p1 * h1 + p2 * h2 + s1 * h3 + s2 * h4;
        return TrackHelpers::AdjustHermiteResult(result);
    }
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
    template<typename T, u32 N>
    T Track<T, N>::EvaluateConstant(f32 time, bool is_looping)
    {
        i32 frame_index = FrameIndex(time, is_looping);
        if (frame_index < 0 || frame_index >= (i32)_frames.size())
            return T();
        return Cast(&_frames[frame_index]._value[0]);
    }
    template<typename T, u32 N>
    T Track<T, N>::EvaluateLinear(f32 time, bool is_looping)
    {
        i32 cur_frame = FrameIndex(time, is_looping);
        if (cur_frame < 0 || cur_frame >= (i32)_frames.size() - 1)
            return T();
        i32 next_frame = cur_frame + 1;
        f32 track_time = AdjustTimeToFitTrack(time, is_looping);
        f32 cur_time = _frames[cur_frame]._time;
        f32 frame_delta = _frames[next_frame]._time - cur_time;
        if (frame_delta <= 0.0f)
            return T();
        f32 t = (track_time - cur_time) / frame_delta;
        T start = _frames[cur_frame];//Cast(&_frames[cur_frame]._value[0]);
        T end = _frames[next_frame];//Cast(&_frames[next_frame]._value[0]);
        return TrackHelpers::Lerp(start, end, t);
    }
    template<typename T, u32 N>
    T Track<T, N>::EvaluateCubic(f32 time, bool is_looping)
    {
        i32 cur_frame = FrameIndex(time, is_looping);
        if (cur_frame < 0 || cur_frame >= (i32)_frames.size() - 1)
            return T();
        i32 next_frame = cur_frame + 1;
        f32 track_time = AdjustTimeToFitTrack(time, is_looping);
        f32 cur_time = _frames[cur_frame]._time;
        f32 frame_delta = _frames[next_frame]._time - cur_time;
        if (frame_delta <= 0.0f)
            return T();
        f32 t = (track_time - cur_time) / frame_delta;
        u64 flt_size = sizeof(f32);
        T p1 = _frames[cur_frame];//Cast(&_frames[cur_frame]._value[0]);
        T slope1,slope2;
        memcpy(&slope1,_frames[cur_frame]._out,N * flt_size);
        slope1 = slope1 * frame_delta;
        T p2 = _frames[next_frame];//Cast(&_frames[next_frame]._value[0]);
        memcpy(&slope2, _frames[next_frame]._in, N * flt_size);
        slope2 = slope2 * frame_delta;
        return Hermite(t, p1, slope1, p2, slope2);
    }
}// namespace Ailu
#endif//AILU_TRACK_HPP
