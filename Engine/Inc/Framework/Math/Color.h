#pragma once
#include "Framework/Math/Vector.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        using Color = Vector4D<float>;
        using Color32 = Vector4D<u8>;
        namespace Colors
        {
            static const Color kBlue = {0.f, 0.f, 1.f, 1.0f};
            static const Color kRed = {1.f, 0.f, 0.f, 1.0f};
            static const Color kGreen = {0.f, 1.f, 0.f, 1.0f};
            static const Color kWhite = {1.f, 1.f, 1.f, 1.0f};
            static const Color kBlack = {0.f, 0.f, 0.f, 1.0f};
            static const Color kGray = {0.3f, 0.3f, 0.3f, 1.0f};
            static const Color kYellow = {1.0f, 1.0f, 0.0f, 1.0f};
            static const Color kCyan = {0.f, 1.f, 1.f, 1.0f};
            static const Color kMagenta = {1.f, 0.f, 1.f, 1.0f};
            static const Color kOrange = {1.f, 0.5f, 0.f, 1.0f};
            static const Color kPurple = {0.5f, 0.f, 0.5f, 1.0f};
            static const Color kBrown = {0.6f, 0.3f, 0.0f, 1.0f};
            static const Color kPink = {1.f, 0.75f, 0.8f, 1.0f};
            static const Color kDarkBlue = {0.f, 0.f, 0.5f, 1.0f};
            static const Color kLightGray = {0.75f, 0.75f, 0.75f, 1.0f};
            static const Color kDarkGray = {0.2f, 0.2f, 0.2f, 1.0f};
            static const Color kLightGreen = {0.5f, 1.f, 0.5f, 1.0f};
            static const Color kDarkGreen = {0.f, 0.5f, 0.f, 1.0f};
            static const Color kBeige = {0.96f, 0.96f, 0.86f, 1.0f};
            static const Color kTurquoise = {0.25f, 0.88f, 0.82f, 1.0f};
            static const Color kTransparent = {0.0f, 0.0f, 0.0f, 0.0f};
        }// namespace Colors
    }
#pragma warning(pop)
}
