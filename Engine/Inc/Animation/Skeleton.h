#pragma once
#ifndef __SKELETON__
#define __SKELETON__
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Transform.h"

namespace Ailu
{
	struct Joint
	{
		Matrix4x4f _inv_bind_pos;
		String _name;
		u16 _parent;
		//Vector<Matrix4x4f> _pose;
		Vector<Matrix4x4f> _local_mat;
		u16 _frame_count = 0u;
		Vector<Transform> _global_transf;
		Vector<Transform> _local_transf;
		Matrix4x4f _cur_pose;
		Matrix4x4f _node_inv_world_mat;
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
