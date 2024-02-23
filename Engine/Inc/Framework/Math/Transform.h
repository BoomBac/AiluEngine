#pragma once
#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__
#include "ALMath.hpp"
#include "GlobalMarco.h"

namespace Ailu
{
	//https://www.andre-gaschler.com/rotationconverter/
	struct Transform
	{
	public:
		Transform* _p_parent;
		Vector3f _position;
		Quaternion _rotation;
		Vector3f _scale;
		Vector3f _forward = Vector3f::kForward;
		Vector3f _right = Vector3f::kRight;
		Vector3f _up = Vector3f::kUp;
		static Matrix4x4f ToMatrix(const Transform& transform)
		{
			Vector3f x = transform._rotation * Vector3f(1, 0, 0);
			Vector3f y = transform._rotation * Vector3f(0, 1, 0);
			Vector3f z = transform._rotation * Vector3f(0, 0, 1);
			x = x * transform._scale.x; // Vector * float
			y = y * transform._scale.y; // Vector * float
			z = z * transform._scale.z; // Vector * float
			Vector3f t = transform._position;
			return { {{
				{x.x, x.y, x.z, 0}, // X basis (& Scale)
				{y.x, y.y, y.z, 0}, // Y basis (& scale)
				{z.x, z.y, z.z, 0}, // Z basis (& scale)
				{ t.x, t.y, t.z, 1 }  // Position
			}} };
		}

		static void ToMatrix(const Transform& transform, Matrix4x4f& out_matrix)
		{
			Vector3f x = transform._rotation * Vector3f(1, 0, 0);
			Vector3f y = transform._rotation * Vector3f(0, 1, 0);
			Vector3f z = transform._rotation * Vector3f(0, 0, 1);
			x = x * transform._scale.x; // Vector * float
			y = y * transform._scale.y; // Vector * float
			z = z * transform._scale.z; // Vector * float
			Vector3f t = transform._position;
			out_matrix.SetRow(0, Vector4f{x,0.f});
			out_matrix.SetRow(1, Vector4f{y,0.f});
			out_matrix.SetRow(2, Vector4f{z,0.f});
			out_matrix.SetRow(3, Vector4f{t,1.f});
		}

		//expensive! don't use realtime!
		static Transform FromMatrix(const Matrix4x4f& m)
		{
			Transform out;
			out._position = Vector3f(m[3][0], m[3][1], m[3][2]);
			out._rotation = Quaternion::FromMat4f(m);
			Matrix4x4f rotScaleMat{ {{
				{ m[0][0], m[0][1], m[0][2], 0 },
				{ m[1][0], m[1][1], m[1][2], 0 },
				{ m[2][0], m[2][1], m[2][2], 0 },
				{ 0, 0, 0, 1 }
			}} };
			Matrix4x4f invRotMat = Quaternion::ToMat4f(Quaternion::Inverse(out._rotation));
			Matrix4x4f scaleSkewMat = rotScaleMat * invRotMat;
			out._scale = Vector3f(scaleSkewMat[0][0],scaleSkewMat[1][1],scaleSkewMat[2][2]);
			return out;
		}

		static Transform Combine(const Transform& a, const Transform& b) 
		{
			Transform out;
			out._scale = a._scale * b._scale;
			out._rotation = b._rotation * a._rotation;
			out._position = a._rotation * (a._scale * b._position);
			out._position = a._position + out._position;
			return out;
		}

		static Transform Inverse(const Transform& t)
		{
			Transform inv;
			inv._rotation = Quaternion::Inverse(t._rotation);
			inv._scale.x = fabs(t._scale.x) < kFloatEpsilon ?
				0.0f : 1.0f / t._scale.x;
			inv._scale.y = fabs(t._scale.y) < kFloatEpsilon ?
				0.0f : 1.0f / t._scale.y;
			inv._scale.z = fabs(t._scale.z) < kFloatEpsilon ?
				0.0f : 1.0f / t._scale.z;
			Vector3f invTrans = t._position * -1.0f;
			inv._position = inv._rotation * (inv._scale * invTrans);
			return inv;
		}

		static Transform Mix(const Transform& a, const Transform& b, float t)
		{
			Quaternion bRot = b._rotation;
			if (Quaternion::Dot(a._rotation, bRot) < 0.0f) {
				bRot = -bRot;
			}
			return Transform(
				lerp(a._position, b._position, t),
				Quaternion::NLerp(a._rotation, bRot, t),
				lerp(a._scale, b._scale, t));
		}

