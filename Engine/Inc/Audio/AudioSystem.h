#pragma once
#ifndef __AUDIO_SYSTEM_H__
#define __AUDIO_SYSTEM_H__

#include "Audio/Audio.h"
#include "Scene/Component.h"

namespace Ailu::ECS
{
    class AILU_API AudioSystem final : public System
    {
        DECLARE_SYSTEM(AudioSystem)

    public:
        void Update(Register &r, f32 delta_time) override;
        ESystemPhase GetPhase() const override { return ESystemPhase::kRenderData; }
        i32 GetOrder() const override { return -100; }
        Ref<System> Clone() override { return MakeRef<AudioSystem>(*this); }
    };
}// namespace Ailu::ECS

#endif// __AUDIO_SYSTEM_H__
