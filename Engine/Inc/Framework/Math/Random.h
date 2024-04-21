#pragma once
#ifndef __RANDOM__
#define __RANDOM__
#include <random>
#include "ALMath.hpp"

namespace Ailu
{
	namespace Random
	{
		static Color32 RandomColor(u32 seed)
		{
			std::mt19937 rng(seed);
			std::uniform_real_distribution<float> dist(0.0f, 1.0f);
			float random_r = dist(rng);
			float random_g = dist(rng);
			float random_b = dist(rng);
			return Color32(random_r, random_g, random_b, 1.0f);
		}
	}
}

#endif // !RANDOM__
