#pragma once
#ifndef __SKELETON__
#define __SKELETON__
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	struct Joint
	{
		Matrix4x4f _inv_bind_pos;
		String _name;
		u16 _parent;
		u16 _self;
	};

	struct Skeleton
	{
		u32 joint_num;
		Vector<Joint> _joints;
	};
}


#endif // !SKELETON__
