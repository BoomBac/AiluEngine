#pragma once
#ifndef __SKELETON__
#define __SKELETON__
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Transform.h"

namespace Ailu
{
	struct Joint
	{
		String _name;
		u16 _parent;
		u16 _self;
		Vector<u16> _children;
		Matrix4x4f _inv_bind_pos;
		Matrix4x4f _node_inv_world_mat;
		Vector<Transform> _local_transf;
		Matrix4x4f _cur_pose;
	};

	class Skeleton
	{
		DECLARE_PRIVATE_PROPERTY(name,Name,String)
	public:
        static i32 GetJointIndexByName(const Skeleton& sk, const String& name)
        {
            auto it = std::find_if(sk._joints.begin(), sk._joints.end(),[&](const auto& joint) { return joint._name == name; });
            if (it != sk._joints.end())
				return static_cast<i32>(std::distance(sk._joints.begin(), it));
            else
				return -1;
        }
		Skeleton(const String& name) : _name(name) {};
		Skeleton() : Skeleton("Skeleton") {};
		const u32 JointNum() const{ return static_cast<u32>(_joints.size());};
		void AddJoint(const Joint& joint) { _joints.push_back(joint); };
		Joint& GetJoint(u32 index) { return _joints[index]; };
		Joint& GetJoint(const String& name)
		{
			for (auto& joint : _joints)
			{
				if (joint._name == name)
					return joint;
			}
			return _joints[0];
		}
		void Clear() 
		{
			_joints.clear();
		};
		const bool operator==(const Skeleton& other) const
		{
			if (_joints.size() != other._joints.size())
				return false;
			for (size_t i = 0; i < _joints.size(); ++i) 
			{
				const Joint& joint1 = _joints[i];
				const Joint& joint2 = other._joints[i];

				if (joint1._name != joint2._name ||
					joint1._parent != joint2._parent/* ||
					joint1._self != joint2._self ||
					joint1._inv_bind_pos != joint2._inv_bind_pos ||
					joint1._node_inv_world_mat != joint2._node_inv_world_mat ||
					joint1._local_transf != joint2._local_transf ||
					joint1._cur_pose != joint2._cur_pose*/) 
					{
					return false;
				}
			}
			return true;
		}
		Joint& operator[](u32 index) { return _joints[index]; };
		const Joint& operator[](u32 index) const { return _joints[index]; };
		auto begin() { return _joints.begin(); }
		auto end() { return _joints.end(); }
		auto begin() const { return _joints.cbegin(); }
		auto end() const { return _joints.cend(); }
	private:
		Vector<Joint> _joints;
	};
}


#endif // !SKELETON__
