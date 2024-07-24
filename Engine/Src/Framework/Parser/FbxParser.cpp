#include "pch.h"
#include "Framework/Parser/FbxParser.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/Utils.h"
//#include "Animation/Skeleton.h"
#include "Animation/Clip.h"

using fbxsdk::FbxNodeAttribute;
using fbxsdk::FbxCast;
using fbxsdk::FbxNode;

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable: 4244)

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

	static Transform FbxMatToTransform(FbxAMatrix src)
	{
		Transform t;
		FbxVector4 p = src.GetT();
		FbxVector4 s = src.GetS();
		FbxQuaternion q = src.GetQ();
		t._position = Vector3f{ (float)p[0],(float)p[1],(float)p[2] };
		t._rotation = Quaternion{ (float)q[0],(float)q[1],(float)q[2],(float)q[3] };
		t._scale = Vector3f{ (float)s[0],(float)s[1],(float)s[2] };
		return t;
	}

	// Get the matrix of the given pose
	static FbxAMatrix GetPoseMatrix(FbxPose* pPose, int pNodeIndex)
	{
		FbxAMatrix lPoseMatrix;
		FbxMatrix lMatrix = pPose->GetMatrix(pNodeIndex);

		memcpy((double*)lPoseMatrix, (double*)lMatrix, sizeof(lMatrix.mData));

		return lPoseMatrix;
	}

	static FbxAMatrix GetGlobalPosition(FbxNode* pNode, const FbxTime& pTime, FbxPose* pPose = nullptr, FbxAMatrix* pParentGlobalPosition = nullptr)
	{
		FbxAMatrix lGlobalPosition;
		bool        lPositionFound = false;

		if (pPose)
		{
			int lNodeIndex = pPose->Find(pNode);

			if (lNodeIndex > -1)
			{
				// The bind pose is always a global matrix.
				// If we have a rest pose, we need to check if it is
				// stored in global or local space.
				if (pPose->IsBindPose() || !pPose->IsLocalMatrix(lNodeIndex))
				{
					lGlobalPosition = GetPoseMatrix(pPose, lNodeIndex);
				}
				else
				{
					// We have a local matrix, we need to convert it to
					// a global space matrix.
					FbxAMatrix lParentGlobalPosition;

					if (pParentGlobalPosition)
					{
						lParentGlobalPosition = *pParentGlobalPosition;
					}
					else
					{
						if (pNode->GetParent())
						{
							lParentGlobalPosition = GetGlobalPosition(pNode->GetParent(), pTime, pPose);
						}
					}

					FbxAMatrix lLocalPosition = GetPoseMatrix(pPose, lNodeIndex);
					lGlobalPosition = lParentGlobalPosition * lLocalPosition;
				}

				lPositionFound = true;
			}
		}

		if (!lPositionFound)
		{
			// There is no pose entry for that node, get the current global position instead.

			// Ideally this would use parent global position and local position to compute the global position.
			// Unfortunately the equation 
			//    lGlobalPosition = pParentGlobalPosition * lLocalPosition
			// does not hold when inheritance type is other than "Parent" (RSrs).
			// To compute the parent rotation and scaling is tricky in the RrSs and Rrs cases.
			lGlobalPosition = pNode->EvaluateGlobalTransform(pTime);
			//lGlobalPosition = pNode->EvaluateLocalTransform(pTime);
		}

		return lGlobalPosition;
	}

	// Get the geometry offset to a node. It is never inherited by the children.
	static FbxAMatrix GetGeometry(FbxNode* pNode)
	{
		if (pNode)
		{
			const FbxVector4 lT = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
			const FbxVector4 lR = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
			const FbxVector4 lS = pNode->GetGeometricScaling(FbxNode::eSourcePivot);
			return FbxAMatrix(lT, lR, lS);
		}
		else
		{
			FbxAMatrix mat;
			mat.SetIdentity();
			return mat;
		}
	}

	static void GetGeometry(FbxNode* pNode, FbxVector4& t, FbxVector4& r, FbxVector4& s)
	{
		t = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
		r = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
		s = pNode->GetGeometricScaling(FbxNode::eSourcePivot);
	}

	static void ComputeClusterDeformation(bool is_local, FbxAMatrix& pGlobalPosition, FbxNode* pMesh, FbxCluster* pCluster, FbxNode* cluster_link,
		FbxAMatrix& pVertexTransformMatrix, FbxTime pTime, FbxPose* pPose)
	{
		FbxCluster::ELinkMode lClusterMode = pCluster->GetLinkMode();

		FbxAMatrix lReferenceGlobalInitPosition;
		FbxAMatrix lReferenceGlobalCurrentPosition;
		FbxAMatrix lAssociateGlobalInitPosition;
		FbxAMatrix lAssociateGlobalCurrentPosition;
		FbxAMatrix lClusterGlobalInitPosition;
		FbxAMatrix lClusterGlobalCurrentPosition;

		FbxAMatrix lReferenceGeometry;
		FbxAMatrix lAssociateGeometry;
		FbxAMatrix lClusterGeometry;

		FbxAMatrix lClusterRelativeInitPosition;// inv_bind_pose
		FbxAMatrix lClusterRelativeCurrentPositionInverse;

		if (lClusterMode == FbxCluster::eAdditive && pCluster->GetAssociateModel())
		{
			AL_ASSERT(true);
			pCluster->GetTransformAssociateModelMatrix(lAssociateGlobalInitPosition);
			// Geometric transform of the model
			lAssociateGeometry = GetGeometry(pCluster->GetAssociateModel());
			lAssociateGlobalInitPosition *= lAssociateGeometry;
			lAssociateGlobalCurrentPosition = GetGlobalPosition(pCluster->GetAssociateModel(), pTime, pPose);

			pCluster->GetTransformMatrix(lReferenceGlobalInitPosition);
			// Multiply lReferenceGlobalInitPosition by Geometric Transformation
			lReferenceGeometry = GetGeometry(pMesh);
			lReferenceGlobalInitPosition *= lReferenceGeometry;
			lReferenceGlobalCurrentPosition = pGlobalPosition;

			// Get the link initial global position and the link current global position.
			pCluster->GetTransformLinkMatrix(lClusterGlobalInitPosition);
			// Multiply lClusterGlobalInitPosition by Geometric Transformation
			lClusterGeometry = GetGeometry(pCluster->GetLink());
			lClusterGlobalInitPosition *= lClusterGeometry;
			lClusterGlobalCurrentPosition = GetGlobalPosition(pCluster->GetLink(), pTime, pPose);

			// Compute the shift of the link relative to the reference.
			//ModelM-1 * AssoM * AssoGX-1 * LinkGX * LinkM-1*ModelM
			pVertexTransformMatrix = lReferenceGlobalInitPosition.Inverse() * lAssociateGlobalInitPosition * lAssociateGlobalCurrentPosition.Inverse() *
				lClusterGlobalCurrentPosition * lClusterGlobalInitPosition.Inverse() * lReferenceGlobalInitPosition;
		}
		else
		{
			pCluster->GetTransformMatrix(lReferenceGlobalInitPosition);
			lReferenceGlobalCurrentPosition = pGlobalPosition;
			// Multiply lReferenceGlobalInitPosition by Geometric Transformation
			lReferenceGeometry = GetGeometry(pMesh);
			lReferenceGlobalInitPosition *= lReferenceGeometry;

			// Get the link initial global position and the link current global position.
			pCluster->GetTransformLinkMatrix(lClusterGlobalInitPosition);
			//pose 为空的话，直接返回节点的世界变换，传pose似乎会有问题
			if (!is_local)
			{

				//lClusterGlobalCurrentPosition = GetGlobalPosition(pCluster->GetLink(), pTime, pPose);

				FbxAMatrix localMatrix = pCluster->GetLink()->EvaluateLocalTransform(pTime);

				FbxNode* pParentNode = pCluster->GetLink()->GetParent();
				FbxAMatrix parentMatrix = pParentNode->EvaluateLocalTransform(pTime);
				FbxNode* cur_node = pParentNode;
				while ((pParentNode = pParentNode->GetParent()) != NULL)
				{
					parentMatrix = pParentNode->EvaluateLocalTransform(pTime) * parentMatrix;
					cur_node = pParentNode;
				}
				lClusterGlobalCurrentPosition = pCluster->GetLink()->EvaluateGlobalTransform(pTime);
				// Compute the initial position of the link relative to the reference.
				lClusterRelativeInitPosition = lClusterGlobalInitPosition.Inverse() * lReferenceGlobalInitPosition;
				lClusterGlobalCurrentPosition = parentMatrix * localMatrix;
				// Compute the current position of the link relative to the reference.
				//当前节点的逆世界变换 * 当前cluster的世界变换
				lClusterRelativeCurrentPositionInverse = lReferenceGlobalCurrentPosition.Inverse() * lClusterGlobalCurrentPosition;
			}
			else
			{
				//lClusterRelativeCurrentPositionInverse = pCluster->GetLink()->EvaluateLocalTransform(pTime) * lReferenceGeometry;
				lClusterRelativeCurrentPositionInverse = cluster_link->EvaluateLocalTransform(pTime) * lReferenceGeometry;
			}


			// Compute the shift of the link relative to the reference.
			pVertexTransformMatrix = lClusterRelativeCurrentPositionInverse;// *lClusterRelativeInitPosition;
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
	List<Ref<Mesh>> FbxParser::Parser(std::string_view sys_path)
	{
		// Convert string to wstring
		return ParserImpl(ToWChar(sys_path.data()));
	}
	List<Ref<Mesh>> FbxParser::Parser(const WString& sys_path)
	{
		return ParserImpl(sys_path);
	}

	Ailu::FbxParser::~FbxParser()
	{
	}


	void FbxParser::ParserFbxNode(FbxNode* node, Queue<FbxNode*>& mesh_node, Queue<FbxNode*>& skeleton_node)
	{
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
	}

	void FbxParser::ParserSkeleton(FbxNode* node, Skeleton& sk)
	{
		FbxNodeAttribute* attr = node->GetNodeAttribute();
		fbxsdk::FbxAMatrix init_local_transf = node->EvaluateGlobalTransform();
		if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			i32 joint_index = Skeleton::GetJointIndexByName(sk, node->GetName());
			if (joint_index != -1)
				return;
			else
			{
				i32 parent_joint_index;
				if (node->GetParent())
				{
					if (Skeleton::GetJointIndexByName(sk, node->GetParent()->GetName()) == -1)
					{
						ParserSkeleton(node->GetParent(), sk);
					}
				}
				i32 new_joint_index = Skeleton::GetJointIndexByName(sk, node->GetParent()->GetName());
				parent_joint_index = new_joint_index == -1 ? 65535 : new_joint_index;
				Joint joint;
				joint._name = node->GetName();
				joint._parent = parent_joint_index;
				joint._self = sk.JointNum();
				if (parent_joint_index != 65535)
					sk[parent_joint_index]._children.emplace_back(joint._self);
				sk.AddJoint(joint);
			}

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
		const int mat_count = node->GetMaterialCount();
		static auto fill_tex = [&](const FbxProperty& prop)->String
			{
				int layeredTextureCount = prop.GetSrcObjectCount<FbxLayeredTexture>();
				String sys_path;
				if (layeredTextureCount > 0)
				{
					LOG_WARNING("MultiLayer texture not been supported yet! when load mesh {}", node->GetName());
					for (int layerIndex = 0; layerIndex != layeredTextureCount; ++layerIndex)
					{
						FbxLayeredTexture* layeredTexture = prop.GetSrcObject<FbxLayeredTexture>(layerIndex);
						int count = layeredTexture->GetSrcObjectCount<FbxTexture>();
						for (int c = 0; c != count; ++c)
						{
							FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(layeredTexture->GetSrcObject<FbxFileTexture>(c));
							sys_path = fileTexture->GetFileName();
						}
					}
				}
				else
				{
					int textureCount = prop.GetSrcObjectCount<FbxTexture>();
					for (int i = 0; i != textureCount; ++i)
					{
						FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(prop.GetSrcObject<FbxFileTexture>(i));
						sys_path = fileTexture->GetFileName();
					}
				}
				return sys_path;
			};
		for (int i = 0; i < mat_count; ++i)
		{
			FbxSurfaceMaterial* mat = node->GetMaterial(i);
			if (mat)
			{
				ImportedMaterialInfo mat_info(i, mat->GetName());
				mat_info._textures[0] = fill_tex(mat->FindProperty(FbxSurfaceMaterial::sDiffuse));
				mat_info._textures[1] = fill_tex(mat->FindProperty(FbxSurfaceMaterial::sNormalMap));
				_imported_fbx_materials.emplace_back(mat_info);
			}
		}
		if (use_multi_thread)
		{
			//_time_mgr.Mark();
			auto ret4 = g_pThreadTool->Enqueue(&FbxParser::ParserAnimation, this, node, _cur_skeleton);
			auto ret1 = g_pThreadTool->Enqueue(&FbxParser::ReadVertex, this, node, mesh.get());
			auto ret2 = g_pThreadTool->Enqueue(&FbxParser::ReadNormal, this, std::ref(*fbx_mesh), mesh.get());
			auto ret3 = g_pThreadTool->Enqueue(&FbxParser::ReadUVs, this, std::ref(*fbx_mesh), mesh.get());
			if (ret1.get() && ret2.get() && ret3.get() && ret4.get())
			{
				//LOG_INFO("Read mesh data takes {}ms", _time_mgr.GetElapsedSinceLastMark());
				//_time_mgr.Mark();
				GenerateIndexdMesh(mesh.get());
				CalculateTangant(mesh.get());
				//LOG_INFO("Generate indices and tangent data takes {}ms", _time_mgr.GetElapsedSinceLastMark());
				//mesh->BuildRHIResource();
				if (is_skined)
				{
					dynamic_cast<SkinedMesh*>(mesh.get())->CurSkeleton(_cur_skeleton);
				}
				loaded_meshes.emplace_back(mesh);
				return true;
			}
			return false;
		}
		else
		{
			//_time_mgr.Mark();
			ReadVertex(node, mesh.get());
			ReadNormal(*fbx_mesh, mesh.get());
			ReadUVs(*fbx_mesh, mesh.get());
			ParserAnimation(node, _cur_skeleton);
			//LOG_INFO("Read mesh data takes {}ms", _time_mgr.GetElapsedSinceLastMark());
			//_time_mgr.Mark();
			GenerateIndexdMesh(mesh.get());
			CalculateTangant(mesh.get());
			//LOG_INFO("Generate indices and tangent data takes {}ms", _time_mgr.GetElapsedSinceLastMark());
			//mesh->Build();
			if (is_skined)
			{
				dynamic_cast<SkinedMesh*>(mesh.get())->CurSkeleton(_cur_skeleton);
			}
			loaded_meshes.emplace_back(mesh);
			return true;
			return false;
		}
	}

	bool FbxParser::ParserAnimation(FbxNode* node, Skeleton& sk)
	{
		auto fbx_mesh = node->GetMesh();
		auto mesh_name = fbx_mesh->GetName();
		u32 deformers_num = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
		bool b_use_mt = false;
		static auto parser_anim = [](Skeleton& sk, FbxNode* node, FbxMesh* mesh, FbxCluster* cluster, FbxNode* cluster_link, const FbxAMatrix& geometry_transform, AnimationClip* clip,
			FbxLongLong frame_count, FbxTime::EMode time_mode) {
				String joint_name = cluster->GetLink()->GetName();
				i32 joint_i = Skeleton::GetJointIndexByName(sk, joint_name);
				if (joint_i == -1)
					return;
				FbxAMatrix transform_matrix, transform_link_matrix, global_bindpose_inv_matrix;
				cluster->GetTransformMatrix(transform_matrix);//transform of mesh at binding time
				cluster->GetTransformLinkMatrix(transform_link_matrix);//transform of joint at binding time from joint space -> world_space
				global_bindpose_inv_matrix = transform_link_matrix.Inverse() * transform_matrix * geometry_transform;
				u16 joint_index = (u16)joint_i;
				sk[joint_index]._inv_bind_pos = FbxMatToMat4x4f(global_bindpose_inv_matrix);
				auto joint = cluster_link;
				for (FbxLongLong i = 0; i < frame_count; i++)
				{
					FbxTime cur_time;
					cur_time.SetFrame(i, time_mode);
					FbxAMatrix global_offpositon = node->EvaluateGlobalTransform(cur_time) * geometry_transform;
					FbxAMatrix bone_matrix_l;
					ComputeClusterDeformation(true, global_offpositon, node, cluster, cluster_link, bone_matrix_l, cur_time, nullptr);
					sk[joint_index]._node_inv_world_mat = FbxMatToMat4x4f(global_offpositon.Inverse());
					if (!joint->GetParent()->GetSkeleton())
					{
						FbxNode* parent_node = joint->GetParent();
						FbxAMatrix none_joint_matrix = parent_node->EvaluateLocalTransform(cur_time);
						while ((parent_node = parent_node->GetParent()) != NULL)
						{
							none_joint_matrix = parent_node->EvaluateLocalTransform(cur_time) * none_joint_matrix;
						}
						bone_matrix_l = none_joint_matrix * bone_matrix_l;
					}
					clip->AddKeyFrame(joint_index, FbxMatToTransform(bone_matrix_l));
				}
			};
		g_pTimeMgr->Mark();
		FbxAnimStack* cur_anim_stack = _p_cur_fbx_scene->GetSrcObject<FbxAnimStack>(0);
		if (deformers_num > 0 && cur_anim_stack)
		{
			FbxVector4 t, r, s;
			GetGeometry(node, t, r, s);
			FbxAMatrix geometry_transform;
			geometry_transform.SetTRS(t, r, s);
			//only support one stack
			//LOG_INFO("Scene {} has {} animation stack", _p_cur_fbx_scene->GetName(), _p_cur_fbx_scene->GetSrcObjectCount<FbxAnimStack>());
			FbxString anim_fname = cur_anim_stack->GetName();
			String anim_name = anim_fname.Buffer();
			FbxTakeInfo* take_info = _p_cur_fbx_scene->GetTakeInfo(anim_fname);
			FbxTime start_time = take_info->mLocalTimeSpan.GetStart();
			FbxTime end_time = take_info->mLocalTimeSpan.GetStop();
			FbxTime Duration = take_info->mLocalTimeSpan.GetDuration();
			FbxTime::EMode TimeMode = FbxTime::GetGlobalTimeMode();
			FbxLongLong FrameCount = Duration.GetFrameCount(TimeMode);
			double FrameRate = FbxTime::GetFrameRate(TimeMode);
			AnimationClip* clip = new AnimationClip(_cur_skeleton, FrameCount, static_cast<float>(Duration.GetSecondDouble()));
			clip->Name(node->GetName());
			if (b_use_mt)
			{
				Vector<std::future<void>> res{};
				for (u32 i = 0; i < deformers_num; i++)
				{
					FbxSkin* skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(i, FbxDeformer::eSkin));
					if (!skin)
					{
						g_pLogMgr->LogWarningFormat("only support Skin deformer!");
						continue;
					}
					u32 cluster_num = skin->GetClusterCount();
					for (u32 j = 0; j < cluster_num; j++)
					{
						res.emplace_back(g_pThreadTool->Enqueue(parser_anim, std::ref(sk), node, fbx_mesh, skin->GetCluster(j), skin->GetCluster(j)->GetLink(), std::ref(geometry_transform), clip, FrameCount, TimeMode));
					}
				}
				for (auto& ret : res)
					ret.wait();
			}
			else
			{
				for (u32 i = 0; i < deformers_num; i++)
				{
					FbxSkin* skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(i, FbxDeformer::eSkin));
					if (!skin)
						continue;
					u32 cluster_num = skin->GetClusterCount();
					for (u32 j = 0; j < cluster_num; j++)
					{
						parser_anim(sk, node, fbx_mesh, skin->GetCluster(j), skin->GetCluster(j)->GetLink(), geometry_transform, clip, FrameCount, TimeMode);
					}
				}
			}
			//LOG_INFO("Fill animation clip cost {} ms", g_pTimeMgr->GetElapsedSinceLastMark());
			clip->CurSkeletion(sk);
			g_pTimeMgr->Mark();
			clip->Bake();
			LOG_INFO("Bake animation clip cost {} ms", g_pTimeMgr->GetElapsedSinceLastMark());
			LOG_INFO("Import animation {} end", clip->Name())
				_loaded_anims.emplace_back(clip);
		}
		return true;
	}

	bool FbxParser::ReadNormal(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh)
	{
		if (fbx_mesh.GetElementNormalCount() < 1) return false;
		auto* normals = fbx_mesh.GetElementNormal(0);
		int vertex_count = fbx_mesh.GetControlPointsCount(), data_size = 0;
		void* data = nullptr;
		if (normals->GetMappingMode() == fbxsdk::FbxLayerElement::EMappingMode::eByControlPoint)
		{
			_b_normal_by_controlpoint = true;
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
				//reinterpret_cast<float*>(data)[i * 3 + 2] = normal[2];
			}
		}
		else if (normals->GetMappingMode() == fbxsdk::FbxLayerElement::EMappingMode::eByPolygonVertex)
		{
			_b_normal_by_controlpoint = false;
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
					//reinterpret_cast<float*>(data)[cur_vertex_id * 3 + 2] = normal[2];
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
		std::map<u32, Vector<std::pair<u16, float>>> control_point_weight_infos{};
		if (deformers_num > 0)
		{
			for (u32 i = 0; i < deformers_num; i++)
			{
				FbxSkin* skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(i, FbxDeformer::eSkin));
				if (!skin)
					continue;
				u32 cluster_num = skin->GetClusterCount();
				for (u32 j = 0; j < cluster_num; j++)
				{
					FbxCluster* cluster = skin->GetCluster(j);
					//auto skin_type = skin->GetSkinningType();
					String joint_name = cluster->GetLink()->GetName();
					i32 joint_i = Skeleton::GetJointIndexByName(_cur_skeleton, joint_name);
					if (joint_i == -1)
					{
						LOG_WARNING("Can't find joint {} when load mesh {}", joint_name, mesh_name);
						continue;
					}
					u16 joint_index = (u16)joint_i;
					u32 indices_count = cluster->GetControlPointIndicesCount();
					for (u32 k = 0; k < indices_count; k++)
					{
						u32 control_point_index = cluster->GetControlPointIndices()[k];
						if (control_point_weight_infos.find(control_point_index) == control_point_weight_infos.end())
							control_point_weight_infos.insert(std::make_pair(control_point_index, Vector<std::pair<u16, float>>()));
						control_point_weight_infos[control_point_index].push_back(std::make_pair(joint_index, cluster->GetControlPointWeights()[k]));
					}
				}
			}
		}
		_positon_conrtol_index_mapper.clear();
		_positon_conrtol_index_mapper.resize(fbx_mesh->GetPolygonCount() * 3);
		_positon_material_index_mapper.clear();
		_positon_material_index_mapper.resize(fbx_mesh->GetPolygonCount() * 3);
		fbxsdk::FbxVector4* points{ fbx_mesh->GetControlPoints() };
		u32 cur_index_count = 0u;
		std::vector<Vector3f> positions{};
		Vector<Vector4f> bone_weights_vec{};
		Vector<Vector4D<u32>> bone_indices_vec{};
		i32 trangle_count = fbx_mesh->GetPolygonCount();
		auto mat_element = fbx_mesh->GetElementMaterial();
		if (mat_element)
		{
			auto material_indices = mat_element->GetIndexArray();
			if (control_point_weight_infos.size() > 0)
			{
				for (int32_t i = 0; i < fbx_mesh->GetPolygonCount(); ++i)
				{
					auto cur_mat_index = material_indices.GetAt(i);
					for (int32_t j = 0; j < 3; ++j)
					{
						auto cur_control_point_index = fbx_mesh->GetPolygonVertex(i, j);
						auto p = points[cur_control_point_index];
						Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
						//position *= scale_factor;
						_positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
						_positon_material_index_mapper[positions.size()] = cur_mat_index;
						positions.emplace_back(position);
						auto& cur_weight_info = control_point_weight_infos[cur_control_point_index];
						if (cur_weight_info.size() > 4)
						{
							std::sort(cur_weight_info.begin(), cur_weight_info.end(), [](const std::pair<u16, float>& a, const std::pair<u16, float>& b) {
								return a.second > b.second;
								});
						}
						//AL_ASSERT(cur_weight_info.size() > 4 , "weight must equal one");
						auto bone_effect_num = cur_weight_info.size() > 4 ? 4 : cur_weight_info.size();
						Vector4f cur_weight{ 0,0,0,0 };
						Vector4D<u32> cur_indices{ 0,0,0,0 };
						for (int weight_index = 0; weight_index < bone_effect_num; ++weight_index)
						{
							cur_weight[weight_index] = cur_weight_info[weight_index].second;
							cur_indices[weight_index] = cur_weight_info[weight_index].first;
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
					auto cur_mat_index = material_indices.GetAt(i);
					for (int32_t j = 0; j < 3; ++j)
					{
						auto cur_control_point_index = fbx_mesh->GetPolygonVertex(i, j);
						auto p = points[cur_control_point_index];
						Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
						_positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
						_positon_material_index_mapper[positions.size()] = cur_mat_index;
						position *= scale_factor;
						positions.emplace_back(position);
					}
				}
			}
		}
		else
		{
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
						_positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
						_positon_material_index_mapper[positions.size()] = 0;
						positions.emplace_back(position);
						auto& cur_weight_info = control_point_weight_infos[cur_control_point_index];
						if (cur_weight_info.size() > 4)
						{
							std::sort(cur_weight_info.begin(), cur_weight_info.end(), [](const std::pair<u16, float>& a, const std::pair<u16, float>& b) {
								return a.second > b.second;
								});
						}
						//AL_ASSERT(cur_weight_info.size() > 4 , "weight must equal one");
						auto bone_effect_num = cur_weight_info.size() > 4 ? 4 : cur_weight_info.size();
						Vector4f cur_weight{ 0,0,0,0 };
						Vector4D<u32> cur_indices{ 0,0,0,0 };
						for (int weight_index = 0; weight_index < bone_effect_num; ++weight_index)
						{
							cur_weight[weight_index] = cur_weight_info[weight_index].second;
							cur_indices[weight_index] = cur_weight_info[weight_index].first;
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
						auto cur_control_point_index = fbx_mesh->GetPolygonVertex(i, j);
						auto p = points[cur_control_point_index];
						Vector3f position{ (float)p[0],(float)p[1],(float)p[2] };
						_positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
						_positon_material_index_mapper[positions.size()] = 0;
						position *= scale_factor;
						positions.emplace_back(position);
					}
				}
			}
		}
		u32 vertex_count = (u32)positions.size();
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
				throw std::runtime_error("Mesh uv by eByControlPoint");
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
		u32 tangent_num = fbx_mesh.GetElementTangentCount();
		AL_ASSERT_MSG(tangent_num < 0, "ReadTangent");
		auto* tangents = fbx_mesh.GetElementTangent();
		if (tangents == nullptr)
		{
			LOG_WARNING("FbxMesh: {} don't contain tangent info!", fbx_mesh.GetName());
			return true;
		}
		u32 vertex_count = fbx_mesh.GetControlPointsCount(), data_size = 0;
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
			for (u32 vertex_id = 0; vertex_id < vertex_count; ++vertex_id)
			{
				u32 tangent_id = 0;
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

		Vector3f* pos = mesh->GetVertices();
		Vector2f* uv0 = mesh->GetUVs(0);
		Vector4f* tangents = new Vector4f[mesh->_vertex_count];
		Vector4f* bitangents = new Vector4f[mesh->_vertex_count];
		for (u16 i = 0; i < mesh->SubmeshCount(); i++)
		{
			u32* indices = mesh->GetIndices(i);
			for (size_t j = 0; j < mesh->GetIndicesCount(i); j += 3)
			{
				Vector4f tangent, bitangent;
				u32 index0 = indices[j], index1 = indices[j + 1], index2 = indices[j + 2];
				Vector3f& v0 = pos[index0], v1 = pos[index1], v2 = pos[index2];
				Vector2f& t0 = uv0[index0], t1 = uv0[index1], t2 = uv0[index2];
				Vector3f edge1 = v1 - v0;
				Vector3f edge2 = v2 - v0;
				Vector2f deltaUV1 = t1 - t0;
				Vector2f deltaUV2 = t2 - t0;
				float f = 1.0f / ((deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y) + 0.0000001f);
				tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
				tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
				tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
				bitangent.x = f * (deltaUV1.x * edge2.x - deltaUV2.x * edge1.x);
				bitangent.y = f * (deltaUV1.x * edge2.y - deltaUV2.x * edge1.y);
				bitangent.z = f * (deltaUV1.x * edge2.z - deltaUV2.x * edge1.z);
				tangents[index0] = tangent;
				tangents[index1] = tangent;
				tangents[index2] = tangent;
				bitangents[index0] = bitangent;
				bitangents[index1] = bitangent;
				bitangents[index2] = bitangent;
			}
		}

		Vector3f* normal = mesh->GetNormals();
		for (size_t i = 0; i < mesh->_vertex_count; i++)
		{
			const Vector3f& n = normal[i];
			const Vector3f& t = tangents[i].xyz;
			const Vector3f& b = bitangents[i].xyz;
			tangents[i].xyz = Normalize(t - n * DotProduct(n, t));
			tangents[i].w = (DotProduct(CrossProduct(n, t), b) < 0.0f) ? -1.0f : 1.0f;
		}
		delete[] bitangents;
		//auto vertexCount = mesh->_vertex_count;
		//Vector3f* tan1 = new Vector3f[vertexCount * 2];
		//Vector3f* tan2 = tan1 + vertexCount;
		//ZeroMemory(tan1, vertexCount * sizeof(Vector3f) * 2);
		//for (size_t i = 0; i < mesh->GetIndicesCount(); i += 3)
		//{
		//	u32 i1 = indices[i], i2 = indices[i + 1], i3 = indices[i + 2];
		//	const Vector3f& pos1 = pos[i1];
		//	const Vector3f& pos2 = pos[i2];
		//	const Vector3f& pos3 = pos[i3];
		//	const Vector2f& uv1 = uv0[i1];
		//	const Vector2f& uv2 = uv0[i2];
		//	const Vector2f& uv3 = uv0[i3];
		//	float x1 = pos2.x - pos1.x;
		//	float x2 = pos3.x - pos1.x;
		//	float y1 = pos2.y - pos1.y;
		//	float y2 = pos3.y - pos1.y;
		//	float z2 = pos3.z - pos1.z;
		//	float z1 = pos2.z - pos1.z;
		//	float s1 = uv2.x - uv1.x;
		//	float s2 = uv3.x - uv1.x;
		//	float t1 = uv2.y - uv1.y;
		//	float t2 = uv3.y - uv1.y;
		//	float r = 1.0f / (s1 * t2 - s2 * t1);
		//	Vector3f sdir{ (t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,(t2 * z1 - t1 * z2) * r };
		//	Vector3f tdir{ (s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,(s1 * z2 - s2 * z1) * r };
		//	tan1[i1] += sdir;
		//	tan1[i2] += sdir;
		//	tan1[i3] += sdir;
		//	tan2[i1] += tdir;
		//	tan2[i2] += tdir;
		//	tan2[i3] += tdir;
		//	LOG_INFO("({},{},{})", tan1[i].x, tan1[i].y, tan1[i].z);
		//}
		//Vector3f* normal = mesh->GetNormals();
		//for (size_t i = 0; i < mesh->_vertex_count; i++)
		//{
		//	const Vector3f& n = normal[i];
		//	const Vector3f& t = tan1[i];
		//	tangents[i].xyz = Normalize(t - n * DotProduct(n, t));
		//	tangents[i].w = (DotProduct(CrossProduct(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
		//}
		//delete[] tan1;
		mesh->SetTangents(std::move(tangents));
		return true;
	}

	void FbxParser::GenerateIndexdMesh(Mesh* mesh)
	{
		if (!mesh)
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), "mesh is null");
			return;
		}
		SkinedMesh* skined_mesh = dynamic_cast<SkinedMesh*>(mesh);

		u32 vertex_count = mesh->_vertex_count;
		std::unordered_map<uint64_t, u32> vertex_map{};
		std::vector<Vector3f> normals{};
		std::vector<Vector3f> positions{};
		std::vector<Vector2f> uv0s{};
		std::vector<u32> indices{};
		std::map<u16, Vector<u32>> submesh_indices{};
		u32 cur_index_count = 0u;
		auto raw_normals = mesh->GetNormals();
		auto raw_uv0 = mesh->GetUVs(0u);
		auto raw_pos = mesh->GetVertices();
		//for skined mesh
		std::vector<Vector4D<u32>> bone_indices{};
		std::vector<Vector4f> bone_weights{};
		auto raw_bonei = skined_mesh ? skined_mesh->GetBoneIndices() : nullptr;
		auto raw_bonew = skined_mesh ? skined_mesh->GetBoneWeights() : nullptr;

		Math::ALHash::Vector3fHash v3hash{};
		Math::ALHash::Vector2fHash v2hash{};
		Math::ALHash::VectorHash<Vector4D, float> v4fhash{};
		Math::ALHash::VectorHash<Vector4D, u32> u4fhash{};
		TimeMgr mgr;
		mgr.Mark();
		constexpr float minf = std::numeric_limits<float>::lowest();
		constexpr float maxf = std::numeric_limits<float>::max();
		Vector3f vertex_min{ maxf,maxf ,maxf };
		Vector3f vertex_max{ minf,minf ,minf };
		Vector3f mesh_vmin = vertex_min;
		Vector3f mesh_vmax = vertex_max;
		Map<u32,std::tuple<Vector3f, Vector3f>> subemesh_aabbs;
		if (raw_bonei)
		{
			for (size_t i = 0; i < vertex_count; i++)
			{
				auto p = raw_pos[i];
				auto n = raw_normals[_b_normal_by_controlpoint ? _positon_conrtol_index_mapper[i] : i];
				auto uv = raw_uv0[i];
				auto hash0 = v3hash(n), hash1 = v2hash(uv);
				auto vertex_hash = Math::ALHash::CombineHashes(hash0, hash1);
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
					mesh_vmin = Min(mesh_vmin, p);
					mesh_vmax = Max(mesh_vmax, p);
				}
				else indices.emplace_back(it->second);
			}
		}
		else
		{
			for (size_t i = 0; i < vertex_count; i++)
			{
				int submesh_index = _positon_material_index_mapper[i];
				auto p = raw_pos[i], n = raw_normals[i];
				auto uv = raw_uv0[i];
				auto hash0 = v3hash(n), hash1 = v2hash(uv);
				auto vertex_hash = Math::ALHash::CombineHashes(hash0, hash1);
				auto it = vertex_map.find(vertex_hash);
				if (it == vertex_map.end())
				{
					vertex_map[vertex_hash] = cur_index_count;
					submesh_indices[submesh_index].emplace_back(cur_index_count);
					indices.emplace_back(cur_index_count);
					normals.emplace_back(n);
					positions.emplace_back(p);
					uv0s.emplace_back(uv);
					if (subemesh_aabbs.contains(submesh_index))
					{
						auto& [exist_min, exist_max] = subemesh_aabbs[submesh_index];
						exist_min = Min(exist_min, p);
						exist_max = Max(exist_max, p);
					}
					else
					{
						subemesh_aabbs[submesh_index] = std::make_tuple(vertex_min,vertex_max);
					}
					++cur_index_count;
				}
				else
				{
					indices.emplace_back(it->second);
					submesh_indices[submesh_index].emplace_back(it->second);
				}
			}
		}

		//LOG_INFO("indices gen takes {}ms", mgr.GetElapsedSinceLastMark());
		mesh->Clear();
		float aabb_space = 0.25f;
		mesh->_bound_boxs.emplace_back(AABB(mesh_vmin, mesh_vmax));
		for (auto& it : subemesh_aabbs)
		{
			auto& [exist_min, exist_max] = it.second;
			mesh_vmin = Min(mesh_vmin, exist_min);
			mesh_vmax = Max(mesh_vmax, exist_max);
			mesh->_bound_boxs.emplace_back(AABB(exist_min, exist_max));
		}
		mesh->_bound_boxs[0] = AABB(mesh_vmin, mesh_vmax);
		for (auto& box : mesh->_bound_boxs)
		{
			box._min -= aabb_space;
			box._max += aabb_space;
		}
		vertex_count = (u32)positions.size();
		//auto index_count = indices.size();
		mesh->_vertex_count = vertex_count;
		//mesh->_index_count = (u32)index_count;
		float* vertex_buf = new float[vertex_count * 3];
		float* normal_buf = new float[vertex_count * 3];
		float* uv0_buf = new float[vertex_count * 2];
		memcpy(uv0_buf, uv0s.data(), sizeof(Vector2f) * vertex_count);
		memcpy(normal_buf, normals.data(), sizeof(Vector3f) * vertex_count);
		memcpy(vertex_buf, positions.data(), sizeof(Vector3f) * vertex_count);
		mesh->SetUVs(std::move(reinterpret_cast<Vector2f*>(uv0_buf)), 0u);
		mesh->SetNormals(std::move(reinterpret_cast<Vector3f*>(normal_buf)));
		mesh->SetVertices(std::move(reinterpret_cast<Vector3f*>(vertex_buf)));

		for (int i = 0; i < submesh_indices.size(); ++i)
		{
			auto& submesh_indices_i = submesh_indices[i];
			u32 cur_indices_count = static_cast<u32>(submesh_indices_i.size());
			u32* index_buf = new u32[cur_indices_count];
			memcpy(index_buf, submesh_indices_i.data(), sizeof(u32) * cur_indices_count);
			mesh->AddSubmesh(index_buf, cur_indices_count);
		}
		//mesh->SetIndices(std::move(reinterpret_cast<u32*>(index_buf)));
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
		//return ParserImpl(sys_path, false);
		_cur_file_sys_path = sys_path;
		scale_factor = Vector3f::kOne;
		String path = ToChar(sys_path.data());
		_p_cur_fbx_scene = FbxScene::Create(fbx_manager_, "RootScene");
		List<Ref<Mesh>> loaded_meshs{};
		if (fbx_importer_ != nullptr && !fbx_importer_->Initialize(path.c_str(), -1, fbx_manager_->GetIOSettings()))
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), "Load mesh failed whit invalid path {}", path);
			return loaded_meshs;
		}
		fbx_importer_->Import(_p_cur_fbx_scene);
		FbxNode* fbx_rt = _p_cur_fbx_scene->GetRootNode();
		fbxsdk::FbxAxisSystem::DirectX.DeepConvertScene(_p_cur_fbx_scene);
		if (_p_cur_fbx_scene->GetGlobalSettings().GetSystemUnit() != fbxsdk::FbxSystemUnit::m)
		{
			const fbxsdk::FbxSystemUnit::ConversionOptions lConversionOptions = {
			false, /* mConvertRrsNodes */
			true, /* mConvertAllLimits */
			true, /* mConvertClusters */
			true, /* mConvertLightIntensity */
			true, /* mConvertPhotometricLProperties */
			true  /* mConvertCameraClipPlanes */
			};
			fbxsdk::FbxSystemUnit::m.ConvertScene(_p_cur_fbx_scene, lConversionOptions);
		}
		FillPoseArray(_p_cur_fbx_scene, _fbx_poses);
		Queue<FbxNode*> mesh_node, skeleton_node;
		ParserFbxNode(fbx_rt, mesh_node, skeleton_node);
		_cur_skeleton.Clear();//当前仅支持一个骨骼，所以清空之前的数据
		_loaded_anims.clear();
		_imported_fbx_materials.clear();
		auto skeleton_node_c = skeleton_node;
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
		return loaded_meshs;
	}

	List<Ref<Mesh>> FbxParser::ParserImpl(WString sys_path, bool ph)
	{
		String path = ToChar(sys_path.data());
		_p_cur_fbx_scene = FbxScene::Create(fbx_manager_, "RootScene");
		List<Ref<Mesh>> loaded_meshs{};
		if (fbx_importer_ != nullptr && !fbx_importer_->Initialize(path.c_str(), -1, fbx_manager_->GetIOSettings()))
		{
			g_pLogMgr->LogErrorFormat(std::source_location::current(), "Load mesh failed at path {}", path);
			return loaded_meshs;
		}
		if (fbx_importer_->Import(_p_cur_fbx_scene))
		{
			// Check the scene integrity!
			FbxStatus status;
			FbxArray< FbxString*> details;
			FbxSceneCheckUtility sceneCheck(FbxCast<FbxScene>(_p_cur_fbx_scene), &status, &details);
			bool lNotify = (!sceneCheck.Validate(FbxSceneCheckUtility::eCkeckData) && details.GetCount() > 0) || (fbx_importer_->GetStatus().GetCode() != FbxStatus::eSuccess);
			if (lNotify)
			{
				LOG_ERROR("\n");
				LOG_ERROR("********************************************************************************\n");
				if (details.GetCount())
				{
					LOG_ERROR("Scene integrity verification failed with the following errors:\n");
					for (int i = 0; i < details.GetCount(); i++)
						LOG_ERROR("   {}\n", details[i]->Buffer());
					FbxArrayDelete<FbxString*>(details);
				}
				if (fbx_importer_->GetStatus().GetCode() != FbxStatus::eSuccess)
				{
					LOG_ERROR("\n");
					LOG_ERROR("WARNING:\n");
					LOG_ERROR("   The importer was able to read the file but with errors.\n");
					LOG_ERROR("   Loaded scene may be incomplete.\n\n");
					LOG_ERROR("   Last error message: {}\n", fbx_importer_->GetStatus().GetErrorString());
				}
				LOG_ERROR("********************************************************************************\n");
				LOG_ERROR("\n");
			}
			//convert scene axis and unit
			FbxAxisSystem scene_axis_system = _p_cur_fbx_scene->GetGlobalSettings().GetAxisSystem();
			if (scene_axis_system != FbxAxisSystem::DirectX)
				FbxAxisSystem::DirectX.DeepConvertScene(_p_cur_fbx_scene);
			fbxsdk::FbxSystemUnit scene_sys_unit = _p_cur_fbx_scene->GetGlobalSettings().GetSystemUnit();
			if (scene_sys_unit.GetScaleFactor() != 1.0)
				fbxsdk::FbxSystemUnit::cm.ConvertScene(_p_cur_fbx_scene);
			_p_cur_fbx_scene->FillAnimStackNameArray(_fbx_anim_stack_names);
			SetCurrentAnimStack(0);
			//convert all geometry object to triangle mesh
			FbxGeometryConverter converter(fbx_manager_);
			try
			{
				converter.Triangulate(FbxCast<FbxScene>(_p_cur_fbx_scene), true);
			}
			catch (std::runtime_error)
			{
				LOG_ERROR("Scene integrity verification failed.\n");
			}
			ParserSceneNodeRecursive(_p_cur_fbx_scene->GetRootNode(), _p_fbx_anim_layer);
			FillPoseArray(_p_cur_fbx_scene, _fbx_poses);
		}
		FbxNode* fbx_rt = _p_cur_fbx_scene->GetRootNode();

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
		_cur_skeleton.Clear();//当前仅支持一个骨骼，所以清空之前的数据
		_loaded_anims.clear();
		auto skeleton_node_c = skeleton_node;
		while (!skeleton_node.empty())
		{
			ParserSkeleton(skeleton_node.front(), _cur_skeleton);
			skeleton_node.pop();
		}
		while (!mesh_node.empty())
		{
			auto ret = g_pThreadTool->Enqueue(&FbxParser::ParserAnimation, this, mesh_node.front(), _cur_skeleton);
			ParserMesh(mesh_node.front(), loaded_meshs);
			if (ret.get())
				mesh_node.pop();
		}
		if (loaded_meshs.empty())
		{
			g_pLogMgr->LogErrorFormat(L"Load fbx from path {} failed!", sys_path);
		}
		return loaded_meshs;
	}

	void FbxParser::FillCameraArray(FbxScene* pScene, FbxArray<FbxNode*>& pCameraArray)
	{
		pCameraArray.Clear();
		FillCameraArrayRecursive(pScene->GetRootNode(), pCameraArray);
	}

	void FbxParser::FillCameraArrayRecursive(FbxNode* pNode, FbxArray<FbxNode*>& pCameraArray)
	{
		if (pNode)
		{
			if (pNode->GetNodeAttribute())
			{
				if (pNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eCamera)
				{
					pCameraArray.Add(pNode);
				}
			}
			const int lCount = pNode->GetChildCount();
			for (int i = 0; i < lCount; i++)
			{
				FillCameraArrayRecursive(pNode->GetChild(i), pCameraArray);
			}
		}
	}

	void FbxParser::FillPoseArray(FbxScene* pScene, FbxArray<FbxPose*>& pPoseArray)
	{
		const int lPoseCount = pScene->GetPoseCount();
		for (int i = 0; i < lPoseCount; ++i)
		{
			pPoseArray.Add(pScene->GetPose(i));
		}
	}

	bool FbxParser::SetCurrentAnimStack(u16 index)
	{
		const int lAnimStackCount = _fbx_anim_stack_names.GetCount();
		if (!lAnimStackCount || index >= lAnimStackCount)
			return false;
		// select the base layer from the animation stack
		FbxAnimStack* lCurrentAnimationStack = _p_cur_fbx_scene->FindMember<FbxAnimStack>(_fbx_anim_stack_names[index]->Buffer());
		if (lCurrentAnimationStack == NULL)
		{
			// this is a problem. The anim stack should be found in the scene!
			return false;
		}
		// we assume that the first animation layer connected to the animation stack is the base layer
		// (this is the assumption made in the FBXSDK)
		_p_fbx_anim_layer = lCurrentAnimationStack->GetMember<FbxAnimLayer>();
		_p_cur_fbx_scene->SetCurrentAnimationStack(lCurrentAnimationStack);

		FbxTakeInfo* lCurrentTakeInfo = _p_cur_fbx_scene->GetTakeInfo(*(_fbx_anim_stack_names[index]));
		if (lCurrentTakeInfo)
		{
			_start_time = lCurrentTakeInfo->mLocalTimeSpan.GetStart();
			_end_time = lCurrentTakeInfo->mLocalTimeSpan.GetStop();
		}
		else
		{
			// Take the time line value
			FbxTimeSpan lTimeLineTimeSpan;
			_p_cur_fbx_scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(lTimeLineTimeSpan);
			_start_time = lTimeLineTimeSpan.GetStart();
			_end_time = lTimeLineTimeSpan.GetStop();
		}
		return true;
	}

	void FbxParser::ParserSceneNodeRecursive(FbxNode* pNode, FbxAnimLayer* pAnimLayer)
	{
		const int mat_count = pNode->GetMaterialCount();
		for (int i = 0; i < mat_count; i++)
		{
			FbxSurfaceMaterial* material = pNode->GetMaterial(i);
		}
		FbxNodeAttribute* node_attribute = pNode->GetNodeAttribute();
		if (node_attribute)
		{
			if (node_attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
			{

			}
			else if (node_attribute->GetAttributeType() == FbxNodeAttribute::eLight)
			{

			}
		}
		const int child_count = pNode->GetChildCount();
		for (int i = 0; i < child_count; ++i)
		{
			ParserSceneNodeRecursive(pNode->GetChild(i), pAnimLayer);
		}
	}
#pragma warning(pop)
}
