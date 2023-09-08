#pragma once
#ifndef __MESH_H__
#define __MESH_H__
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	class AILU_API Mesh
	{
	public:
		Mesh();
		~Mesh();
		void SetVertices(Vector3f* vertices);
	private:
		Vector3f* _vertices;
		Vector3f* _normals ;
		Color* _colors ;
		Vector3f* _tangents;
		Vector2f* _uv0;
		int _vertex_count;
	};
}


#endif // !__MESH_H__