		static Vector3f TransformPoint(const Transform& a, const Vector3f& b)
		{
			Vector3f out;
			out = a._rotation * (a._scale * b);
			out = a._position + out;
			return out;
		}

		static Vector3f TransformVector(const Transform& a, const Vector3f& b)
		{
			Vector3f out;
			out = a._rotation * (a._scale * b);
			return out;
		}

		static Transform GetWorldTransform(const Transform& transform)
		{
			Transform worldTransform = transform; // This is acopy, not a reference
			if (transform._p_parent) 
			{
				Transform worldParent = GetWorldTransform(*transform._p_parent);
				// Accumulate scale, Vector * Vector
				worldTransform._scale = worldParent._scale * worldTransform._scale;
				// Accumulate rotation, Quaternion * Quaternion
				// Remember, quaternions multiply in reverse order! So:
				// parent times child is written as: child * parent
				worldTransform._rotation = worldTransform._rotation * worldParent._rotation;
				// Accumulate position: scale first, Vector * vector
				worldTransform._position = worldParent._scale * worldTransform._position;
				// Accumulate position: rotate next, vector * Quaternion (quats rotate right to left)
				worldTransform._position = worldParent._rotation * worldTransform._position;
				// Accumulate position: transform last, Vector + Vector
				worldTransform._position = worldParent._position + worldTransform._position;
			}
			return worldTransform;
		}

		static Matrix4x4f GetWorldMatrix(Transform transform)
		{
			Transform worldSpaceTransform = GetWorldTransform(transform);
			return ToMatrix(worldSpaceTransform);
		}

		static Quaternion GetWorldRotation(const Transform& t)
		{
			return GetWorldTransform(t)._rotation;
		}

		static Vector3f GetWorldPosition(const Transform& t)
		{
			return GetWorldTransform(t)._position;
		}

		static Vector3f GetWorldScale(const Transform& t)
		{
			return GetWorldTransform(t)._scale;
		}

		static Transform LocalInverse(const Transform& t)
		{
			Quaternion invRotation = Quaternion::Inverse(t._rotation);
			Vector3f invScale = Vector3f(0, 0, 0);
			if (t._scale.x != 0.0f)
				invScale.x = 1.0f / t._scale.x;
			if (t._scale.y != 0)
				invScale.y = 1.0f / t._scale.y;
			if (t._scale.z != 0)
				invScale.z = 1.0f / t._scale.z;
			Vector3f invTranslation = invRotation * (invScale * (-1.0f * t._position));
			Transform result;
			result._position = invTranslation;
			result._rotation = invRotation;
			result._scale = invScale;
			return result;
		}

		static void SetGlobalSRT(Transform& t, Vector3f s, Quaternion r, Vector3f p)
		{
			if (t._p_parent == nullptr) 
			{
				t._rotation = r;
				t._position = p;
				t._scale = s;
				return;
			}
			auto worldParent = GetWorldTransform(*t._p_parent);
			auto invParent = LocalInverse(worldParent);
			Transform worldXForm;
			worldXForm._position = p;
			worldXForm._rotation = r;
			worldXForm._scale = s;
			worldXForm = Combine(invParent, worldXForm);
			t._position = worldXForm._position;
			t._rotation = worldXForm._rotation;
			t._scale = worldXForm._scale;
		}

		void SetGlobalRotation(Transform& t, Quaternion rotation)
		{
			Transform worldXForm = GetWorldTransform(t);
			SetGlobalSRT(t, worldXForm._scale, rotation, worldXForm._position);
		}

		void SetGlobalPosition(Transform& t, Vector3f position)
		{
			Transform worldXForm = GetWorldTransform(t);
			SetGlobalSRT(t, worldXForm._scale, worldXForm._rotation, position);
		}

		void SetGlobalScale(Transform& t, Vector3f scale)
		{
			Transform worldXForm = GetWorldTransform(t);
			SetGlobalSRT(t, scale, worldXForm._rotation, worldXForm._position);
		}

	public:
		Transform() : _position(Vector3f::kZero), _rotation(Quaternion()), _scale(Vector3f::kOne) ,_p_parent(nullptr){}
		Transform(const Vector3f& p, const Quaternion& r, const Vector3f& s) :
			_position(p), _rotation(r), _scale(s) , _p_parent(nullptr) {}
	}; 
}


#endif // !TRANSFORM_H__

