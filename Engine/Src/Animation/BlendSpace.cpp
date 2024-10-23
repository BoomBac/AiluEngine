#include "Animation/BlendSpace.h"
#include "pch.h"
#include "Framework/Common/Log.h"
namespace Ailu
{
    BlendSpace::BlendSpace() 
    {
    }
    BlendSpace::BlendSpace(Skeleton *sk, Vector2f x_range, Vector2f y_range, bool is_2d) : _x_range(x_range), _y_range(y_range), _x(x_range.x), _y(y_range.x), _is_2d(is_2d), _time(0.f)
    {
        SetSkeleton(sk);
    }
    BlendSpace::~BlendSpace()
    {
        //LOG_INFO("BlendSpace::~BlendSpace")
    }
    void BlendSpace::SetSkeleton(Skeleton *sk)
    {
        if (sk)
        {
            _skeleton = sk;
            for (u16 i = 0; i < _poses.size(); ++i)
                _poses[i] = _skeleton->GetBindPose();
        }
    }
    Skeleton *BlendSpace::GetSkeleton() const
    {
        return _skeleton;
    }
    void BlendSpace::AddClip(AnimationClip *clip, f32 x, f32 y)
    {
        for (auto &c: _clips)
        {
            if (c._clip == clip)
            {
                c._x = x;
                c._y = y;
                return;
            }
        }
        clip->IsLooping(true);
        NormalizeRange(x, y);
        _clips.emplace_back(x, y, clip);
    }
    void BlendSpace::RemoveClip(AnimationClip *clip)
    {
        for (auto it = _clips.begin(); it != _clips.end(); ++it)
        {
            if (it->_clip == clip)
            {
                _clips.erase(it);
                return;
            }
        }
    }
    void BlendSpace::Resize(Vector2f x_range, Vector2f y_range)
    {
        _x_range = x_range;
        _y_range = y_range;
        for (auto &c: _clips)
        {
            NormalizeRange(c._x, c._y);
        }
    }
    void BlendSpace::GetRange(Vector2f &x_range, Vector2f &y_range) const
    {
        x_range = _x_range;
        y_range = _y_range;
    }
    void BlendSpace::SetPosition(f32 x, f32 y)
    {
        NormalizeRange(x, y);
        _x = x;
        _y = y;
    }
    void BlendSpace::GetPosition(f32 &x, f32 &y) const
    {
        x = _x;
        y = _y;
    }
    void BlendSpace::Update(f32 dt)
    {
        if (_clips.empty())
            return;
        _time += dt;
        if (!_is_2d)
        {
            if (_clips.size() == 1)
            {
                _clips[0]._clip->Sample(_poses[0], _time);
            }
            else
            {
                u32 left = 0u, right = 0u;
                f32 dx_left = 10000.0f, dx_right = 10000.0f;
                for (u32 i = 0; i < _clips.size(); ++i)
                {
                    auto &cur_clip = _clips[i];
                    if (cur_clip._x <= _x && _x - cur_clip._x < dx_left)
                    {
                        left = i;
                        dx_left = _x - cur_clip._x;
                    }
                    if (cur_clip._x >= _x && cur_clip._x - _x < dx_right)
                    {
                        right = i;
                        dx_right = cur_clip._x - _x;
                    }
                }
                f32 t;
                if (dx_left + dx_right == 0)
                    t = 1.0f;
                else
                    t = (_x - _clips[left]._x) / (dx_left + dx_right);
                t = std::max<f32>(t, 0.f);
                f32 normalize_time_a = _clips[left]._clip->GetNormalizedTime(_time);
                f32 normalize_time_b = _clips[right]._clip->GetNormalizedTime(_time);
                f32 mix_time = Lerp(normalize_time_a,normalize_time_b,t);
                _clips[left]._clip->Sample(_poses[1], mix_time * _clips[left]._clip->Duration());
                _clips[right]._clip->Sample(_poses[2], mix_time * _clips[right]._clip->Duration());
                Pose::Blend(_poses[0], _poses[1], _poses[2], t, -1);
            }
        }
        else
        {
            AL_ASSERT(true);
        }
    }
    Pose &BlendSpace::GetCurrentPose()
    {
        return _poses[0];
    }
    void BlendSpace::SetClipPosition(AnimationClip *clip, f32 x, f32 y)
    {
        NormalizeRange(x, y);
        for (auto &c: _clips)
        {
            if (c._clip == clip)
            {
                c._x = x;
                c._y = y;
                return;
            }
        }
    }
    void BlendSpace::NormalizeRange(f32 &x, f32 &y) const
    {
        x = std::clamp(x, _x_range.x, _x_range.y);
        y = std::clamp(y, _y_range.x, _y_range.y);
    }
}// namespace Ailu
