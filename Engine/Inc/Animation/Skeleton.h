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
		Vector<Matrix4x4f> _pose;
		Matrix4x4f _global_pose;
		u16 _frame_count = 0u;
	};

	struct Skeleton
	{
        static i32 GetJointIndexByName(const Skeleton& sk, const String& name)
        {
            auto it = std::find_if(sk._joints.begin(), sk._joints.end(),[&](const auto& joint) { return joint._name == name; });
            if (it != sk._joints.end())
				return static_cast<i32>(std::distance(sk._joints.begin(), it));
            else
				return -1;
        }

		u32 joint_num = 0u;
		Vector<Joint> _joints;
	};
}


#endif // !SKELETON__
