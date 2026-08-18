#include "Animation/AnimationEvent.h"

#include "Animation/Clip.h"

#include <algorithm>
#include <cmath>

namespace Ailu
{
    void CollectAnimationEvents(const AnimationClip &clip, f32 previous_time, f32 current_time,
                                Vector<AnimationEvent> &out_events)
    {
        const auto &events = clip.Events();
        const f32 duration = clip.Duration();
        if (events.empty() || duration <= 0.0f || previous_time == current_time)
            return;

        const auto collect_range = [&](f32 begin, f32 end, bool include_begin)
        {
            for (const auto &event : events)
            {
                const bool after_begin = include_begin ? event._time >= begin : event._time > begin;
                if (after_begin && event._time <= end)
                    out_events.push_back(event);
            }
        };

        if (!clip.IsLooping())
        {
            if (current_time > previous_time)
                collect_range(previous_time, current_time, false);
            return;
        }

        const auto normalize_time = [duration](f32 time)
        {
            time = std::fmod(time, duration);
            return time < 0.0f ? time + duration : time;
        };
        const f32 previous = normalize_time(previous_time);
        const f32 current = normalize_time(current_time);
        if (current_time - previous_time >= duration)
        {
            collect_range(0.0f, duration, true);
        }
        else if (current >= previous)
        {
            collect_range(previous, current, false);
        }
        else
        {
            collect_range(previous, duration, false);
            collect_range(0.0f, current, true);
        }
    }
}
