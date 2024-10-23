#include "pch.h"
#include "Animation/Pose.h"
#include "Animation/Skeleton.h"

namespace Ailu
{
	Pose::Pose() { }

	Pose::Pose(unsigned int numJoints)
	{
		Resize(numJoints);
	}

	Pose::Pose(const Pose& p)
	{
		*this = p;
	}

	Pose& Pose::operator=(const Pose& p)
	{
		if (&p == this) {
			return *this;
		}
		if (_parents.size() != p._parents.size()) {
			_parents.resize(p._parents.size());
		}
		if (_joints.size() != p._joints.size()) {
			_joints.resize(p._joints.size());
		}
		_parents = p._parents;
		_joints = p._joints;
		return *this;
	}

	void Pose::Resize(unsigned int size) {
		_parents.resize(size);
		_joints.resize(size);
	}
	unsigned int Pose::Size() {
		return static_cast<int>(_joints.size());
	}

	Transform Pose::GetLocalTransform(unsigned int index) const
    {
		return _joints[index];
	}
	void Pose::SetLocalTransform(unsigned int index, const Transform& transform) {
		_joints[index] = transform;
	}

	Transform Pose::GetGlobalTransform(unsigned int i) const
    {
		Transform result = _joints[i];
		for (u16 p = _parents[i]; p != Joint::kInvalidJointIndex; p = _parents[p])
		{
			result = Transform::Combine(_joints[p], result);
		}
		return result;
	}
	Transform Pose::operator[](unsigned int index) {
		return GetGlobalTransform(index);
	}

	void Pose::GetMatrixPalette(Vector<Matrix4x4f> &out)
	{
		unsigned int size = Size();
		if (out.size() != size) {
			out.resize(size);
		}
		for (unsigned int i = 0; i < size; ++i) {
			Transform t = GetGlobalTransform(i);
            out[i] = Transform::ToMatrix(t);
		}
	}
	int Pose::GetParent(unsigned int index)
	{
		return _parents[index];
	}
	void Pose::SetParent(unsigned int index, int parent)
	{
		_parents[index] = parent;
	}
	bool Pose::operator==(const Pose& other)
	{
		if (_joints.size() != other._joints.size())
		{
			return false;
		}
		if (_parents.size() != other._parents.size()) {
			return false;
		}
		unsigned int size = (unsigned int)_joints.size();
		for (unsigned int i = 0; i < size; ++i)
		{
			Transform thisLocal = _joints[i];
			Transform otherLocal = other._joints[i];
			int thisParent = _parents[i];
			int otherParent = other._parents[i];
			if (thisParent != otherParent) { return false; }
			if (thisLocal._position != otherLocal._position)
				return false;
			if (thisLocal._rotation != otherLocal._rotation)
				return false;
			if (thisLocal._scale != otherLocal._scale)
				return false;
		}
		return true;
	}
	bool Pose::operator!=(const Pose& other)
	{
		return !(*this == other);
	}
    bool Pose::IsInHierarchy(Pose &pose, u16 root, u16 search)
    {
       if (search == root)
       {
           return true;
       }
       for (u16 p = pose.GetParent(search); p != Joint::kInvalidJointIndex; p = pose.GetParent(p))
       {
           if (p == root)
           {
               return true;
           }
       }
       return false;
    }
    void Pose::Blend(Pose &out, const Pose &a, const Pose &b, f32 t,u16 root)
    {
        u16 joint_num = out.Size();
        for(u16 i = 0; i < joint_num; ++i)
        {
            if (root != Joint::kInvalidJointIndex && !IsInHierarchy(out, root, i))
            {
                continue;
            }
            out.SetLocalTransform(i, Transform::Mix(a.GetLocalTransform(i), b.GetLocalTransform(i), t));
        }
    }
}
