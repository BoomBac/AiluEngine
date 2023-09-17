#pragma once
#ifndef __MESH_H__
#define __MESH_H__
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"

namespace Ailu
{
	class Mesh
	{
	public:
		Mesh();
		~Mesh();
		void SetVertices(Vector3f* vertices);
		void SetNormals(Vector3f* normals);
		void SetTangents(Vector3f* tangents);
		void SetUVs(Vector2f* uv, uint8_t index);
		inline  Vector3f* GetVertices() { return _vertices; };
		inline Vector3f* GetNormals() { return _normals; };
		inline Vector3f* GetTangents() { return _tangents; };
		inline Vector2f* GetUVs(uint8_t index) { return _uv[index]; };
		void Clear();
		void SetIndices(uint32_t* indices);
		const Ref<VertexBuffer>& GetVertexBuffer() const;
		const Ref<IndexBuffer>& GetIndexBuffer() const;
		void Build();

	public:
		uint32_t _vertex_count;
		uint16_t _index_count;
	private:
		Ref<VertexBuffer> _p_vbuf;
		Ref<IndexBuffer> _p_ibuf;
		Vector3f* _vertices;
		Vector3f* _normals ;
		Color* _colors ;
		Vector3f* _tangents;
		Vector2f** _uv;
		uint32_t* _p_indices;
	};
}


#endif // !__MESH_H__

