#include "Animation/SpriteAnimationTrack.h"

#include <algorithm>
#include <cmath>

namespace Ailu
{
    void SpriteAnimationTrack::AddFrame(SpriteKeyFrame frame)
    {
        const auto it = std::lower_bound(_frames.begin(), _frames.end(), frame._time,
                                         [](const SpriteKeyFrame &item, f32 time) { return item._time < time; });
        _frames.insert(it, std::move(frame));
    }

    Guid SpriteAnimationTrack::Sample(f32 time, f32 duration, bool looping) const
    {
        if (_frames.empty())
            return Guid::EmptyGuid();

        if (looping && duration > 0.0f)
        {
            time = std::fmod(time, duration);
            if (time < 0.0f)
                time += duration;
        }
        else
        {
            time = std::clamp(time, _frames.front()._time, _frames.back()._time);
        }

        u32 low = 0u;
        u32 high = static_cast<u32>(_frames.size());
        while (low < high)
        {
            const u32 middle = low + (high - low) / 2u;
            if (_frames[middle]._time <= time)
                low = middle + 1u;
            else
                high = middle;
        }
        return _frames[low == 0u ? 0u : low - 1u]._sprite;
    }
}
