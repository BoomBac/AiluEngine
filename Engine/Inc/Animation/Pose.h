#pragma once
#ifndef __POSE_H__
#define __POSE_H__
#include "Framework/Math/Transform.h"

namespace Ailu
{
	//https://github.com/apachecn/apachecn-c-cpp-zh/blob/master/docs/handson-cpp-game-ani-prog/09.md
	//表示动画在某一帧的状态
	class AILU_API Pose
	{
	public:
        static bool IsInHierarchy(Pose& pose, u16 root, u16 search) ;
        //root = 65535则完全混合两个动画，两个姿势需要具有相同的骨骼结构
        static void Blend(Pose& out, const Pose& a, const Pose& b, f32 t,u16 root);
		Pose();
		Pose(const Pose& p);
		Pose& operator=(const Pose& p);
		explicit Pose(unsigned int numJoints);
		void Resize(unsigned int size);
		unsigned int Size();
		int GetParent(unsigned int index);
		void SetParent(unsigned int index, int parent);
		[[nodiscard]] Transform GetLocalTransform(unsigned int index) const;
		void SetLocalTransform(unsigned int index,const Transform& transform);
		[[nodiscard]] Transform GetGlobalTransform(unsigned int index) const;
		Transform operator[](unsigned int index);
		void GetMatrixPalette(Vector<Matrix4x4f>& out);
		bool operator==(const Pose& other);
		bool operator!=(const Pose& other);
        //返回search是否为root的子节点
	private:
		Vector<Transform> _joints;
		Vector<u16> _parents;
	};
}


#endif // !POSE_H__

