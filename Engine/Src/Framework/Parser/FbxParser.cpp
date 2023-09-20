#include "pch.h"
#include "Framework/Parser/FbxParser.h"

using fbxsdk::FbxNodeAttribute;
using fbxsdk::FbxCast;
using fbxsdk::FbxNode;

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable: 4244)
	FbxParser::FbxParser()
	{
		fbx_manager_ = FbxManager::Create();
		fbx_ios_ = FbxIOSettings::Create(fbx_manager_, IOSROOT);
		fbx_manager_->SetIOSettings(fbx_ios_);
		fbx_importer_ = FbxImporter::Create(fbx_manager_, "");
	}
	Ref<Mesh> FbxParser::Parser(const std::string_view& path)
	{
		_time_mgr.Mark();
		FbxScene* fbx_scene = FbxScene::Create(fbx_manager_, "RootScene");
		if (fbx_importer_ != nullptr && !fbx_importer_->Initialize(path.data(), -1, fbx_manager_->GetIOSettings()))
			return nullptr;
		fbx_importer_->Import(fbx_scene);
		FbxNode* fbx_rt = fbx_scene->GetRootNode();
		fbxsdk::FbxAxisSystem::DirectX.DeepConvertScene(fbx_scene);
		if (fbx_scene->GetGlobalSettings().GetSystemUnit() != fbxsdk::FbxSystemUnit::cm)
		{
			const FbxSystemUnit::ConversionOptions lConversionOptions = {
			false, /* mConvertRrsNodes */
			true, /* mConvertAllLimits */
			true, /* mConvertClusters */
			true, /* mConvertLightIntensity */
			true, /* mConvertPhotometricLProperties */
			true  /* mConvertCameraClipPlanes */
			};
			fbxsdk::FbxSystemUnit::cm.ConvertScene(fbx_scene, lConversionOptions);
		}
		std::vector<Ref<Mesh>> meshs{};
		if (fbx_rt != nullptr)
		{
			auto child_num = fbx_rt->GetChildCount();
			for (int i = 0; i < child_num; i++)
			{
				FbxNode* fbx_node = fbx_rt->GetChild(i);
				FbxNodeAttribute* attribute = fbx_node->GetNodeAttribute();
				std::string node_name = fbx_node->GetName();
				auto type = attribute->GetAttributeType();
				if (attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
				{
					fbxsdk::FbxMesh* fbx_mesh = FbxCast<fbxsdk::FbxMesh>(attribute);
					Ref<Mesh> mesh = MakeRef<Mesh>(node_name);
					if (!GenerateMesh(mesh, fbx_mesh)) continue;
					LOG_INFO("Loading mesh: {} takes {}ms", node_name, _time_mgr.GetElapsedSinceLastMark());
					mesh->Build();
					MeshPool::AddMesh(mesh);
					meshs.emplace_back(mesh);
				}
			}
		}
		if (meshs.empty())
		{
			AL_ASSERT(true, "Load mesh failed!");
			return nullptr;
		}
		else return *(meshs.end()-1);
	}
	Ailu::FbxParser::~FbxParser()
	{
	}
	bool FbxParser::GenerateMesh(Ref<Mesh>& mesh, fbxsdk::FbxMesh* fbx_mesh)
	{
		if (!fbx_mesh->IsTriangleMesh()) 
		{
			fbxsdk::FbxGeometryConverter convert(fbx_manager_);
			fbx_mesh = FbxCast<fbxsdk::FbxMesh>(convert.Triangulate(fbx_mesh, true));
		}
		
		//The vertex must be added to the MeshObject's vector before the normal, 
		//because the input layout is passed in the vertex-normal order
		//not thread safe now 
		auto ret1 = ReadVertex(*fbx_mesh, mesh);
		auto ret2 = ReadNormal(*fbx_mesh, mesh);
		auto ret3 = ReadUVs(*fbx_mesh, mesh);
		auto ret4 = ReadTangent(*fbx_mesh, mesh);
		GenerateIndexdMesh(mesh);
		CalculateTangant(mesh);
		//auto ret1 = p_thread_pool_->Enqueue(&FbxParser::ReadVertex, this, std::ref(*mesh), nut_mesh);
		//auto ret2 = p_thread_pool_->Enqueue(&FbxParser::ReadNormal, this, std::ref(*mesh), nut_mesh);
		//auto ret3 = p_thread_pool_->Enqueue(&FbxParser::ReadUVs, this, std::ref(*mesh), nut_mesh);
		//	auto ret4 = p_thread_pool_->Enqueue(&FbxParser::ReadTangent, this, std::ref(*mesh), nut_mesh);
		//if (ret1.get() && ret2.get() && ret3.get())
		//{
		//	CalculateTangant(nut_mesh);
		//	geo->AddMesh(nut_mesh);
		//	scene.Geometries[mesh->GetName()] = geo;
		//	return true;
		//}
		return ret1 && ret2 && ret3 && ret4;
	}

	bool FbxParser::ReadNormal(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh)
	{
		if (fbx_mesh.GetElementNormalCount() < 1) return false;
		auto* normals = fbx_mesh.GetElementNormal(0);
		int vertex_count = fbx_mesh.GetControlPointsCount(), data_size = 0;
		void* data = nullptr;
		if (normals->GetMappingMode() == fbxsdk::FbxLayerElement::EMappingMode::eByControlPoint)
		{
			data = new float[vertex_count * 3];
			for (int i = 0; i < vertex_count; ++i)
			{
				int normal_index = 0;
				if (normals->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eDirect)
					normal_index = i;
				else if (normals->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eIndexToDirect)
					normal_index = normals->GetIndexArray().GetAt(i);
				auto normal = normals->GetDirectArray().GetAt(normal_index);
				reinterpret_cast<float*>(data)[i * 3] = normal[0];
				reinterpret_cast<float*>(data)[i * 3 + 1] = normal[1];
				reinterpret_cast<float*>(data)[i * 3 + 2] = normal[2];
			}
		}
		else if (normals->GetMappingMode() == fbxsdk::FbxLayerElement::EMappingMode::eByPolygonVertex)
		{
			int trangle_count = fbx_mesh.GetPolygonCount();
			vertex_count = trangle_count * 3;
			data = new float[vertex_count * 3];
			int cur_vertex_id = 0;
			for (int i = 0; i < trangle_count; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					int normal_index = 0;
					if (normals->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eDirect)
						normal_index = cur_vertex_id;
					else if (normals->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eIndexToDirect)
						normal_index = normals->GetIndexArray().GetAt(cur_vertex_id);
					auto normal = normals->GetDirectArray().GetAt(normal_index);
					reinterpret_cast<float*>(data)[cur_vertex_id * 3] = normal[0];
					reinterpret_cast<float*>(data)[cur_vertex_id * 3 + 1] = normal[1];
					reinterpret_cast<float*>(data)[cur_vertex_id * 3 + 2] = normal[2];
					++cur_vertex_id;
				}
			}
		}
		mesh->SetNormals(std::move(reinterpret_cast<Vector3f*>(data)));
		return true;
	}

	bool FbxParser::ReadVertex(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh)
	{
		//fbxsdk::FbxVector4* points{ fbx_mesh.GetControlPoints() };
		//uint32_t cur_index_count = 0u;
		//std::unordered_map<Vector3f, uint16_t,ALHash::Vector3fHash,ALHash::Vector3Equal> vertexMap{};
		//std::vector<uint32_t> indices{};
		//std::vector<Vector3f> positions{};
		//for (int32_t i = 0; i < fbx_mesh.GetPolygonCount(); ++i)
		//{
		//	for (int32_t j = 0; j < 3; ++j)
		//	{
		//		auto p = points[fbx_mesh.GetPolygonVertex(i, j)];
		//		Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
		//		auto it = vertexMap.find(position);
		//		if (it == vertexMap.end())
		//		{
		//			vertexMap[position] = cur_index_count;
		//			indices.emplace_back(cur_index_count);
		//			positions.emplace_back(position);
		//			cur_index_count++;
		//		}
		//		else indices.emplace_back(it->second);
		//	}
		//}
		//uint32_t index_count = indices.size(), vertex_count = positions.size();
		//float* vertex_buf = new float[vertex_count * 3];
		//uint32_t* index_buf = new uint32_t[index_count];
		//memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
		//memcpy(index_buf, indices.data(), sizeof(uint32_t) * index_count);
		//mesh->_vertex_count = vertex_count;
		//mesh->_index_count = index_count;
		//mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));
		//mesh->SetIndices(std::move(reinterpret_cast<uint32_t*>(index_buf)));
		//return true;
		fbxsdk::FbxVector4* points{ fbx_mesh.GetControlPoints() };
		uint32_t cur_index_count = 0u;
		std::vector<Vector3f> positions{};
		for (int32_t i = 0; i < fbx_mesh.GetPolygonCount(); ++i)
		{
			for (int32_t j = 0; j < 3; ++j)
			{
				auto p = points[fbx_mesh.GetPolygonVertex(i, j)];
				Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
				positions.emplace_back(position);
			}
		}
		uint32_t vertex_count = positions.size();
		float* vertex_buf = new float[vertex_count * 3];
		memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
		mesh->_vertex_count = vertex_count;
		mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));
		return true;
	}

	bool FbxParser::ReadUVs(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh)
	{
		//get all UV set names
		fbxsdk::FbxStringList name_list;
		fbx_mesh.GetUVSetNames(name_list);
		for (int i = 0; i < name_list.GetCount(); ++i)
		{
			//get lUVSetIndex-th uv set
			const char* uv_name = name_list.GetStringAt(i);
			const FbxGeometryElementUV* uv = fbx_mesh.GetElementUV(uv_name);
			if (!uv) continue;
			//index array, where holds the index referenced to the uv data
			const bool lUseIndex = uv->GetReferenceMode() != FbxGeometryElement::eDirect;
			const int lIndexCount = (lUseIndex) ? uv->GetIndexArray().GetCount() : 0;
			//iterating through the data by polygon
			const int trangle_count = fbx_mesh.GetPolygonCount();
			float* data = new float[trangle_count * 6];
			if (uv->GetMappingMode() == FbxGeometryElement::eByControlPoint)
			{
				int cur_vertex_id = 0;
				for (int lPolyIndex = 0; lPolyIndex < trangle_count; ++lPolyIndex)
				{
					// build the max index array that we need to pass into MakePoly
					for (int lVertIndex = 0; lVertIndex < 3; ++lVertIndex)
					{
						//get the index of the current vertex in control points array
						int lPolyVertIndex = fbx_mesh.GetPolygonVertex(lPolyIndex, lVertIndex);
						//the UV index depends on the reference mode
						int lUVIndex = lUseIndex ? uv->GetIndexArray().GetAt(lPolyVertIndex) : lPolyVertIndex;
						FbxVector2 uvs = uv->GetDirectArray().GetAt(lUVIndex);
						reinterpret_cast<float*>(data)[cur_vertex_id * 2] = uv->GetDirectArray().GetAt(lUVIndex)[0];
						reinterpret_cast<float*>(data)[cur_vertex_id * 2 + 1] = 1.f - uv->GetDirectArray().GetAt(lUVIndex)[1];
						++cur_vertex_id;
					}
				}
			}
			else if (uv->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
			{
				int lPolyIndexCounter = 0;
				for (int lPolyIndex = 0; lPolyIndex < trangle_count; ++lPolyIndex)
				{
					for (int lVertIndex = 0; lVertIndex < 3; ++lVertIndex)
					{
						//the UV index depends on the reference mode
						int lUVIndex = lUseIndex ? uv->GetIndexArray().GetAt(lPolyIndexCounter) : lPolyIndexCounter;
						FbxVector2 uvs = uv->GetDirectArray().GetAt(lUVIndex);
						reinterpret_cast<float*>(data)[lPolyIndexCounter * 2] = uv->GetDirectArray().GetAt(lUVIndex)[0];
						reinterpret_cast<float*>(data)[lPolyIndexCounter * 2 + 1] = 1.f - uv->GetDirectArray().GetAt(lUVIndex)[1];
						//NE_LOG(ALL,kWarning,"U:{}V:{}",uvs[0],uvs[1])
						lPolyIndexCounter++;
					}
				}
			}
			mesh->SetUVs(std::move(reinterpret_cast<Vector2f*>(data)),i);
		}
		return true;
	}

	bool FbxParser::ReadTangent(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh)
	{
		int tangent_num = fbx_mesh.GetElementTangentCount();
		AL_ASSERT(tangent_num < 0,"ReadTangent");
		auto* tangents = fbx_mesh.GetElementTangent();
		if (tangents == nullptr)
		{
			LOG_WARNING("FbxMesh: {} don't contain tangent info!", fbx_mesh.GetName());
			return true;
		}
		int vertex_count = fbx_mesh.GetControlPointsCount(), data_size = 0;
		void* data = nullptr;
		if (tangents->GetMappingMode() == fbxsdk::FbxGeometryElement::eByPolygonVertex)
		{
			int trangle_count = fbx_mesh.GetPolygonCount();
			vertex_count = trangle_count * 3;
			data = new float[vertex_count * 4];
			int cur_vertex_id = 0;
			for (int trangle_id = 0; trangle_id < trangle_count; ++trangle_id)
			{
				for (int point_id = 0; point_id < 3; ++point_id)
				{
					UINT tangent_id = 0;
					if (tangents->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eDirect)
						tangent_id = cur_vertex_id;
					else if (tangents->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eIndexToDirect)
						tangent_id = tangents->GetIndexArray().GetAt(cur_vertex_id);
					auto tangent = tangents->GetDirectArray().GetAt(tangent_id);
					reinterpret_cast<float*>(data)[cur_vertex_id * 3] = tangent[0];
					reinterpret_cast<float*>(data)[cur_vertex_id * 3 + 1] = tangent[1];
					reinterpret_cast<float*>(data)[cur_vertex_id * 3 + 2] = tangent[2];
					++cur_vertex_id;
				}
			}
		}
		else if (tangents->GetMappingMode() == fbxsdk::FbxGeometryElement::eByControlPoint)
		{
			data = new float[vertex_count * 3];
			for (UINT vertex_id = 0; vertex_id < vertex_count; ++vertex_id)
			{
				UINT tangent_id = 0;
				if (tangents->GetReferenceMode() == fbxsdk::FbxGeometryElement::eDirect)
					tangent_id = vertex_id;
				else if (tangents->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndexToDirect)
					tangent_id = tangents->GetIndexArray().GetAt(vertex_id);
				auto tangent = tangents->GetDirectArray().GetAt(tangent_id);
				reinterpret_cast<float*>(data)[vertex_id * 3] = tangent[0];
				reinterpret_cast<float*>(data)[vertex_id * 3 + 1] = tangent[1];
				reinterpret_cast<float*>(data)[vertex_id * 3 + 2] = tangent[2];
			}

		}
		mesh->SetTangents(std::move(reinterpret_cast<Vector4f*>(data)));
		return true;
	}

	bool FbxParser::CalculateTangant(Ref<Mesh>& mesh)
	{
		uint32_t* indices = mesh->GetIndices();
		Vector3f* pos = mesh->GetVertices();
		Vector2f* uv0 = mesh->GetUVs(0);
		Vector4f* tangents = new Vector4f[mesh->_vertex_count];
		for (size_t trangle_count = 0; trangle_count < mesh->_index_count / 3; trangle_count++)
		{
			const Vector3f& pos1 = pos[indices[trangle_count]];
			const Vector3f& pos2 = pos[indices[trangle_count + 1]];
			const Vector3f& pos3 = pos[indices[trangle_count + 2]];
			const Vector2f& uv1 = uv0[indices[trangle_count]];
			const Vector2f& uv2 = uv0[indices[trangle_count + 1]];
			const Vector2f& uv3 = uv0[indices[trangle_count + 2]];
			Vector3f edge1 = pos2 - pos1;
			Vector3f edge2 = pos3 - pos1;
			Vector2f duv1 = uv2 - uv1;
			Vector2f duv2 = uv3 - uv1;
			Vector4f tangent{};
			float f = 1.f / (duv1.x * duv2.y - duv2.x * duv1.y);
			tangent.x = f * (duv2.y * edge1.x - duv1.y * edge2.x);
			tangent.y = f * (duv2.y * edge1.y - duv1.y * edge2.y);
			tangent.z = f * (duv2.y * edge1.z - duv1.y * edge2.z);
			tangent.w = 1.0f;
			Normalize(tangent);
			tangents[indices[trangle_count]] = tangent;
			tangents[indices[trangle_count + 1]] = tangent;
			tangents[indices[trangle_count + 2]] = tangent;
		}
		mesh->SetTangents(std::move(tangents));
		return true;
	}

	void FbxParser::GenerateIndexdMesh(Ref<Mesh>& mesh)
	{
		uint32_t vertex_count = mesh->_vertex_count;
		std::unordered_map<Vector3f, uint16_t,ALHash::Vector3fHash,ALHash::Vector3Equal> normal_map{};
		std::unordered_map<Vector2f, uint16_t,ALHash::Vector2fHash,ALHash::Vector2Equal> uv_map{};
		std::unordered_map<uint64_t, uint16_t> vertex_map{};
		std::vector<Vector3f> normals{};
		std::vector<Vector3f> positions{};
		std::vector<Vector2f> uv0s{};
		std::vector<uint32_t> indices{};
		uint32_t cur_index_count = 0u;
		auto raw_normals = mesh->GetNormals();
		auto raw_uv0 = mesh->GetUVs(0u);
		auto raw_pos = mesh->GetVertices();
		ALHash::Vector3fHash v3hash{};
		ALHash::Vector2fHash v2hash{};
		for (size_t i = 0; i < vertex_count; i++)
		{
			auto p = raw_pos[i], n = raw_normals[i];
			auto uv = raw_uv0[i];
			auto hash0 = v3hash(n), hash1 = v2hash(uv);
			auto vertex_hash = ALHash::CombineHashes(hash0, hash1);
			auto it = vertex_map.find(vertex_hash);
			if (it == vertex_map.end())
			{
				vertex_map[vertex_hash] = cur_index_count;
				indices.emplace_back(cur_index_count++);
				normals.emplace_back(n);
				positions.emplace_back(p);
				uv0s.emplace_back(uv);
			}
			else indices.emplace_back(it->second);
			//auto it0 = normal_map.find(n);
			//auto it1 = uv_map.find(uv);
			//if (it0 == normal_map.end() || it1 == uv_map.end())
			//{
			//	normal_map[n] = cur_index_count;
			//	uv_map[uv] = cur_index_count;
			//	indices.emplace_back(cur_index_count++);
			//	normals.emplace_back(n);
			//	positions.emplace_back(p);
			//	uv0s.emplace_back(uv);
			//}
			//else indices.emplace_back(it0->second);
		}
		mesh->Clear();
		vertex_count = positions.size();
		auto index_count = indices.size();
		mesh->_vertex_count = vertex_count;
		mesh->_index_count = index_count;
		float* vertex_buf = new float[vertex_count * 3];
		memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
		mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));
		float* normal_buf = new float[vertex_count * 3];
		memcpy(normal_buf, normals.data(), sizeof(Vector3f) * vertex_count);
		mesh->SetNormals(std::move(reinterpret_cast<Vector3f*>(normal_buf)));
		float* uv0_buf = new float[vertex_count * 2];
		memcpy(uv0_buf, uv0s.data(), sizeof(Vector2f) * vertex_count);
		mesh->SetUVs(std::move(reinterpret_cast<Vector2f*>(uv0_buf)),0u);
		uint32_t* index_buf = new uint32_t[index_count];
		memcpy(index_buf, indices.data(), sizeof(uint32_t) * index_count);
		mesh->SetIndices(std::move(reinterpret_cast<uint32_t*>(index_buf)));
	}
#pragma warning(pop)
}
