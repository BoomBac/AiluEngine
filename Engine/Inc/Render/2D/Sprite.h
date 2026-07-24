#pragma once
#include "Framework/Math/ALMath.hpp"
#include "Objects/Object.h"
#include "generated/Sprite.gen.h"

namespace Ailu
{
    namespace Render
    {
        class Texture2D;
        ACLASS()
        class AILU_API Sprite : public Object
        {
            GENERATED_BODY()
        public:
            Sprite() = default;
            Sprite(const String& name) : Object(name) {};
        public:
            Ref<Render::Texture2D> _texture;
            Vector4f _uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
            Vector2f _pivot = {0.5f, 0.5f};
            Vector2f _size = Vector2f::kOne;
            //九宫格slice,RLBT，靠近边界的像素不会被拉伸
            Vector4f _border = Vector4f::kZero;
        };
    }// namespace Render
}// namespace Ailu
