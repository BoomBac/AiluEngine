#include "pch.h"
#include "Render/Mesh.h"

namespace Ailu
{
	Mesh::Mesh()
	{
		_vertices = nullptr;
		_normals = nullptr;
		_colors = nullptr;
		_tangents = nullptr;
		_uv0 = nullptr;
		_vertex_count = 0;
	}
	Mesh::~Mesh()
	{
		DESTORY_PTR(_vertices);
		DESTORY_PTR(_normals);
		DESTORY_PTR(_colors);
		DESTORY_PTR(_tangents);
		DESTORY_PTR(_uv0);
		_vertex_count = 0;
	}
	void Mesh::SetVertices(Vector3f* vertices)
	{
		_vertices = vertices;
	}
}
