#pragma once
#ifndef __BLEND_SPACE_H__
#define __BLEND_SPACE_H__

#include "Animation/AnimationEvaluation.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Guid.h"
#include "Objects/Object.h"
#include "generated/BlendSpace.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API BlendSpaceSample
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _clip = Guid::EmptyGuid();
        APROPERTY()
        Vector2f _position = Vector2f::kZero;
    };

    ACLASS()
    class AILU_API BlendSpaceAsset : public Object
    {
        GENERATED_BODY()

    public:
        BlendSpaceAsset();
        explicit BlendSpaceAsset(const String &name);

        const Vector<BlendSpaceSample> &Samples() const { return _samples; }
        Vector<BlendSpaceSample> &Samples() { return _samples; }
        const Vector2f &XRange() const { return _x_range; }
        void XRange(const Vector2f &range) { _x_range = range; }
        const Vector2f &YRange() const { return _y_range; }
        void YRange(const Vector2f &range) { _y_range = range; }
        bool Is2D() const { return _is_2d; }
        void Is2D(bool value) { _is_2d = value; }

        void AddSample(BlendSpaceSample sample);
        void AddSamples(f32 position, f32 time, f32 weight, bool loop, AnimationEvaluation &evaluation,
                        bool normalized_time = false) const;
        void AddSamples(Vector2f position, f32 time, f32 weight, bool loop, AnimationEvaluation &evaluation,
                        bool normalized_time = false) const;

    private:
        APROPERTY()
        Vector<BlendSpaceSample> _samples;
        APROPERTY()
        Vector2f _x_range = Vector2f(0.0f, 1.0f);
        APROPERTY()
        Vector2f _y_range = Vector2f(0.0f, 1.0f);
        APROPERTY()
        bool _is_2d = false;
    };
}

#endif // __BLEND_SPACE_H__
