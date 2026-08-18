#include "Animation/AnimationController.h"
#include "Animation/AnimationEvent.h"
#include "Animation/Clip.h"
#include "Animation/SpriteAnimationTrack.h"

#include <cmath>
#include <utility>

namespace Ailu::AnimationTests
{
    namespace
    {
        Guid MakeClipId(const char *value)
        {
            return Guid(value);
        }

        AnimationState MakeState(const char *name, const char *clip_id)
        {
            AnimationState state;
            state._name = name;
            state._motion._asset = MakeClipId(clip_id);
            return state;
        }

        bool NearlyEqual(f32 lhs, f32 rhs)
        {
            return std::abs(lhs - rhs) < 0.0001f;
        }
    }

    bool TestControllerEntryAndClipEvaluation()
    {
        AnimationControllerAsset asset;
        const u16 idle_state = asset.AddState(MakeState("Idle", "00000000-0000-0000-0000-000000000001"));
        asset.EntryState(idle_state);

        AnimationInstance instance;
        instance.Initialize(asset);
        AnimationController controller(&asset);
        controller.Update(instance, 0.25f);
        const AnimationEvaluation evaluation = controller.Evaluate(instance);

        return evaluation._sample_count == 1u &&
               evaluation._samples[0]._clip == MakeClipId("00000000-0000-0000-0000-000000000001") &&
               NearlyEqual(evaluation._samples[0]._time, 0.25f) &&
               NearlyEqual(evaluation._samples[0]._weight, 1.0f);
    }

    bool TestControllerCrossFadeWeights()
    {
        AnimationControllerAsset asset;
        const u16 idle_state = asset.AddState(MakeState("Idle", "00000000-0000-0000-0000-000000000001"));
        const u16 run_state = asset.AddState(MakeState("Run", "00000000-0000-0000-0000-000000000002"));
        asset.EntryState(idle_state);

        AnimationTransition transition;
        transition._from_state = idle_state;
        transition._to_state = run_state;
        transition._duration = 0.2f;
        asset.AddTransition(std::move(transition));

        AnimationInstance instance;
        instance.Initialize(asset);
        AnimationController controller(&asset);
        controller.Update(instance, 0.1f);
        controller.Update(instance, 0.1f);
        const AnimationEvaluation evaluation = controller.Evaluate(instance);

        return evaluation._sample_count == 2u &&
               NearlyEqual(evaluation._samples[0]._weight, 0.5f) &&
               NearlyEqual(evaluation._samples[1]._weight, 0.5f) &&
               evaluation._samples[0]._clip == MakeClipId("00000000-0000-0000-0000-000000000001") &&
               evaluation._samples[1]._clip == MakeClipId("00000000-0000-0000-0000-000000000002");
    }

    bool TestControllerTriggerConsumption()
    {
        AnimationControllerAsset asset;
        const AnimationParameterId attack = asset.AddParameter("attack", EAnimationParameterType::kTrigger);
        const u16 idle_state = asset.AddState(MakeState("Idle", "00000000-0000-0000-0000-000000000001"));
        const u16 attack_state = asset.AddState(MakeState("Attack", "00000000-0000-0000-0000-000000000002"));
        asset.EntryState(idle_state);

        AnimationTransition transition;
        transition._from_state = kInvalidAnimationState;
        transition._to_state = attack_state;
        transition._duration = 0.0f;
        AnimationCondition condition;
        condition._parameter_index = attack;
        condition._op = EAnimationConditionOp::kTriggered;
        transition._conditions.push_back(condition);
        asset.AddTransition(std::move(transition));

        AnimationInstance instance;
        instance.Initialize(asset);
        instance.SetTrigger(attack);
        AnimationController controller(&asset);
        controller.Update(instance, 0.0f);

        return instance._current_state == attack_state && instance._triggers[attack] == 0u &&
               !instance._in_transition;
    }

    bool TestAnimationEventLoopWrap()
    {
        AnimationClip clip;
        clip.Duration(1.0f);
        clip.IsLooping(true);

        AnimationEvent event;
        event._time = 0.2f;
        event._event_id = 42u;
        clip.AddEvent(event);

        Vector<AnimationEvent> events;
        CollectAnimationEvents(clip, 0.8f, 1.3f, events);

        return events.size() == 1u && events[0]._event_id == 42u && NearlyEqual(events[0]._time, 0.2f);
    }

    bool TestSpriteAnimationTrackSampling()
    {
        SpriteAnimationTrack track;
        const Guid first = MakeClipId("00000000-0000-0000-0000-000000000011");
        const Guid second = MakeClipId("00000000-0000-0000-0000-000000000012");
        track.AddFrame({0.0f, first});
        track.AddFrame({0.5f, second});

        return track.Sample(0.1f, 1.0f, false) == first && track.Sample(0.6f, 1.0f, false) == second &&
               track.Sample(1.2f, 1.0f, true) == first;
    }
}
