#pragma once
#ifndef __POSE_H__
#define __POSE_H__
#include "Framework/Math/Transform.h"


namespace Ailu
{
	//https://github.com/apachecn/apachecn-c-cpp-zh/blob/master/docs/handson-cpp-game-ani-prog/09.md
	//表示动画在某一帧的状态
	class Pose
	{
	public:
		Pose();
		Pose(const Pose& p);
		Pose& operator=(const Pose& p);
		Pose(unsigned int numJoints);
		void Resize(unsigned int size);
		unsigned int Size();
		int GetParent(unsigned int index);
		void SetParent(unsigned int index, int parent);
		Transform GetLocalTransform(unsigned int index);
		void SetLocalTransform(unsigned int index,const Transform& transform);
		Transform GetGlobalTransform(unsigned int index);
		Transform operator[](unsigned int index);
		void GetMatrixPalette(Vector<Matrix4x4f>& out);
		bool operator==(const Pose& other);
		bool operator!=(const Pose& other);
	private:
		Vector<Transform> _joints;
		Vector<u32> _parents;
	};
}


#endif // !POSE_H__

