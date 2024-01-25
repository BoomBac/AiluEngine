#include "pch.h"
#include <limits>
#include "Framework/Parser/FbxParser.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/Utils.h"
#include "Animation/Skeleton.h"

using fbxsdk::FbxNodeAttribute;
using fbxsdk::FbxCast;
using fbxsdk::FbxNode;

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable: 4244)

	static int FindJointIndexByName(const FbxString& name, const Skeleton& sk)
	{
		for (int i = 0; i < sk._joints.size(); i++)
		{
			if (name.Buffer() == sk._joints[i]._name)
				return i;
		}
		return -1;
	}
	static Matrix4x4f FbxMatToMat4x4f(const FbxAMatrix& src)
	{
		Matrix4x4f out{};
		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
				out[i][j] = src[i][j];
		}
		return out;
	}

	static void ParserSkeleton(FbxNode* node, Skeleton& sk, FbxTime start, FbxTime end, FbxTime::EMode TimeMode, Vector<Vector<Matrix4x4f>>& anim)
	{
		FbxNodeAttribute* attr = node->GetNodeAttribute();
		fbxsdk::FbxAMatrix init_local_transf = node->EvaluateGlobalTransform();
		if (attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			Vector<Matrix4x4f> anim_frame;
			for (FbxLongLong Frame = start.GetFrameCount(TimeMode); Frame <= end.GetFrameCount(TimeMode); ++Frame)
			{
				FbxTime Time;
				Time.SetFrame(Frame, TimeMode);

				// The global node transform is equal to your local skeleton root if there is no parent bone 
				fbxsdk::FbxAMatrix local_transf = node->EvaluateGlobalTransform(Time);
				if (fbxsdk::FbxNode* parent = node->GetParent())
				{
					FbxNodeAttribute* ParentAttribute = parent->GetNodeAttribute();
					if (ParentAttribute && ParentAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
					{
						FbxAMatrix GlobalParentTransform = parent->EvaluateGlobalTransform();
						local_transf = GlobalParentTransform.Inverse() * local_transf;
					}
				}

				anim_frame.emplace_back(FbxMatToMat4x4f(local_transf));
				// Now you can decompose
				FbxVector4 t = local_transf.GetT();
				FbxQuaternion q = local_transf.GetQ();
				FbxVector4 s = local_transf.GetS();
			}
			Joint joint;
			joint._name = node->GetName();
			joint._parent = sk.joint_num++;
			sk._joints.emplace_back(joint);
			anim.emplace_back(anim_frame);
		}
		if (attr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
		}
		for (int j = 0; j < node->GetChildCount(); j++)
		{
			ParserSkeleton(node->GetChild(j), sk, start, end, TimeMode, anim);
		}
	}


	FbxParser::FbxParser()
	{
		fbx_manager_ = FbxManager::Create();
		fbx_ios_ = FbxIOSettings::Create(fbx_manager_, IOSROOT);
		fbx_manager_->SetIOSettings(fbx_ios_);
		fbx_importer_ = FbxImporter::Create(fbx_manager_, "");
	}

	static Vector3f scale_factor{};
	static String space_str = "--";
	List<Ref<Mesh>> FbxParser::Parser(std::string_view sys_path)
	{
		// Convert string to wstring
		return ParserImpl(ToWChar(sys_path.data()));
	}
	List<Ref<Mesh>> FbxParser::Parser(const WString& sys_path)
	{
		return ParserImpl(sys_path);
	}
	void FbxParser::Parser(std::string_view sys_path, Skeleton& sk, Vector<Vector<Matrix4x4f>>& anim)
	{
		String path = sys_path.data();
		space_str = "--";
		FbxScene* fbx_scene = FbxScene::Create(fbx_manager_, "RootScene");
		if (fbx_importer_ != nullptr && !fbx_importer_->Initialize(path.c_str(), -1, fbx_manager_->GetIOSettings()))
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), "Load mesh failed at path {}", path);
		}
		fbx_importer_->Import(fbx_scene);
		FbxNode* node = fbx_scene->GetRootNode();
		fbxsdk::FbxAxisSystem::DirectX.DeepConvertScene(fbx_scene);
		if (fbx_scene->GetGlobalSettings().GetSystemUnit() != fbxsdk::FbxSystemUnit::cm)
		{
			const FbxSystemUnit::ConversionOptions lConversionOptions = { false,true,true,true,true,true };
			fbxsdk::FbxSystemUnit::cm.ConvertScene(fbx_scene, lConversionOptions);
		}
		FbxAnimStack* AnimStack = fbx_scene->GetSrcObject<FbxAnimStack>(0);
		fbx_scene->SetCurrentAnimationStack(AnimStack);

		FbxString Name = AnimStack->GetNameOnly();
		FbxString TakeName = AnimStack->GetName();
		FbxTakeInfo* TakeInfo = fbx_scene->GetTakeInfo(TakeName);
		FbxTimeSpan LocalTimeSpan = TakeInfo->mLocalTimeSpan;
		FbxTime Start = LocalTimeSpan.GetStart();
		FbxTime Stop = LocalTimeSpan.GetStop();
		FbxTime Duration = LocalTimeSpan.GetDuration();

		FbxTime::EMode TimeMode = FbxTime::GetGlobalTimeMode();
		FbxLongLong FrameCount = Duration.GetFrameCount(TimeMode);
		double FrameRate = FbxTime::GetFrameRate(TimeMode);

		if (node != nullptr)
		{
			auto child_num = node->GetChildCount();
			for (int i = 0; i < child_num; i++)
			{
				auto child = node->GetChild(i);
				FbxNodeAttribute* attribute = child->GetNodeAttribute();
				std::string node_name = node->GetNameOnly().Buffer();
				LOG_INFO("FBXNode: {}{}", space_str, node_name);
				space_str += "--";
				auto type = attribute->GetAttributeType();
				FbxAMatrix transformMatrix = node->EvaluateGlobalTransform();
				FbxVector4 scale = transformMatrix.GetS();
				scale_factor.x = scale[0];
				scale_factor.y = scale[1];
				scale_factor.z = scale[2];
				//ParserSkeleton(child, sk,Start,Stop,TimeMode, anim);
				LOG_INFO("{}", sk.joint_num);
			}
		}
	}

	Ailu::FbxParser::~FbxParser()
	{
	}

	//void FbxParser::ParserFbxNode(FbxNode* node, List<Ref<Mesh>>& loaded_meshes)
	//{
	//	std::string node_name = node->GetNameOnly().Buffer();
	//	LOG_INFO("FBXNode: {}{}", space_str, node_name);
	//	space_str += "--";
	//	if (node != nullptr)
	//	{
	//		FbxNodeAttribute* attribute = node->GetNodeAttribute();
	//		if (attribute)
	//		{
	//			auto type = attribute->GetAttributeType();
	//			FbxAMatrix transformMatrix = node->EvaluateGlobalTransform();
	//			FbxVector4 scale = transformMatrix.GetS();
	//			scale_factor.x = scale[0];
	//			scale_factor.y = scale[1];
	//			scale_factor.z = scale[2];
	//			if (type == FbxNodeAttribute::eMesh)
	//			{
	//				ParserMesh(node, loaded_meshes);
	//			}
	//			else if (type == FbxNodeAttribute::eSkeleton)
	//			{
	//				ParserSkeleton(node);
	//			}
	//		}
	//	}
	//	auto child_num = node->GetChildCount();
	//	for (int i = 0; i < child_num; i++)
	//	{
	//		auto child = node->GetChild(i);
	//		ParserFbxNode(child, loaded_meshes);
	//	}
	//	space_str = space_str.substr(space_str.size() - 2, 2);
	//}

	void FbxParser::ParserFbxNode(FbxNode* node, Queue<FbxNode*>& mesh_node, Queue<FbxNode*>& skeleton_node)
	{
		std::string node_name = node->GetNameOnly().Buffer();
		LOG_INFO("FBXNode: {}{}", space_str, node_name);
		space_str += "--";
		if (node != nullptr)
		{
			FbxNodeAttribute* attribute = node->GetNodeAttribute();
			if (attribute)
			{
				auto type = attribute->GetAttributeType();
				FbxAMatrix transformMatrix = node->EvaluateGlobalTransform();
				FbxVector4 scale = transformMatrix.GetS();
				scale_factor.x = scale[0];
				scale_factor.y = scale[1];
				scale_factor.z = scale[2];
				if (type == FbxNodeAttribute::eMesh)
				{
					mesh_node.emplace(node);
				}
				else if (type == FbxNodeAttribute::eSkeleton)
				{
					skeleton_node.emplace(node);
				}
			}
			auto child_num = node->GetChildCount();
			for (int i = 0; i < child_num; i++)
			{
				auto child = node->GetChild(i);
				ParserFbxNode(child, mesh_node, skeleton_node);
			}
		}
		space_str = space_str.substr(space_str.size() - 2, 2);
	}

	void FbxParser::ParserSkeleton(FbxNode* node, Skeleton& sk)
	{
		FbxNodeAttribute* attr = node->GetNodeAttribute();
		fbxsdk::FbxAMatrix init_local_transf = node->EvaluateGlobalTransform();
		if (attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			// The global node transform is equal to your local skeleton root if there is no parent bone 
			fbxsdk::FbxAMatrix local_transf = node->EvaluateGlobalTransform();
			if (fbxsdk::FbxNode* parent = node->GetParent())
			{
				FbxNodeAttribute* ParentAttribute = parent->GetNodeAttribute();
				if (ParentAttribute && ParentAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
				{
					FbxAMatrix GlobalParentTransform = parent->EvaluateGlobalTransform();
					local_transf = GlobalParentTransform.Inverse() * local_transf;
				}
			}
			Joint joint;
			joint._name = node->GetName();
			joint._parent = sk.joint_num;
			sk._joints.emplace_back(joint);
			sk.joint_num++;
		}
	}

	bool FbxParser::ParserMesh(FbxNode* node, List<Ref<Mesh>>& loaded_meshes)
	{
		auto fbx_mesh = node->GetMesh();
		if (!fbx_mesh->IsTriangleMesh())
		{
			fbxsdk::FbxGeometryConverter convert(fbx_manager_);
			fbx_mesh = FbxCast<fbxsdk::FbxMesh>(convert.Triangulate(fbx_mesh, true));
		}
		bool use_multi_thread = true;
		bool is_skined = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
		auto mesh = is_skined ? MakeRef<SkinedMesh>(node->GetName()) : MakeRef<Mesh>(node->GetName());
		if (use_multi_thread)
		{
			_time_mgr.Mark();
			auto ret1 = g_thread_pool->Enqueue(&FbxParser::ReadVertex, this, node, mesh.get());
			auto ret2 = g_thread_pool->Enqueue(&FbxParser::ReadNormal, this, std::ref(*fbx_mesh), mesh.get());
			auto ret3 = g_thread_pool->Enqueue(&FbxParser::ReadUVs, this, std::ref(*fbx_mesh), mesh.get());
			if (ret1.get() && ret2.get() && ret3.get())
			{
				LOG_INFO("Read mesh data takes {}ms", _time_mgr.GetElapsedSinceLastMark());
				_time_mgr.Mark();
				GenerateIndexdMesh(mesh.get());
				CalculateTangant(mesh.get());
				LOG_INFO("Generate indices and tangent data takes {}ms", _time_mgr.GetElapsedSinceLastMark());
				mesh->Build();
				if (is_skined)
					dynamic_cast<SkinedMesh*>(mesh.get())->CurSkeleton(_cur_skeleton);
				loaded_meshes.emplace_back(mesh);
				return true;
			}
			return false;
		}
		else
		{
			throw std::runtime_error("error");
		}
	}

	void FbxParser::ParserAnimation(Queue<FbxNode*>& skeleton_node, Skeleton& sk)
	{
		FbxAnimStack* AnimStack = _p_cur_fbx_scene->GetSrcObject<FbxAnimStack>(0);
		_p_cur_fbx_scene->SetCurrentAnimationStack(AnimStack);

		FbxString Name = AnimStack->GetNameOnly();
		FbxString TakeName = AnimStack->GetName();
		FbxTakeInfo* TakeInfo = _p_cur_fbx_scene->GetTakeInfo(TakeName);
		FbxTimeSpan LocalTimeSpan = TakeInfo->mLocalTimeSpan;
		FbxTime Start = LocalTimeSpan.GetStart();
		FbxTime Stop = LocalTimeSpan.GetStop();
		FbxTime Duration = LocalTimeSpan.GetDuration();

		FbxTime::EMode TimeMode = FbxTime::GetGlobalTimeMode();
		FbxLongLong FrameCount = Duration.GetFrameCount(TimeMode);
		double FrameRate = FbxTime::GetFrameRate(TimeMode);

		while (!skeleton_node.empty())
		{
			auto cur_node = skeleton_node.front();
			skeleton_node.pop();
			auto& joint = sk._joints[Skeleton::GetJointIndexByName(sk, cur_node->GetName())];
			joint._pose.clear();
			joint._frame_count = FrameCount;
			for (FbxLongLong Frame = Start.GetFrameCount(TimeMode); Frame <= Stop.GetFrameCount(TimeMode); ++Frame)
			{
				FbxTime Time;
				Time.SetFrame(Frame, TimeMode);

				// The global node transform is equal to your local skeleton root if there is no parent bone 
				fbxsdk::FbxAMatrix local_transf = cur_node->EvaluateGlobalTransform(Time);
				if (fbxsdk::FbxNode* parent = cur_node->GetParent())
				{
					FbxNodeAttribute* ParentAttribute = parent->GetNodeAttribute();
					if (ParentAttribute && ParentAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
					{
						FbxAMatrix GlobalParentTransform = parent->EvaluateGlobalTransform();
						local_transf = GlobalParentTransform.Inverse() * local_transf;
					}
				}
				// Now you can decompose
				FbxVector4 t = local_transf.GetT();
				FbxQuaternion q = local_transf.GetQ();
				FbxVector4 s = local_transf.GetS();
				joint._pose.emplace_back(FbxMatToMat4x4f(local_transf));
			}

		}
	}

	bool FbxParser::ReadNormal(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh)
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

	bool FbxParser::ReadVertex(fbxsdk::FbxNode* node, Mesh* mesh)
	{
		auto fbx_mesh = node->GetMesh();
		auto mesh_name = fbx_mesh->GetName();
		u32 deformers_num = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
		FbxAMatrix geometry_transform = GetGeometryTransformation(node);
		//control point : <joint_index, weight>,<joint_index, weight>...
		std::map<u32, Vector<std::pair<u16, float>>> control_point_weight_infos{};
		for (u32 i = 0; i < deformers_num; i++)
		{
			FbxSkin* skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(i, FbxDeformer::eSkin));
			if (!skin)
				continue;
			u32 cluster_num = skin->GetClusterCount();
			for (u32 j = 0; j < cluster_num; j++)
			{
				FbxCluster* cluster = skin->GetCluster(j);
				String joint_name = cluster->GetLink()->GetName();
				i32 joint_i = Skeleton::GetJointIndexByName(_cur_skeleton, joint_name);
				if (joint_i == -1)
				{
					LOG_WARNING("Can't find joint {} when load mesh {}", joint_name, mesh_name);
					continue;
				}
				FbxAMatrix transform_matrix, transform_link_matrix, global_bindpose_inv_matrix;
				cluster->GetTransformMatrix(transform_matrix);//transform of mesh at binding time
				cluster->GetTransformLinkMatrix(transform_link_matrix);//transform of joint at binding time from joint space -> world_space
				global_bindpose_inv_matrix = transform_link_matrix.Inverse() * transform_matrix * geometry_transform;
				u16 joint_index = (u16)joint_i;
				_cur_skeleton._joints[joint_index]._inv_bind_pos = FbxMatToMat4x4f(global_bindpose_inv_matrix);
				FbxAMatrix local_matrix = cluster->GetLink()->EvaluateLocalTransform();
				u32 indices_count = cluster->GetControlPointIndicesCount();
				for (int k = 0; k < indices_count; k++)
				{
					u32 control_point_index = cluster->GetControlPointIndices()[k];
					if (control_point_weight_infos.find(control_point_index) == control_point_weight_infos.end())
						control_point_weight_infos.insert(std::make_pair(control_point_index, Vector<std::pair<u16, float>>()));
					control_point_weight_infos[control_point_index].push_back(std::make_pair(joint_index, cluster->GetControlPointWeights()[k]));
					//control_point_weight_infos[control_point_index][j] = std::make_pair(it->second._self, cluster->GetControlPointWeights()[k]);
				}
				//only support one stack
				FbxAnimStack* cur_anim_stack = _p_cur_fbx_scene->GetSrcObject<FbxAnimStack>(0);
				FbxString anim_fname = cur_anim_stack->GetName();
				String anim_name = anim_fname.Buffer();
				FbxTakeInfo* take_info = _p_cur_fbx_scene->GetTakeInfo(anim_fname);
				FbxTime start_time = take_info->mLocalTimeSpan.GetStart();
				FbxTime end_time = take_info->mLocalTimeSpan.GetStop();
				FbxTime Duration = take_info->mLocalTimeSpan.GetDuration();
				FbxTime::EMode TimeMode = FbxTime::GetGlobalTimeMode();
				FbxLongLong FrameCount = Duration.GetFrameCount(TimeMode);
				double FrameRate = FbxTime::GetFrameRate(TimeMode);
				_cur_skeleton._joints[joint_index]._frame_count = static_cast<u16>(FrameCount);
				for (FbxLongLong i = 0; i < FrameCount; i++)
				{
					FbxTime cur_time;
					cur_time.SetFrame(i, TimeMode);
					FbxAMatrix cur_transform_offset = node->EvaluateGlobalTransform(cur_time) * geometry_transform;
					_cur_skeleton._joints[joint_index]._pose.emplace_back(FbxMatToMat4x4f(cur_transform_offset.Inverse() * cluster->GetLink()->EvaluateGlobalTransform(cur_time)));
					//_cur_skeleton._joints[joint_index]._pose.emplace_back(FbxMatToMat4x4fcur_transform_offset);
				}
			}
		}
		fbxsdk::FbxVector4* points{ fbx_mesh->GetControlPoints() };
		uint32_t cur_index_count = 0u;
		std::vector<Vector3f> positions{};
		Vector<Vector4f> bone_weights_vec{};
		Vector<Vector4D<u32>> bone_indices_vec{};
		if (control_point_weight_infos.size() > 0)
		{
			for (int32_t i = 0; i < fbx_mesh->GetPolygonCount(); ++i)
			{
				for (int32_t j = 0; j < 3; ++j)
				{
					auto cur_control_point_index = fbx_mesh->GetPolygonVertex(i, j);
					auto p = points[cur_control_point_index];
					Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
					//position *= scale_factor;
					positions.emplace_back(position);
					auto& cur_weight_info = control_point_weight_infos[cur_control_point_index];
					u16 bone_effect_num = cur_weight_info.size() > 4 ? 4 : cur_weight_info.size();
					Vector4f cur_weight{ 0,0,0,0 };
					Vector4D<u32> cur_indices{ 0,0,0,0 };
					for (int weight_index = 0; weight_index < bone_effect_num; ++weight_index)
					{
						cur_weight[weight_index] = cur_weight_info[weight_index].second;
					}
					bone_weights_vec.emplace_back(cur_weight);
					bone_indices_vec.emplace_back(cur_indices);
				}
			}
		}
		else
		{
			for (int32_t i = 0; i < fbx_mesh->GetPolygonCount(); ++i)
			{
				for (int32_t j = 0; j < 3; ++j)
				{
					auto p = points[fbx_mesh->GetPolygonVertex(i, j)];
					Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
					position *= scale_factor;
					positions.emplace_back(position);
				}
			}
		}

		uint32_t vertex_count = (uint32_t)positions.size();
		float* vertex_buf = new float[vertex_count * 3];
		memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
		mesh->_vertex_count = vertex_count;
		mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));
		if (control_point_weight_infos.size() > 0)
		{
			float* bone_weights = new float[vertex_count * 4];
			u32* bone_indices = new u32[vertex_count * 4];
			memcpy(bone_weights, bone_weights_vec.data(), sizeof(Vector4f) * vertex_count);
			memcpy(bone_indices, bone_indices_vec.data(), sizeof(Vector4D<u32>) * vertex_count);
			auto sk_mesh = dynamic_cast<SkinedMesh*>(mesh);
			sk_mesh->SetBoneIndices(reinterpret_cast<Vector4D<u32>*>(bone_indices));
			sk_mesh->SetBoneWeights(reinterpret_cast<Vector4D<float>*>(bone_weights));
		}
		return true;
	}

	bool FbxParser::ReadUVs(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh)
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
			mesh->SetUVs(std::move(reinterpret_cast<Vector2f*>(data)), i);
		}
		return true;
	}

	bool FbxParser::ReadTangent(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh)
	{
		uint32_t tangent_num = fbx_mesh.GetElementTangentCount();
		AL_ASSERT(tangent_num < 0, "ReadTangent");
		auto* tangents = fbx_mesh.GetElementTangent();
		if (tangents == nullptr)
		{
			LOG_WARNING("FbxMesh: {} don't contain tangent info!", fbx_mesh.GetName());
			return true;
		}
		uint32_t vertex_count = fbx_mesh.GetControlPointsCount(), data_size = 0;
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
			for (uint32_t vertex_id = 0; vertex_id < vertex_count; ++vertex_id)
			{
				uint32_t tangent_id = 0;
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

	bool FbxParser::CalculateTangant(Mesh* mesh)
	{
		uint32_t* indices = mesh->GetIndices();
		Vector3f* pos = mesh->GetVertices();
		Vector2f* uv0 = mesh->GetUVs(0);
		Vector4f* tangents = new Vector4f[mesh->_vertex_count];

		auto vertexCount = mesh->_vertex_count;
		Vector3f* tan1 = new Vector3f[vertexCount * 2];
		Vector3f* tan2 = tan1 + vertexCount;
		ZeroMemory(tan1, vertexCount * sizeof(Vector3f) * 2);
		for (size_t i = 0; i < mesh->_index_count; i += 3)
		{
			uint32_t i1 = indices[i], i2 = indices[i + 1], i3 = indices[i + 2];
			const Vector3f& pos1 = pos[i1];
			const Vector3f& pos2 = pos[i2];
			const Vector3f& pos3 = pos[i3];
			const Vector2f& uv1 = uv0[i1];
			const Vector2f& uv2 = uv0[i2];
			const Vector2f& uv3 = uv0[i3];
			float x1 = pos2.x - pos1.x;
			float x2 = pos3.x - pos1.x;
			float y1 = pos2.y - pos1.y;
			float y2 = pos3.y - pos1.y;
			float z2 = pos3.z - pos1.z;
			float z1 = pos2.z - pos1.z;
			float s1 = uv2.x - uv1.x;
			float s2 = uv3.x - uv1.x;
			float t1 = uv2.y - uv1.y;
			float t2 = uv3.y - uv1.y;
			float r = 1.0f / (s1 * t2 - s2 * t1);
			Vector3f sdir{ (t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,(t2 * z1 - t1 * z2) * r };
			Vector3f tdir{ (s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,(s1 * z2 - s2 * z1) * r };
			tan1[i1] += sdir;
			tan1[i2] += sdir;
			tan1[i3] += sdir;
			tan2[i1] += tdir;
			tan2[i2] += tdir;
			tan2[i3] += tdir;
		}
		Vector3f* normal = mesh->GetNormals();
		for (size_t i = 0; i < mesh->_vertex_count; i++)
		{
			const Vector3f& n = normal[i];
			const Vector3f& t = tan1[i];
			tangents[i].xyz = Normalize(t - n * DotProduct(n, t));
			tangents[i].w = (DotProduct(CrossProduct(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
		}
		mesh->SetTangents(std::move(tangents));
		delete[] tan1;
		return true;
	}

	//void FbxParser::GenerateIndexdMesh(Ref<Mesh>& mesh)
	//{
	//	uint32_t vertex_count = mesh->_vertex_count;
	//	std::unordered_map<Vector3f, uint32_t,ALHash::Vector3fHash,ALHash::Vector3Equal> normal_map{};
	//	std::unordered_map<Vector2f, uint32_t,ALHash::Vector2fHash,ALHash::Vector2Equal> uv_map{};
	//	std::unordered_map<uint64_t, uint32_t> vertex_map{};
	//	std::vector<Vector3f> normals{};
	//	std::vector<Vector3f> positions{};
	//	std::vector<Vector2f> uv0s{};
	//	std::vector<uint32_t> indices{};
	//	uint32_t cur_index_count = 0u;
	//	auto raw_normals = mesh->GetNormals();
	//	auto raw_uv0 = mesh->GetUVs(0u);
	//	auto raw_pos = mesh->GetVertices();
	//	ALHash::Vector3fHash v3hash{};
	//	ALHash::Vector2fHash v2hash{};
	//	TimeMgr mgr;
	//	mgr.Mark();
	//	constexpr float minf = std::numeric_limits<float>::lowest();
	//	constexpr float maxf = std::numeric_limits<float>::max();
	//	Vector3f vmin{maxf,maxf ,maxf };
	//	Vector3f vmax{ minf,minf ,minf };
	//	for (size_t i = 0; i < vertex_count; i++)
	//	{
	//		auto p = raw_pos[i], n = raw_normals[i];
	//		auto uv = raw_uv0[i];
	//		auto hash0 = v3hash(n), hash1 = v2hash(uv);
	//		auto vertex_hash = ALHash::CombineHashes(hash0, hash1);
	//		auto it = vertex_map.find(vertex_hash);
	//		if (it == vertex_map.end())
	//		{
	//			vertex_map[vertex_hash] = cur_index_count;
	//			indices.emplace_back(cur_index_count++);
	//			normals.emplace_back(n);
	//			positions.emplace_back(p);
	//			uv0s.emplace_back(uv);
	//			vmin = Min(vmin, p);
	//			vmax = Max(vmax, p);
	//		}
	//		else indices.emplace_back(it->second);
	//	}
	//	LOG_INFO("indices gen takes {}ms", mgr.GetElapsedSinceLastMark());
	//	mesh->Clear();
	//	float aabb_space = 1.0f;
	//	mesh->_bound_box._max = vmax + Vector3f::kOne * aabb_space;
	//	mesh->_bound_box._min = vmin - Vector3f::kOne * aabb_space;
	//	vertex_count = (uint32_t)positions.size();
	//	auto index_count = indices.size();
	//	mesh->_vertex_count = vertex_count;
	//	mesh->_index_count = (uint32_t)index_count;


	//	float* vertex_buf = new float[vertex_count * 3];
	//	float* normal_buf = new float[vertex_count * 3];
	//	float* uv0_buf = new float[vertex_count * 2];
	//	uint32_t* index_buf = new uint32_t[index_count];
	//	memcpy(uv0_buf, uv0s.data(), sizeof(Vector2f) * vertex_count);
	//	memcpy(normal_buf, normals.data(), sizeof(Vector3f) * vertex_count);
	//	memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
	//	memcpy(index_buf, indices.data(), sizeof(uint32_t) * index_count);

	//	mesh->SetUVs(std::move(reinterpret_cast<Vector2f*>(uv0_buf)), 0u);
	//	mesh->SetNormals(std::move(reinterpret_cast<Vector3f*>(normal_buf)));
	//	mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));
	//	mesh->SetIndices(std::move(reinterpret_cast<uint32_t*>(index_buf)));
	//}

	void FbxParser::GenerateIndexdMesh(Mesh* mesh)
	{
		if (!mesh)
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), "mesh is null");
			return;
		}
		SkinedMesh* skined_mesh = dynamic_cast<SkinedMesh*>(mesh);

		uint32_t vertex_count = mesh->_vertex_count;
		std::unordered_map<uint64_t, uint32_t> vertex_map{};
		std::vector<Vector3f> normals{};
		std::vector<Vector3f> positions{};
		std::vector<Vector2f> uv0s{};
		std::vector<uint32_t> indices{};

		uint32_t cur_index_count = 0u;
		auto raw_normals = mesh->GetNormals();
		auto raw_uv0 = mesh->GetUVs(0u);
		auto raw_pos = mesh->GetVertices();
		//for skined mesh
		std::vector<Vector4D<u32>> bone_indices{};
		std::vector<Vector4f> bone_weights{};
		auto raw_bonei = skined_mesh ? skined_mesh->GetBoneIndices() : nullptr;
		auto raw_bonew = skined_mesh ? skined_mesh->GetBoneWeights() : nullptr;

		ALHash::Vector3fHash v3hash{};
		ALHash::Vector2fHash v2hash{};
		ALHash::VectorHash<Vector4D, float> v4fhash{};
		ALHash::VectorHash<Vector4D, u32> u4fhash{};
		TimeMgr mgr;
		mgr.Mark();
		constexpr float minf = std::numeric_limits<float>::lowest();
		constexpr float maxf = std::numeric_limits<float>::max();
		Vector3f vmin{ maxf,maxf ,maxf };
		Vector3f vmax{ minf,minf ,minf };
		if (raw_bonei)
		{
			for (size_t i = 0; i < vertex_count; i++)
			{
				auto p = raw_pos[i], n = raw_normals[i];
				auto uv = raw_uv0[i];
				auto hash0 = v3hash(n), hash1 = v2hash(uv);
				auto vertex_hash = ALHash::CombineHashes(hash0, hash1);
				auto bi = raw_bonei[i];
				auto bw = raw_bonew[i];
				auto it = vertex_map.find(vertex_hash);
				if (it == vertex_map.end())
				{
					vertex_map[vertex_hash] = cur_index_count;
					indices.emplace_back(cur_index_count++);
					normals.emplace_back(n);
					positions.emplace_back(p);
					uv0s.emplace_back(uv);
					bone_indices.emplace_back(bi);
					bone_weights.emplace_back(bw);
					vmin = Min(vmin, p);
					vmax = Max(vmax, p);
				}
				else indices.emplace_back(it->second);
			}
		}
		else
		{
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
					vmin = Min(vmin, p);
					vmax = Max(vmax, p);
				}
				else indices.emplace_back(it->second);
			}
		}

		LOG_INFO("indices gen takes {}ms", mgr.GetElapsedSinceLastMark());
		mesh->Clear();
		float aabb_space = 1.0f;
		mesh->_bound_box._max = vmax + Vector3f::kOne * aabb_space;
		mesh->_bound_box._min = vmin - Vector3f::kOne * aabb_space;
		vertex_count = (uint32_t)positions.size();
		auto index_count = indices.size();
		mesh->_vertex_count = vertex_count;
		mesh->_index_count = (uint32_t)index_count;
		float* vertex_buf = new float[vertex_count * 3];
		float* normal_buf = new float[vertex_count * 3];
		float* uv0_buf = new float[vertex_count * 2];
		uint32_t* index_buf = new uint32_t[index_count];
		memcpy(uv0_buf, uv0s.data(), sizeof(Vector2f) * vertex_count);
		memcpy(normal_buf, normals.data(), sizeof(Vector3f) * vertex_count);
		memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
		memcpy(index_buf, indices.data(), sizeof(uint32_t) * index_count);
		mesh->SetUVs(std::move(reinterpret_cast<Vector2f*>(uv0_buf)), 0u);
		mesh->SetNormals(std::move(reinterpret_cast<Vector3f*>(normal_buf)));
		mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));
		mesh->SetIndices(std::move(reinterpret_cast<uint32_t*>(index_buf)));
		if (raw_bonei)
		{
			u32* bone_indices_buf = new u32[vertex_count * 4];
			float* bone_weights_buf = new float[vertex_count * 4];
			memcpy(bone_indices_buf, bone_indices.data(), sizeof(Vector4D<u32>) * vertex_count);
			memcpy(bone_weights_buf, bone_weights.data(), sizeof(Vector4f) * vertex_count);
			skined_mesh->SetBoneIndices(std::move(reinterpret_cast<Vector4D<u32>*>(bone_indices_buf)));
			skined_mesh->SetBoneWeights(std::move(reinterpret_cast<Vector4f*>(bone_weights_buf)));
		}
	}

	List<Ref<Mesh>> FbxParser::ParserImpl(WString sys_path)
	{
		scale_factor = Vector3f::kOne;
		String path = ToChar(sys_path.data());
		space_str = "--";
		_p_cur_fbx_scene = FbxScene::Create(fbx_manager_, "RootScene");
		List<Ref<Mesh>> loaded_meshs{};
		if (fbx_importer_ != nullptr && !fbx_importer_->Initialize(path.c_str(), -1, fbx_manager_->GetIOSettings()))
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), "Load mesh failed at path {}", path);
			return loaded_meshs;
		}
		fbx_importer_->Import(_p_cur_fbx_scene);
		FbxNode* fbx_rt = _p_cur_fbx_scene->GetRootNode();
		fbxsdk::FbxAxisSystem::DirectX.DeepConvertScene(_p_cur_fbx_scene);
		if (_p_cur_fbx_scene->GetGlobalSettings().GetSystemUnit() != fbxsdk::FbxSystemUnit::cm)
		{
			const FbxSystemUnit::ConversionOptions lConversionOptions = {
			false, /* mConvertRrsNodes */
			true, /* mConvertAllLimits */
			true, /* mConvertClusters */
			true, /* mConvertLightIntensity */
			true, /* mConvertPhotometricLProperties */
			true  /* mConvertCameraClipPlanes */
			};
			fbxsdk::FbxSystemUnit::cm.ConvertScene(_p_cur_fbx_scene, lConversionOptions);
		}
		//ParserFbxNode(fbx_rt, loaded_meshs);
		Queue<FbxNode*> mesh_node, skeleton_node;
		ParserFbxNode(fbx_rt, mesh_node, skeleton_node);
		_cur_skeleton.joint_num = 0;
		_cur_skeleton._joints.clear();//当前仅支持一个骨骼，所以清空之前的数据
		while (!skeleton_node.empty())
		{
			ParserSkeleton(skeleton_node.front(), _cur_skeleton);
			skeleton_node.pop();
		}
		while (!mesh_node.empty())
		{
			ParserMesh(mesh_node.front(), loaded_meshs);
			mesh_node.pop();
		}
		if (loaded_meshs.empty())
		{
			g_pLogMgr->LogErrorFormat(L"Load fbx from path {} failed!", sys_path);
		}
		for (auto& mesh : loaded_meshs)
			mesh->OriginPath(PathUtils::ExtractAssetPath(path));
		return loaded_meshs;
	}

	FbxAMatrix FbxParser::GetGeometryTransformation(FbxNode* node)
	{
		if (!node)
		{
			throw std::exception("Null for mesh geometry\n\n");
		}
		const FbxVector4 IT = node->GetGeometricTranslation(FbxNode::eSourcePivot);
		const FbxVector4 IR = node->GetGeometricRotation(FbxNode::eSourcePivot);
		const FbxVector4 IS = node->GetGeometricScaling(FbxNode::eSourcePivot);
		return FbxAMatrix(IT, IR, IS);
	}
#pragma warning(pop)
}
