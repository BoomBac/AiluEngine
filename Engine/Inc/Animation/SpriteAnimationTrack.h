#pragma once
#ifndef __SPRITE_ANIMATION_TRACK_H__
#define __SPRITE_ANIMATION_TRACK_H__

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
    struct SpriteKeyFrame
    {
        f32 _time = 0.0f;
        Guid _sprite = Guid::EmptyGuid();
    };

    class AILU_API SpriteAnimationTrack
    {
    public:
        const Vector<SpriteKeyFrame> &Frames() const { return _frames; }
        Vector<SpriteKeyFrame> &Frames() { return _frames; }
        bool Empty() const { return _frames.empty(); }

        void AddFrame(SpriteKeyFrame frame);
        Guid Sample(f32 time, f32 duration, bool looping) const;

    private:
        Vector<SpriteKeyFrame> _frames;
    };
}

#endif // __SPRITE_ANIMATION_TRACK_H__
