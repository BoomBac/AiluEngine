#include "Framework/Parser/FbxParser.h"
#include "Animation/SkeletonAsset.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/Utils.h"
#include "Framework/Math/MathHash.hpp"
#include "Animation/AnimationKeyReducer.h"
#include "pch.h"
#include <unordered_set>
//#include "Animation/Skeleton.h"
#include "Animation/Clip.h"

using fbxsdk::FbxCast;
using fbxsdk::FbxNode;
using fbxsdk::FbxNodeAttribute;
using namespace Ailu::Render;

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)

    static Matrix4x4f FbxMatToMat4x4f(const FbxAMatrix &src)
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
        t._position = Vector3f{(float) p[0], (float) p[1], (float) p[2]};
        t._scale = Vector3f{(float) s[0], (float) s[1], (float) s[2]};
        t._rotation = Quaternion{(float) q[0], (float) q[1], (float) q[2], (float) q[3]};
        t._rotation.NormalizeQ();
        return t;
    }

    // Get the matrix of the given pose
    static FbxAMatrix GetPoseMatrix(FbxPose *pPose, int pNodeIndex)
    {
        FbxAMatrix lPoseMatrix;
        FbxMatrix lMatrix = pPose->GetMatrix(pNodeIndex);

        memcpy((double *) lPoseMatrix, (double *) lMatrix, sizeof(lMatrix.mData));

        return lPoseMatrix;
    }

    static FbxAMatrix GetGlobalPosition(FbxNode *pNode, const FbxTime &pTime, FbxPose *pPose = nullptr, FbxAMatrix *pParentGlobalPosition = nullptr)
    {
        FbxAMatrix lGlobalPosition;
        bool lPositionFound = false;

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
    static FbxAMatrix GetGeometry(FbxNode *pNode)
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

    static void GetGeometry(FbxNode *pNode, FbxVector4 &t, FbxVector4 &r, FbxVector4 &s)
    {
        t = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
        r = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
        s = pNode->GetGeometricScaling(FbxNode::eSourcePivot);
    }

    static void ComputeClusterDeformation(bool is_local, FbxAMatrix &pGlobalPosition, FbxNode *pMesh, FbxCluster *pCluster, FbxNode *cluster_link,
                                          FbxAMatrix &pVertexTransformMatrix, FbxTime pTime, FbxPose *pPose)
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
            AL_ASSERT(false);
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

                FbxNode *pParentNode = pCluster->GetLink()->GetParent();
                FbxAMatrix parentMatrix = pParentNode->EvaluateLocalTransform(pTime);
                FbxNode *cur_node = pParentNode;
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

    static FbxAMatrix GetNodeGlobalTransformAtTime(FbxNode *node, FbxTime time = 0u)
    {
        FbxAMatrix global_transform = node->EvaluateGlobalTransform(time);
        FbxVector4 t, r, s;
        GetGeometry(node, t, r, s);
        FbxAMatrix geometry_transform;
        geometry_transform.SetTRS(t, r, s);
        global_transform *= geometry_transform;
        return global_transform;
    }

    static FbxAMatrix GetSkeletonGlobalTransformAtTime(FbxNode *node, FbxTime time = 0u)
    {
        // Geometric transforms belong to the mesh bind transform, not to the joint animation pose.
        return node->EvaluateGlobalTransform(time);
    }

    static FbxAMatrix GetSkeletonLocalTransform(FbxNode *node, const FbxTime &time)
    {
        FbxAMatrix local_transform = node->EvaluateLocalTransform(time);
        FbxNode *parent_node = node->GetParent();
        if (parent_node == nullptr || parent_node->GetSkeleton())
            return local_transform;

        FbxAMatrix parent_transform = parent_node->EvaluateLocalTransform(time);
        while ((parent_node = parent_node->GetParent()) != nullptr)
            parent_transform = parent_node->EvaluateLocalTransform(time) * parent_transform;
        return parent_transform * local_transform;
    }

    static FbxAMatrix GetNodeBindGlobalTransform(FbxNode *node, const FbxArray<FbxPose *> &poses)
    {
        for (int pose_index = 0; pose_index < poses.GetCount(); ++pose_index)
        {
            FbxPose *pose = poses[pose_index];
            if (pose != nullptr && pose->IsBindPose() && pose->Find(node) >= 0)
                return GetGlobalPosition(node, FbxTime(), pose);
        }
        return node->EvaluateGlobalTransform();
    }

    FbxParser::FbxParser()
    {
        fbx_manager_ = FbxManager::Create();
        fbx_ios_ = FbxIOSettings::Create(fbx_manager_, IOSROOT);
        fbx_manager_->SetIOSettings(fbx_ios_);
        fbx_importer_ = FbxImporter::Create(fbx_manager_, "");
    }

    Ailu::FbxParser::~FbxParser()
    {
        // try
        // {
        //     fbx_manager_->Destroy();
        //     fbx_importer_->Destroy();
        //     fbx_ios_->Destroy();
        // }
        // catch (const std::exception& e)
        // {
        //     std::cout << e.what() << std::endl;
        // }
    }

    void FbxParser::Parser(const WString &sys_path, const MeshImportSetting &import_setting)
    {
        std::unique_lock<std::mutex> lock(_parser_lock);
        _import_setting = import_setting;
        ParserImpl(sys_path);
    }


    void FbxParser::ParserFbxNode(FbxNode *node, Queue<FbxNode *> &mesh_node, Queue<FbxNode *> &skeleton_node)
    {
        if (node != nullptr)
        {
            FbxNodeAttribute *attribute = node->GetNodeAttribute();
            if (attribute)
            {
                auto type = attribute->GetAttributeType();
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

    void FbxParser::ParserSkeleton(FbxNode *node, Skeleton &sk)
    {
        FbxNodeAttribute *attr = node->GetNodeAttribute();
        if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
        {
            i32 joint_index = Skeleton::GetJointIndexByName(sk, node->GetName());
            if (joint_index != -1)
            {
                _skeleton_joint_nodes[(u16)joint_index] = node;
                return;
            }
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
                i32 new_joint_index = node->GetParent() == nullptr ? -1 :
                    Skeleton::GetJointIndexByName(sk, node->GetParent()->GetName());
                parent_joint_index = new_joint_index == -1 ? Joint::kInvalidJointIndex : new_joint_index;
                Joint joint;
                joint._name = node->GetName();
                joint._parent = parent_joint_index;
                joint._self = sk.JointNum();
                if (parent_joint_index != Joint::kInvalidJointIndex)
                    sk[parent_joint_index]._children.emplace_back(joint._self);
                sk.AddJoint(joint);
                _skeleton_joint_nodes[joint._self] = node;
            }
        }
    }

    void FbxParser::BuildSkeletonBindPose(Queue<FbxNode *> mesh_nodes, Skeleton &sk)
    {
        std::unordered_set<u16> cluster_bound_joints;
        while (!mesh_nodes.empty())
        {
            FbxNode *mesh_node = mesh_nodes.front();
            mesh_nodes.pop();
            FbxMesh *fbx_mesh = mesh_node != nullptr ? mesh_node->GetMesh() : nullptr;
            if (fbx_mesh == nullptr)
                continue;

            for (u32 skin_index = 0u; skin_index < fbx_mesh->GetDeformerCount(FbxDeformer::eSkin); ++skin_index)
            {
                FbxSkin *skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(skin_index, FbxDeformer::eSkin));
                if (skin == nullptr)
                    continue;
                for (u32 cluster_index = 0u; cluster_index < skin->GetClusterCount(); ++cluster_index)
                {
                    FbxCluster *cluster = skin->GetCluster(cluster_index);
                    FbxNode *link = cluster != nullptr ? cluster->GetLink() : nullptr;
                    if (link == nullptr)
                        continue;
                    const i32 joint_index = Skeleton::GetJointIndexByName(sk, link->GetName());
                    if (joint_index < 0)
                        continue;

                    const u16 joint_id = static_cast<u16>(joint_index);
                    if (!_skeleton_bind_globals.contains(joint_id))
                    {
                        FbxAMatrix bind_global;
                        cluster->GetTransformLinkMatrix(bind_global);
                        _skeleton_bind_globals.emplace(joint_id, bind_global);
                        _skeleton_joint_nodes[joint_id] = link;
                    }
                    cluster_bound_joints.insert(joint_id);
                }
            }
        }

        for (const Joint &joint : sk)
        {
            if (_skeleton_bind_globals.contains(joint._self))
                continue;
            const auto node_iter = _skeleton_joint_nodes.find(joint._self);
            if (node_iter != _skeleton_joint_nodes.end())
                _skeleton_bind_globals.emplace(joint._self, GetNodeBindGlobalTransform(node_iter->second, _fbx_poses));
        }

        for (const Joint &joint : sk)
        {
            const auto bind_iter = _skeleton_bind_globals.find(joint._self);
            if (bind_iter == _skeleton_bind_globals.end())
                continue;

            FbxAMatrix bind_local = bind_iter->second;
            const auto node_iter = _skeleton_joint_nodes.find(joint._self);
            const bool is_cluster_bound = cluster_bound_joints.contains(joint._self);
            if (joint._parent != Joint::kInvalidJointIndex && !is_cluster_bound &&
                node_iter != _skeleton_joint_nodes.end())
            {
                bind_local = GetSkeletonLocalTransform(node_iter->second, FbxTime());
            }
            else if (joint._parent != Joint::kInvalidJointIndex)
            {
                const auto parent_bind_iter = _skeleton_bind_globals.find(joint._parent);
                if (parent_bind_iter == _skeleton_bind_globals.end())
                    continue;
                bind_local = parent_bind_iter->second.Inverse() * bind_iter->second;
            }
            sk.SetBindPoseLocalTransform(joint._self, FbxMatToTransform(bind_local));
        }

        Vector<Matrix4x4f> bind_palette;
        sk.GetBindPose().GetMatrixPalette(bind_palette);
        for (Joint &joint : sk)
        {
            if (joint._self < bind_palette.size())
                joint._inv_bind_pos = Math::MatrixInverse(bind_palette[joint._self]);
        }
    }

    bool FbxParser::ParserMesh(FbxNode *node, List<Ref<Mesh>> &loaded_meshes)
    {
        auto fbx_mesh = node->GetMesh();
        if (_import_setting.ShouldImportMesh())
        {
            if (!fbx_mesh->IsTriangleMesh())
            {
                _time_mgr.Mark();
                fbxsdk::FbxGeometryConverter convert(fbx_manager_);
                fbx_mesh = FbxCast<fbxsdk::FbxMesh>(convert.Triangulate(fbx_mesh, true));
                LOG_INFO("Triangulate mesh cost {}ms", _time_mgr.GetElapsedSinceLastMark());
            }
        }
        bool is_skined = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
        auto mesh = is_skined ? MakeRef<SkeletonMesh>(node->GetName()) : MakeRef<Mesh>(node->GetName());
        const int mat_count = node->GetMaterialCount();
        struct FbxTextureInfo
        {
            Vector3f color{1.0f, 1.0f, 1.0f};
            f32 factor;
            String texture_path;
            bool has_texture = false;
        };

        static auto fill_tex = [&](const FbxProperty &prop) -> FbxTextureInfo
        {
            FbxTextureInfo info{};

            // -----------颜色部分 -----------
            if (prop.IsValid())
            {
                FbxDataType type = prop.GetPropertyDataType();
                auto type_name = type.GetName();
                if (type.Is(FbxColor3DT))
                {
                    FbxDouble3 color = prop.Get<FbxDouble3>();
                    info.color = {(float) color[0], (float) color[1], (float) color[2]};
                }
                else if (type.Is(FbxColor4DT))
                {
                    FbxDouble4 color = prop.Get<FbxDouble4>();
                    info.color = {(float) color[0], (float) color[1], (float) color[2]};
                }
                else if (type.Is(FbxDoubleDT))
                {
                    double value = prop.Get<FbxDouble>();
                    info.color = {(float) value, (float) value, (float) value};
                    info.factor = (f32) value;
                }
            }

            // -----------贴图部分 -----------
            int layeredCount = prop.GetSrcObjectCount<FbxLayeredTexture>();
            if (layeredCount > 0)
            {
                LOG_WARNING("Multi-layer texture not supported yet: {}", prop.GetName());
                for (int layerIndex = 0; layerIndex < layeredCount; ++layerIndex)
                {
                    FbxLayeredTexture *layeredTexture = prop.GetSrcObject<FbxLayeredTexture>(layerIndex);
                    int texCount = layeredTexture->GetSrcObjectCount<FbxTexture>();
                    for (int t = 0; t < texCount; ++t)
                    {
                        if (FbxFileTexture *fileTex = FbxCast<FbxFileTexture>(layeredTexture->GetSrcObject<FbxFileTexture>(t)))
                        {
                            info.texture_path = fileTex->GetFileName();
                            info.has_texture = true;
                        }
                    }
                }
            }
            else
            {
                int texCount = prop.GetSrcObjectCount<FbxTexture>();
                for (int t = 0; t < texCount; ++t)
                {
                    if (FbxFileTexture *fileTex = FbxCast<FbxFileTexture>(prop.GetSrcObject<FbxFileTexture>(t)))
                    {
                        info.texture_path = fileTex->GetFileName();
                        info.has_texture = true;
                    }
                }
            }

            return info;
        };
        _cur_node_transform = GetNodeGlobalTransformAtTime(node);
        bool is_multithread = false;
        Vector<std::future<bool>> rets;
        if (_import_setting.ShouldImportMesh())
        {
            RawMeshData mesh_data;
            if (is_multithread)
            {
                rets.emplace_back(Core::ThreadPool::Get().Enqueue(&FbxParser::ReadVertex, this, node, std::ref(mesh_data._positions), std::ref(mesh_data._bone_weights), std::ref(mesh_data._bone_indices)));
                rets.emplace_back(Core::ThreadPool::Get().Enqueue(&FbxParser::ReadNormal, this, node, std::ref(mesh_data._normals)));
                rets.emplace_back(Core::ThreadPool::Get().Enqueue(&FbxParser::ReadUVs, this, std::ref(*fbx_mesh), std::ref(mesh_data._uvs)));
                rets.emplace_back(Core::ThreadPool::Get().Enqueue([this, node, mesh]()
                                                                   {
                                                                       if (auto *skeleton_mesh =
                                                                               dynamic_cast<SkeletonMesh *>(mesh.get()))
                                                                           BuildMeshBindTransform(node, skeleton_mesh);
                                                                       return true;
                                                                   }));
                for (auto &ret: rets)
                    ret.get();
            }
            else
            {
                ReadVertex(node, mesh_data._positions, mesh_data._bone_weights, mesh_data._bone_indices);
                ReadNormal(node, mesh_data._normals);
                ReadUVs(*fbx_mesh, mesh_data._uvs);
                if (is_skined)
                    BuildMeshBindTransform(node, dynamic_cast<SkeletonMesh *>(mesh.get()));
            }
            GenerateIndexdMesh(&mesh_data, mesh.get());
            CalculateTangant(mesh.get());
            auto const ShininessToRoughness = [](f32 shininess)
            {
                shininess = std::max(shininess, 0.0001f);
                f32 roughness = std::sqrt(2.0f / (shininess + 2.0f));
                return std::clamp(roughness, 0.0f, 1.0f);
            };
            if (is_skined)
            {
                dynamic_cast<SkeletonMesh *>(mesh.get())->SetSkeletonAsset(_skeleton_asset);
            }
            {
                for (int i = 0; i < mat_count; ++i)
                {
                    FbxSurfaceMaterial *mat = node->GetMaterial(i);
                    if (mat)
                    {
                        Mesh::ImportedMaterialInfo mat_info(i, mat->GetName());
                        mat_info._source_id = static_cast<u64>(mat->GetUniqueID());
                        if (auto prop = mat->FindProperty(FbxSurfaceMaterial::sDiffuse); prop.IsValid())
                        {
                            FbxTextureInfo tex_info;
                            tex_info = fill_tex(prop);
                            if (tex_info.has_texture)
                                mat_info._textures[0] = tex_info.texture_path;
                            mat_info._diffuse = tex_info.color;
                        }
                        if (auto prop = mat->FindProperty(FbxSurfaceMaterial::sNormalMap); prop.IsValid())
                        {
                            FbxTextureInfo tex_info;
                            tex_info = fill_tex(prop);
                            if (tex_info.has_texture)
                                mat_info._textures[1] = tex_info.texture_path;
                        }
                        if (auto prop = mat->FindProperty(FbxSurfaceMaterial::sEmissive); prop.IsValid())
                        {
                            mat_info._emissive = Colors::kBlack;
                            FbxTextureInfo tex_info;
                            tex_info = fill_tex(prop);
                            if (tex_info.has_texture)
                            {
                                mat_info._textures[2] = tex_info.texture_path;
                                mat_info._emissive = tex_info.color;
                            }
                        }
                        if (auto prop = mat->FindProperty(FbxSurfaceMaterial::sShininess); prop.IsValid())
                        {
                            FbxTextureInfo tex_info;
                            tex_info = fill_tex(prop);
                            mat_info._roughness = ShininessToRoughness(tex_info.factor);
                        }
                        mesh->AddCacheMaterial(mat_info);
                    }
                }
            }
            loaded_meshes.emplace_back(mesh);
            return true;
        }
        return true;
    }

    void FbxParser::BuildMeshBindTransform(FbxNode *node, SkeletonMesh *skeleton_mesh)
    {
        auto fbx_mesh = node->GetMesh();
        u32 deformers_num = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
        if (deformers_num == 0u)
            return;

        FbxVector4 geometry_translation, geometry_rotation, geometry_scale;
        GetGeometry(node, geometry_translation, geometry_rotation, geometry_scale);
        FbxAMatrix geometry_transform;
        geometry_transform.SetTRS(geometry_translation, geometry_rotation, geometry_scale);

        FbxAMatrix mesh_bind_global;
        bool has_mesh_bind_global = false;
        for (u32 skin_index = 0u; skin_index < deformers_num; ++skin_index)
        {
            FbxSkin *skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(skin_index, FbxDeformer::eSkin));
            if (skin == nullptr)
                continue;
            for (u32 cluster_index = 0u; cluster_index < skin->GetClusterCount(); ++cluster_index)
            {
                FbxCluster *cluster = skin->GetCluster(cluster_index);
                FbxNode *cluster_link = cluster != nullptr ? cluster->GetLink() : nullptr;
                if (cluster_link == nullptr)
                    continue;

                if (!has_mesh_bind_global)
                {
                    cluster->GetTransformMatrix(mesh_bind_global);
                    mesh_bind_global *= geometry_transform;
                    has_mesh_bind_global = true;
                }
            }
        }

        if (skeleton_mesh != nullptr && has_mesh_bind_global)
            skeleton_mesh->SetMeshBindGlobalTransform(FbxMatToMat4x4f(mesh_bind_global));
    }

    bool FbxParser::ParserAnimation(Skeleton &sk)
    {
        if (!_import_setting.ShouldImportAnimation())
            return true;

        const int stack_count = _p_cur_fbx_scene->GetSrcObjectCount<FbxAnimStack>();
        if (stack_count == 0)
            return true;
        const int stack_index = std::clamp(_import_setting._animation_stack_index, 0, stack_count - 1);
        FbxAnimStack *anim_stack = _p_cur_fbx_scene->GetSrcObject<FbxAnimStack>(stack_index);
        if (anim_stack == nullptr)
            return false;
        if (stack_count > 1)
        {
            const String fbx_path = ToChar(_cur_file_sys_path.data());
            LOG_INFO("FBX {} has {} AnimationStacks; importing stack {} ({})", fbx_path, stack_count,
                     stack_index, String(anim_stack->GetName()));
        }
        _p_cur_fbx_scene->SetCurrentAnimationStack(anim_stack);
        const FbxTime::EMode time_mode = _p_cur_fbx_scene->GetGlobalSettings().GetTimeMode();

        FbxTimeSpan time_span;
        if (FbxTakeInfo *take_info = _p_cur_fbx_scene->GetTakeInfo(anim_stack->GetName()))
            time_span = take_info->mLocalTimeSpan;
        else
            _p_cur_fbx_scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(time_span);
        const FbxTime start_time = time_span.GetStart();
        const FbxTime duration = time_span.GetDuration();
        const FbxLongLong frame_count = std::max<FbxLongLong>(1, duration.GetFrameCount(time_mode) + 1);
        const f64 frame_rate = FbxTime::GetFrameRate(time_mode);

        Ref<AnimationClip> clip = MakeRef<AnimationClip>();
        clip->Name(String(anim_stack->GetName()));
        clip->Duration(static_cast<f32>(duration.GetSecondDouble()));
        clip->FrameCount(frame_count);
        clip->FrameRate(frame_rate);
        const f32 frame_duration = frame_rate > 0.0 ? static_cast<f32>(1.0 / frame_rate) :
            (frame_count > 1 ? static_cast<f32>(duration.GetSecondDouble() / (frame_count - 1)) : 0.0f);
        clip->FrameDuration(frame_duration);

        for (const Joint &joint : sk)
        {
            const auto node_iter = _skeleton_joint_nodes.find(joint._self);
            if (node_iter == _skeleton_joint_nodes.end() || node_iter->second == nullptr)
                continue;

            TransformTrack &track = (*clip)[joint._self];
            auto &pos_track = track.GetPositionTrack();
            auto &rot_track = track.GetRotationTrack();
            auto &scale_track = track.GetScaleTrack();
            pos_track.Resize(frame_count);
            rot_track.Resize(frame_count);
            scale_track.Resize(frame_count);
            for (FbxLongLong frame_index = 0; frame_index < frame_count; ++frame_index)
            {
                FbxTime frame_offset;
                frame_offset.SetFrame(frame_index, time_mode);
                FbxTime sample_time = start_time;
                sample_time += frame_offset;
                FbxAMatrix current_global =
                    GetSkeletonGlobalTransformAtTime(node_iter->second, sample_time);
                FbxAMatrix current_local = current_global;
                if (joint._parent != Joint::kInvalidJointIndex)
                {
                    const auto parent_node_iter = _skeleton_joint_nodes.find(joint._parent);
                    if (parent_node_iter != _skeleton_joint_nodes.end() && parent_node_iter->second != nullptr)
                    {
                        const FbxAMatrix parent_global =
                            GetSkeletonGlobalTransformAtTime(parent_node_iter->second, sample_time);
                        current_local = parent_global.Inverse() * current_global;
                    }
                }
                const Transform local_transform = FbxMatToTransform(current_local);
                Transform animation_local = local_transform;
                if (joint._parent == Joint::kInvalidJointIndex)
                {
                    // Root motion is not applied to the Animator entity. Keep the root joint anchored at
                    // the bind position so CPU skinning cannot move the mesh away from its entity transform.
                    animation_local._position = sk.GetBindPose().GetLocalTransform(joint._self)._position;
                }
                memcpy(pos_track[frame_index]._value, animation_local._position.data, sizeof(Vector3f));
                memcpy(rot_track[frame_index]._value, animation_local._rotation._quat.data, sizeof(Quaternion));
                memcpy(scale_track[frame_index]._value, animation_local._scale.data, sizeof(Vector3f));
                const f32 clip_time = static_cast<f32>((sample_time - start_time).GetSecondDouble());
                pos_track[frame_index]._time = clip_time;
                rot_track[frame_index]._time = clip_time;
                scale_track[frame_index]._time = clip_time;
            }
        }
        AnimationKeyReducer::Reduce(*clip, sk.GetBindPose(), AnimationReductionSettings{});
        clip->RecalculateDuration();
        LOG_INFO("Import animation {} end", clip->Name());
        _loaded_anims.emplace_back(std::move(clip));
        return true;
    }

    bool FbxParser::ReadNormal(fbxsdk::FbxNode *node, Vector<Vector3f> &normals)
    {
        FbxMesh *fbx_mesh = node->GetMesh();
        if (!fbx_mesh || fbx_mesh->GetElementNormalCount() < 1)
            return false;

        auto *fbx_normals = fbx_mesh->GetElementNormal(0);
        int control_points_count = fbx_mesh->GetControlPointsCount();
        normals.clear();

        const bool is_skinned = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
        FbxAMatrix normal_matrix;
        if (!is_skinned)
        {
            const FbxAMatrix final_transform = GetNodeGlobalTransformAtTime(node);
            normal_matrix = final_transform.Inverse().Transpose();
        }

        auto mapping_mode = fbx_normals->GetMappingMode();
        auto ref_mode = fbx_normals->GetReferenceMode();

        if (mapping_mode == fbxsdk::FbxLayerElement::EMappingMode::eByControlPoint)
        {
            _b_normal_by_controlpoint = true;
            normals.reserve(control_points_count);

            for (int i = 0; i < control_points_count; ++i)
            {
                int normal_index = (ref_mode == fbxsdk::FbxLayerElement::EReferenceMode::eDirect) ? i : fbx_normals->GetIndexArray().GetAt(i);

                FbxVector4 normal = fbx_normals->GetDirectArray().GetAt(normal_index);

                // 如果你需要世界空间的法线，请解开下面两行的注释
                normal[3] = 0.0;
                if (!is_skinned)
                    normal = normal_matrix.MultT(normal);

                normal.Normalize();
                normals.emplace_back(Vector3f{(f32) normal[0], (f32) normal[1], (f32) normal[2]});
            }
        }
        else if (mapping_mode == fbxsdk::FbxLayerElement::EMappingMode::eByPolygonVertex)
        {
            _b_normal_by_controlpoint = false;
            int polygon_count = fbx_mesh->GetPolygonCount();

            // 统计总的顶点数量（兼容非三角形情况）
            int total_vertex_count = 0;
            for (int i = 0; i < polygon_count; ++i)
            {
                total_vertex_count += fbx_mesh->GetPolygonSize(i);
            }
            normals.reserve(total_vertex_count);

            int cur_vertex_id = 0;// 这里的全局计数器在标准遍历下是安全的
            for (int i = 0; i < polygon_count; ++i)
            {
                int polygon_size = fbx_mesh->GetPolygonSize(i);// 获取当前多边形的实际顶点数
                for (int j = 0; j < polygon_size; ++j)
                {
                    int normal_index = (ref_mode == fbxsdk::FbxLayerElement::EReferenceMode::eDirect) ? cur_vertex_id : fbx_normals->GetIndexArray().GetAt(cur_vertex_id);

                    FbxVector4 normal = fbx_normals->GetDirectArray().GetAt(normal_index);

                    // 如果你需要世界空间的法线，请解开下面两行的注释
                    normal[3] = 0.0;
                    if (!is_skinned)
                        normal = normal_matrix.MultT(normal);

                    normal.Normalize();
                    normals.emplace_back(Vector3f{(f32) normal[0], (f32) normal[1], (f32) normal[2]});

                    ++cur_vertex_id;
                }
            }
        }
        return true;
    }

    bool FbxParser::ReadVertex(fbxsdk::FbxNode *node, Vector<Vector3f> &positions, Vector<Vector4f> &weights, Vector<Vector4D<u32>> &bone_indices)
    {
        auto fbx_mesh = node->GetMesh();
        auto mesh_name = fbx_mesh->GetName();
        u32 deformers_num = fbx_mesh->GetDeformerCount(FbxDeformer::eSkin);
        std::map<u32, Vector<std::pair<u16, float>>> control_point_weight_infos{};
        if (deformers_num > 0)
        {
            for (u32 i = 0; i < deformers_num; i++)
            {
                FbxSkin *skin = FbxCast<FbxSkin>(fbx_mesh->GetDeformer(i, FbxDeformer::eSkin));
                if (!skin)
                    continue;
                u32 cluster_num = skin->GetClusterCount();
                for (u32 j = 0; j < cluster_num; j++)
                {
                    FbxCluster *cluster = skin->GetCluster(j);
                    //auto skin_type = skin->GetSkinningType();
                    String joint_name = cluster->GetLink()->GetName();
                    i32 joint_i = Skeleton::GetJointIndexByName(_cur_skeleton, joint_name);
                    if (joint_i == -1)
                    {
                        LOG_WARNING("Can't find joint {} when load mesh {}", joint_name, mesh_name);
                        continue;
                    }
                    u16 joint_index = (u16) joint_i;
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
        fbxsdk::FbxVector4 *points{fbx_mesh->GetControlPoints()};
        u32 cur_index_count = 0u;
        i32 trangle_count = fbx_mesh->GetPolygonCount();
        auto mat_element = fbx_mesh->GetElementMaterial();


        const bool is_skinned = !control_point_weight_infos.empty();
        FbxAMatrix final_transform;
        if (!is_skinned)
            final_transform = GetNodeGlobalTransformAtTime(node);

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
                        if (!is_skinned)
                            p = final_transform.MultT(p);
                        Vector3f position{(float) p[0], (float) p[1], (float) p[2]};
                        _positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
                        _positon_material_index_mapper[positions.size()] = cur_mat_index;
                        positions.emplace_back(position);
                        auto &cur_weight_info = control_point_weight_infos[cur_control_point_index];
                        if (cur_weight_info.size() > 4)
                        {
                            std::sort(cur_weight_info.begin(), cur_weight_info.end(), [](const std::pair<u16, float> &a, const std::pair<u16, float> &b)
                                      { return a.second > b.second; });
                        }
                        //AL_ASSERT(cur_weight_info.size() > 4 , "weight must equal one");
                        auto bone_effect_num = cur_weight_info.size() > 4 ? 4 : cur_weight_info.size();
                        Vector4f cur_weight{0, 0, 0, 0};
                        Vector4D<u32> cur_indices{0, 0, 0, 0};
                        for (int weight_index = 0; weight_index < bone_effect_num; ++weight_index)
                        {
                            cur_weight[weight_index] = cur_weight_info[weight_index].second;
                            cur_indices[weight_index] = cur_weight_info[weight_index].first;
                        }
                        const f32 weight_sum = cur_weight.x + cur_weight.y + cur_weight.z + cur_weight.w;
                        if (weight_sum > Math::kFloatEpsilon)
                            cur_weight /= weight_sum;
                        weights.emplace_back(cur_weight);
                        bone_indices.emplace_back(cur_indices);
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
                        if (!is_skinned)
                            p = final_transform.MultT(p);
                        Vector3f position{(float) p[0], (float) p[1], (float) p[2]};
                        _positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
                        _positon_material_index_mapper[positions.size()] = cur_mat_index;
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
                        if (!is_skinned)
                            p = final_transform.MultT(p);
                        Vector3f position{(float) p[0], (float) p[1], (float) p[2]};
                        _positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
                        _positon_material_index_mapper[positions.size()] = 0;
                        positions.emplace_back(position);
                        auto &cur_weight_info = control_point_weight_infos[cur_control_point_index];
                        if (cur_weight_info.size() > 4)
                        {
                            std::sort(cur_weight_info.begin(), cur_weight_info.end(), [](const std::pair<u16, float> &a, const std::pair<u16, float> &b)
                                      { return a.second > b.second; });
                        }
                        //AL_ASSERT(cur_weight_info.size() > 4 , "weight must equal one");
                        auto bone_effect_num = cur_weight_info.size() > 4 ? 4 : cur_weight_info.size();
                        Vector4f cur_weight{0, 0, 0, 0};
                        Vector4D<u32> cur_indices{0, 0, 0, 0};
                        for (int weight_index = 0; weight_index < bone_effect_num; ++weight_index)
                        {
                            cur_weight[weight_index] = cur_weight_info[weight_index].second;
                            cur_indices[weight_index] = cur_weight_info[weight_index].first;
                        }
                        const f32 weight_sum = cur_weight.x + cur_weight.y + cur_weight.z + cur_weight.w;
                        if (weight_sum > Math::kFloatEpsilon)
                            cur_weight /= weight_sum;
                        //AL_ASSERT((cur_weight.x >= cur_weight.y) && (cur_weight.y >= cur_weight.z) && (cur_weight.z >= cur_weight.w));
                        weights.emplace_back(cur_weight);
                        bone_indices.emplace_back(cur_indices);
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
                        if (!is_skinned)
                            p = final_transform.MultT(p);
                        Vector3f position{(float) p[0], (float) p[1], (float) p[2]};
                        _positon_conrtol_index_mapper[positions.size()] = cur_control_point_index;
                        _positon_material_index_mapper[positions.size()] = 0;
                        positions.emplace_back(position);
                    }
                }
            }
        }
        return true;
    }

    bool FbxParser::ReadUVs(const fbxsdk::FbxMesh &fbx_mesh, Vector<Vector<Vector2f>> &uvs)
    {
        //get all UV set names
        fbxsdk::FbxStringList name_list;
        fbx_mesh.GetUVSetNames(name_list);
        for (int i = 0; i < name_list.GetCount(); ++i)
        {
            //get lUVSetIndex-th uv set
            const char *uv_name = name_list.GetStringAt(i);
            const FbxGeometryElementUV *uv = fbx_mesh.GetElementUV(uv_name);
            if (!uv)
                continue;
            //index array, where holds the index referenced to the uv data
            const bool lUseIndex = uv->GetReferenceMode() != FbxGeometryElement::eDirect;
            const int lIndexCount = (lUseIndex) ? uv->GetIndexArray().GetCount() : 0;
            //iterating through the data by polygon
            const int trangle_count = fbx_mesh.GetPolygonCount();
            //float* data = new float[trangle_count * 6];
            Vector<Vector2f> uv_set;
            uv_set.resize(trangle_count * 3);
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
                        uv_set[cur_vertex_id] = Vector2f{(f32) uvs[0], (f32) (1.0f - uvs[1])};
                        //reinterpret_cast<float*>(data)[cur_vertex_id * 2] = uv->GetDirectArray().GetAt(lUVIndex)[0];
                        //reinterpret_cast<float*>(data)[cur_vertex_id * 2 + 1] = 1.f - uv->GetDirectArray().GetAt(lUVIndex)[1];
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
                        uv_set[lPolyIndexCounter] = Vector2f{(f32) uvs[0], (f32) (1.0f - uvs[1])};
                        //reinterpret_cast<float*>(data)[lPolyIndexCounter * 2] = uv->GetDirectArray().GetAt(lUVIndex)[0];
                        //reinterpret_cast<float*>(data)[lPolyIndexCounter * 2 + 1] = 1.f - uv->GetDirectArray().GetAt(lUVIndex)[1];
                        //NE_LOG(ALL,kWarning,"U:{}V:{}",uvs[0],uvs[1])
                        lPolyIndexCounter++;
                    }
                }
            }
            else
            {
                AL_ASSERT(false);
            }
            uvs.push_back(std::move(uv_set));
        }
        return true;
    }

    bool FbxParser::ReadTangent(const fbxsdk::FbxMesh &fbx_mesh, Vector<Vector3f> &tangents)
    {
        u32 tangent_num = fbx_mesh.GetElementTangentCount();
        AL_ASSERT_MSG(tangent_num > 0, "ReadTangent");
        auto *fbx_tangents = fbx_mesh.GetElementTangent();
        if (fbx_tangents == nullptr)
        {
            LOG_WARNING("FbxMesh: {} don't contain tangent info!", fbx_mesh.GetName());
            return true;
        }
        u32 vertex_count = fbx_mesh.GetControlPointsCount(), data_size = 0;
        if (fbx_tangents->GetMappingMode() == fbxsdk::FbxGeometryElement::eByPolygonVertex)
        {
            int trangle_count = fbx_mesh.GetPolygonCount();
            vertex_count = trangle_count * 3;
            tangents.resize(vertex_count);
            int cur_vertex_id = 0;
            for (int trangle_id = 0; trangle_id < trangle_count; ++trangle_id)
            {
                for (int point_id = 0; point_id < 3; ++point_id)
                {
                    UINT tangent_id = 0;
                    if (fbx_tangents->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eDirect)
                        tangent_id = cur_vertex_id;
                    else if (fbx_tangents->GetReferenceMode() == fbxsdk::FbxLayerElement::EReferenceMode::eIndexToDirect)
                        tangent_id = fbx_tangents->GetIndexArray().GetAt(cur_vertex_id);
                    auto tangent = fbx_tangents->GetDirectArray().GetAt(tangent_id);
                    tangents[cur_vertex_id] = {(f32) tangent[0], (f32) tangent[1], (f32) tangent[2]};
                    ++cur_vertex_id;
                }
            }
        }
        else if (fbx_tangents->GetMappingMode() == fbxsdk::FbxGeometryElement::eByControlPoint)
        {
            tangents.resize(vertex_count);
            for (u32 vertex_id = 0; vertex_id < vertex_count; ++vertex_id)
            {
                u32 tangent_id = 0;
                if (fbx_tangents->GetReferenceMode() == fbxsdk::FbxGeometryElement::eDirect)
                    tangent_id = vertex_id;
                else if (fbx_tangents->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndexToDirect)
                    tangent_id = fbx_tangents->GetIndexArray().GetAt(vertex_id);
                auto tangent = fbx_tangents->GetDirectArray().GetAt(tangent_id);
                tangents[vertex_id] = {(f32) tangent[0], (f32) tangent[1], (f32) tangent[2]};
            }
        }
        return true;
    }

    bool FbxParser::CalculateTangant(Mesh *mesh)
    {
        auto pos = mesh->GetVertices();
        auto uv0 = mesh->GetUVs(0);
        Vector<Vector4f> tangents(mesh->GetVertexCount());
        Vector<Vector4f> bitangents(mesh->GetVertexCount());
        for (u16 i = 0; i < mesh->SubmeshCount(); i++)
        {
            auto indices = mesh->GetIndices(i);
            for (size_t j = 0; j < mesh->GetIndicesCount(i); j += 3)
            {
                Vector4f tangent, bitangent;
                u32 index0 = indices[j], index1 = indices[j + 1], index2 = indices[j + 2];
                const Vector3f &v0 = pos[index0], v1 = pos[index1], v2 = pos[index2];
                const Vector2f &t0 = uv0[index0], t1 = uv0[index1], t2 = uv0[index2];
                Vector3f edge1 = v1 - v0;
                Vector3f edge2 = v2 - v0;
                Vector2f deltaUV1 = t1 - t0;
                Vector2f deltaUV2 = t2 - t0;
                f32 f = 1.0f / ((deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y) + 0.0000001f);
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

        auto normal = mesh->GetNormals();
        for (size_t i = 0; i < mesh->_vertex_count; i++)
        {
            const Vector3f &n = normal[i];
            const Vector3f &t = tangents[i].xyz;
            const Vector3f &b = bitangents[i].xyz;
            tangents[i].xyz = Normalize(t - n * DotProduct(n, t));
            tangents[i].w = (DotProduct(CrossProduct(n, t), b) < 0.0f) ? -1.0f : 1.0f;
        }
        mesh->SetTangents({tangents.data(), tangents.size()});
        return true;
    }

    void CheckQuantizedPositionCollision(
            const HashMap<u64, Vector<Vector3f>> &hash_pos_map,
            float eps = 1e-4f,
            float error_factor = 2.0f// >1 即可
    )
    {
        float eps2 = eps * eps * error_factor * error_factor;

        for (const auto &[hash, positions]: hash_pos_map)
        {
            if (positions.size() <= 1)
                continue;

            for (size_t i = 0; i < positions.size(); ++i)
            {
                for (size_t j = i + 1; j < positions.size(); ++j)
                {
                    Vector3f d = positions[i] - positions[j];
                    float dist2 = DotProduct(d, d);

                    if (dist2 > eps2)
                    {
                        LOG_WARNING(
                                "QuantizePosition collision detected: hash={} count={} dist={}",
                                hash,
                                positions.size(),
                                std::sqrt(dist2));

                        LOG_WARNING(
                                "  p0 = ({}, {}, {})",
                                positions[i].x, positions[i].y, positions[i].z);

                        LOG_WARNING(
                                "  p1 = ({}, {}, {})",
                                positions[j].x, positions[j].y, positions[j].z);

                        goto next_hash;// 一个 hash 报一次就够了
                    }
                }
            }

        next_hash:
            continue;
        }
    }

    void FbxParser::GenerateIndexdMesh(RawMeshData *mesh_data, Mesh *out_mesh)
    {
        if (!mesh_data)
        {
            LOG_ERROR("mesh_data is null");
            return;
        }
        u32 vertex_count = (u32) mesh_data->_positions.size();
        std::unordered_map<u64, u32> vertex_map{};
        std::vector<Vector3f> normals{};
        std::vector<Vector3f> positions{};
        std::vector<Vector2f> uv0s{};
        std::vector<u32> indices{};
        std::map<u16, Vector<u32>> submesh_indices{};
        u32 cur_index_count = 0u;
        auto &raw_normals = mesh_data->_normals;
        auto &raw_uv0 = mesh_data->_uvs[0];
        auto &raw_pos = mesh_data->_positions;
        //for skined mesh
        std::vector<Vector4D<u32>> bone_indices{};
        std::vector<Vector4f> bone_weights{};

        Math::ALHash::Vector3fHash v3hash{};
        Math::ALHash::Vector2fHash v2hash{};
        Math::ALHash::VectorHash<Vector4D, float> v4fhash{};
        Math::ALHash::VectorHash<Vector4D, u32> u4fhash{};
        TimeMgr mgr;
        mgr.Mark();
        constexpr float minf = std::numeric_limits<float>::lowest();
        constexpr float maxf = std::numeric_limits<float>::max();
        Vector3f vertex_min{maxf, maxf, maxf};
        Vector3f vertex_max{minf, minf, minf};
        Vector3f mesh_vmin = vertex_min;
        Vector3f mesh_vmax = vertex_max;
        Map<u32, std::tuple<Vector3f, Vector3f>> subemesh_aabbs;
        const auto process_vert = [&](u64 raw_vert_index) {};

        if (mesh_data->_bone_weights.size())
        {
            auto &raw_bonei = mesh_data->_bone_indices;
            auto &raw_bonew = mesh_data->_bone_weights;
            for (size_t i = 0; i < vertex_count; i++)
            {
                u32 submesh_index = _positon_material_index_mapper[i];
                auto p = raw_pos[i];
                auto n = raw_normals[_b_normal_by_controlpoint ? _positon_conrtol_index_mapper[i] : i];
                auto uv = raw_uv0[i];
                auto hash0 = v3hash(n), hash1 = v2hash(uv);
                auto vertex_hash = Math::ALHash::CombineHashes(hash0, hash1);
                vertex_hash = Math::ALHash::CombineHashes(vertex_hash, v3hash(p));
                auto bi = raw_bonei[i];
                auto bw = raw_bonew[i];
                vertex_hash = Math::ALHash::CombineHashes(vertex_hash, u4fhash(bi));
                vertex_hash = Math::ALHash::CombineHashes(vertex_hash, v4fhash(bw));
                auto it = vertex_map.find(vertex_hash);
                if (it == vertex_map.end())
                {
                    vertex_map[vertex_hash] = cur_index_count;
                    submesh_indices[submesh_index].emplace_back(cur_index_count);
                    indices.emplace_back(cur_index_count);
                    normals.emplace_back(n);
                    positions.emplace_back(p);
                    uv0s.emplace_back(uv);
                    bone_indices.emplace_back(bi);
                    bone_weights.emplace_back(bw);
                    if (subemesh_aabbs.contains(submesh_index))
                    {
                        auto &[exist_min, exist_max] = subemesh_aabbs[submesh_index];
                        exist_min = Min(exist_min, p);
                        exist_max = Max(exist_max, p);
                    }
                    else
                    {
                        subemesh_aabbs[submesh_index] = std::make_tuple(vertex_min, vertex_max);
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
        else
        {
            for (size_t i = 0; i < vertex_count; i++)
            {
                int submesh_index = _positon_material_index_mapper[i];
                auto p = raw_pos[i];
                //auto n = raw_normals[i];
                auto n = raw_normals[_b_normal_by_controlpoint ? _positon_conrtol_index_mapper[i] : i];
                auto uv = raw_uv0[i];
                auto hash0 = v3hash(n), hash1 = v2hash(uv);
                auto vertex_hash = Math::ALHash::CombineHashes(hash0, hash1);
                vertex_hash = Math::ALHash::CombineHashes(vertex_hash, v3hash(p));
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
                        auto &[exist_min, exist_max] = subemesh_aabbs[submesh_index];
                        exist_min = Min(exist_min, p);
                        exist_max = Max(exist_max, p);
                    }
                    else
                    {
                        subemesh_aabbs[submesh_index] = std::make_tuple(vertex_min, vertex_max);
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
        out_mesh->Clear();
        f32 aabb_space = 0.01f;
        out_mesh->_bounds.emplace_back(AABB(mesh_vmin, mesh_vmax));
        for (auto &it: subemesh_aabbs)
        {
            auto &[exist_min, exist_max] = it.second;
            mesh_vmin = Min(mesh_vmin, exist_min);
            mesh_vmax = Max(mesh_vmax, exist_max);
            out_mesh->_bounds.emplace_back(AABB(exist_min, exist_max));
        }
        out_mesh->_bounds[0] = AABB(mesh_vmin, mesh_vmax);
        for (auto &box: out_mesh->_bounds)
        {
            box._min -= aabb_space;
            box._max += aabb_space;
        }
        vertex_count = (u32) positions.size();
        out_mesh->_vertex_count = vertex_count;
        out_mesh->SetVertices({positions.data(), positions.size()});
        out_mesh->SetUVs({uv0s.data(), uv0s.size()}, 0u);
        out_mesh->SetNormals({normals.data(), normals.size()});

        for (int i = 0; i < submesh_indices.size(); ++i)
        {
            auto &submesh_indices_i = submesh_indices[i];
            out_mesh->AddSubmesh({submesh_indices_i.data(), submesh_indices_i.size()});
        }
        if (mesh_data->_bone_weights.size())
        {
            auto sk_mesh = dynamic_cast<SkeletonMesh *>(out_mesh);
            sk_mesh->SetBoneIndices({bone_indices.data(), bone_indices.size()});
            sk_mesh->SetBoneWeights({bone_weights.data(), bone_weights.size()});
        }
    }

    static Vector3f AxisToVector(int axis, int sign)
    {
        switch (axis)
        {
            case FbxAxisSystem::eXAxis:
                return Vector3f(sign, 0.0f, 0.0f);
            case FbxAxisSystem::eYAxis:
                return Vector3f(0.0f, sign, 0.0f);
            case FbxAxisSystem::eZAxis:
                return Vector3f(0.0f, 0.f, sign);
            default:
                return Vector3f::kZero;
        }
    }


    void FbxParser::ParserImpl(WString sys_path)
    {
        TimerBlock b("FbxParser::ParserImpl:  " + ToChar(sys_path.data()));
        _cur_file_sys_path = sys_path;
        String path = ToChar(sys_path.data());
        _p_cur_fbx_scene = FbxScene::Create(fbx_manager_, "RootScene");
        if (fbx_importer_ != nullptr && !fbx_importer_->Initialize(path.c_str(), -1, fbx_manager_->GetIOSettings()))
        {
            LOG_ERROR("Load mesh failed whit invalid path {}", path);
        }
        //_time_mgr.Mark();
        if (fbx_importer_->Import(_p_cur_fbx_scene))
        {
            FbxStatus status;
            FbxArray<FbxString *> details;
            FbxSceneCheckUtility sceneCheck(FbxCast<FbxScene>(_p_cur_fbx_scene), &status, &details);
            bool lNotify = (!sceneCheck.Validate(FbxSceneCheckUtility::eCkeckData) && details.GetCount() > 0) || (fbx_importer_->GetStatus().GetCode() != FbxStatus::eSuccess);
            if (lNotify)
            {
                //LOG_ERROR("********************************************************************************");
                //if (details.GetCount())
                //{
                //    LOG_ERROR("Scene integrity verification failed with the following errors:");
                //    for (int i = 0; i < details.GetCount(); i++)
                //        LOG_ERROR("   {}", String(details[i]->Buffer()));
                //    FbxArrayDelete<FbxString *>(details);
                //}
                //if (fbx_importer_->GetStatus().GetCode() != FbxStatus::eSuccess)
                //{
                //    LOG_ERROR("WARNING:");
                //    LOG_ERROR("   The importer was able to read the file but with errors.");
                //    LOG_ERROR("   Loaded scene may be incomplete.");
                //    String str(fbx_importer_->GetStatus().GetErrorString());
                //    LOG_ERROR("   Last error message: {}", str);
                //}
                //LOG_ERROR("********************************************************************************");
            }
            auto &global_settings = _p_cur_fbx_scene->GetGlobalSettings();
            FbxNode *fbx_rt = _p_cur_fbx_scene->GetRootNode();
            auto axis_sys = global_settings.GetAxisSystem();
            if (axis_sys != FbxAxisSystem::DirectX)
                FbxAxisSystem::DirectX.DeepConvertScene(_p_cur_fbx_scene);
            axis_sys = global_settings.GetAxisSystem();
            i32 up_sign = 0, front_sign = 0;
            FbxAxisSystem::EUpVector up_axis = axis_sys.GetUpVector(up_sign);
            FbxAxisSystem::EFrontVector front_axis = axis_sys.GetFrontVector(front_sign);
            FbxAxisSystem::ECoordSystem coord_system = axis_sys.GetCoorSystem();
            //EUpVector 1,2,3 xyz
            const static Vector3f kAxisDir[3] = {Vector3f::kRight, Vector3f::kUp, Vector3f::kForward};
            Vector3f up_vector = AxisToVector(up_axis, up_sign);
            Vector3f front_vector;
            if (up_axis == FbxAxisSystem::EUpVector::eXAxis)
                front_vector = FbxAxisSystem::EFrontVector::eParityEven ? Vector3f::kUp : Vector3f::kForward;
            else if (up_axis == FbxAxisSystem::EUpVector::eYAxis)
                front_vector = FbxAxisSystem::EFrontVector::eParityEven ? Vector3f::kRight : Vector3f::kForward;
            else//z up
                front_vector = FbxAxisSystem::EFrontVector::eParityEven ? Vector3f::kRight : Vector3f::kUp;
            Vector3f right_vector = coord_system == FbxAxisSystem::eRightHanded ? CrossProduct(front_vector, up_vector) : -CrossProduct(front_vector, up_vector);

            if (global_settings.GetSystemUnit() != fbxsdk::FbxSystemUnit::m)
            {
                const fbxsdk::FbxSystemUnit::ConversionOptions lConversionOptions = {
                        true,  /* mConvertRrsNodes */
                        true,  /* mConvertAllLimits */
                        true,  /* mConvertClusters */
                        true,  /* mConvertLightIntensity */
                        true,  /* mConvertPhotometricLProperties */
                        true   /* mConvertCameraClipPlanes */
                };
                fbxsdk::FbxSystemUnit::m.ConvertScene(_p_cur_fbx_scene, lConversionOptions);
            }
            FillPoseArray(_p_cur_fbx_scene, _fbx_poses);
            Queue<FbxNode *> mesh_node, skeleton_node;
            ParserFbxNode(fbx_rt, mesh_node, skeleton_node);
            _cur_skeleton.Clear();//当前仅支持一个骨骼，所以清空之前的数据
            _skeleton_asset.reset();
            _skeleton_bind_globals.clear();
            _skeleton_joint_nodes.clear();
            _loaded_anims.clear();
            _loaded_meshes.clear();
            //LOG_INFO("preprocess fbx scene cost {}ms",_time_mgr.GetElapsedSinceLastMark());
            const bool should_parse_skeleton = _import_setting.ShouldImportSkeleton() ||
                                               _import_setting.ShouldImportMesh() ||
                                               _import_setting.ShouldImportAnimation();
            while (should_parse_skeleton && !skeleton_node.empty())
            {
                //_time_mgr.Mark();
                ParserSkeleton(skeleton_node.front(), _cur_skeleton);
                //LOG_INFO("parser skeleton {} cost {}ms", skeleton_node.front()->GetName(), _time_mgr.GetElapsedSinceLastMark());
                skeleton_node.pop();
            }
            if (should_parse_skeleton)
            {
                BuildSkeletonBindPose(mesh_node, _cur_skeleton);
                if (_cur_skeleton.JointNum() > 0u)
                {
                    _skeleton_asset = MakeRef<SkeletonAsset>();
                    _skeleton_asset->Name(ToChar(PathUtils::GetFileName(_cur_file_sys_path, false)) + "_Skeleton");
                    _skeleton_asset->GetSkeletonMutable() = _cur_skeleton;
                    _skeleton_asset->Rebuild();
                }
            }
            if (_import_setting.ShouldImportAnimation() && _cur_skeleton.JointNum() > 0u)
                ParserAnimation(_cur_skeleton);
            while (!mesh_node.empty())
            {
                if (!_import_setting._mesh_name.empty() && !_import_setting._is_combine_mesh)
                {
                    auto node_name = String(mesh_node.front()->GetName());
                    if (node_name != _import_setting._mesh_name)
                    {
                        mesh_node.pop();
                        continue;
                    }
                }
                //_time_mgr.Mark();
                if (_import_setting.ShouldImportMesh())
                    ParserMesh(mesh_node.front(), _loaded_meshes);
                //LOG_INFO("parser mesh {} cost {}ms", mesh_node.front()->GetName(), _time_mgr.GetElapsedSinceLastMark());
                mesh_node.pop();
            }
            if (_import_setting._is_combine_mesh && _loaded_meshes.size() > 1)
            {
                bool has_skeleton_mesh = false;
                bool has_static_mesh = false;
                for (auto &mesh : _loaded_meshes)
                {
                    if (dynamic_cast<SkeletonMesh *>(mesh.get()))
                        has_skeleton_mesh = true;
                    else
                        has_static_mesh = true;
                }

                bool can_combine = !(has_skeleton_mesh && has_static_mesh);
                Matrix4x4f combined_mesh_bind_global = Matrix4x4f::Identity();
                if (has_skeleton_mesh && can_combine)
                {
                    auto *first_skeleton_mesh = dynamic_cast<SkeletonMesh *>(_loaded_meshes.front().get());
                    combined_mesh_bind_global = first_skeleton_mesh->GetMeshBindGlobalTransform();
                    for (auto &mesh : _loaded_meshes)
                    {
                        auto *skeleton_mesh = dynamic_cast<SkeletonMesh *>(mesh.get());
                        if (skeleton_mesh == nullptr ||
                            !(skeleton_mesh->GetMeshBindGlobalTransform() == combined_mesh_bind_global))
                        {
                            LOG_WARNING(L"Cannot combine skinned meshes from {} because their mesh bind transforms "
                                        L"differ",
                                        _cur_file_sys_path);
                            can_combine = false;
                            break;
                        }
                    }
                }
                else if (has_skeleton_mesh && has_static_mesh)
                {
                    LOG_WARNING(L"Cannot combine static and skinned meshes from {}", _cur_file_sys_path);
                }

                if (can_combine)
                {
                    Vector<Vector3f> positions;
                    Vector<Vector3f> normals;
                    Vector<Vector2f> uv0s;
                    Vector<Vector4f> tangents;
                    Vector<Vector4D<u32>> bone_indices;
                    Vector<Vector4f> bone_weights;
                    Ref<Mesh> combined_mesh = has_skeleton_mesh ?
                        std::static_pointer_cast<Mesh>(MakeRef<SkeletonMesh>(
                            ToChar(PathUtils::GetFileName(_cur_file_sys_path)))) :
                        MakeRef<Mesh>(ToChar(PathUtils::GetFileName(_cur_file_sys_path)));
                    u64 total_vertex_count = 0;
                    for (auto &mesh : _loaded_meshes)
                        total_vertex_count += mesh->GetVertices().size();
                    positions.resize(total_vertex_count);
                    normals.resize(total_vertex_count);
                    uv0s.resize(total_vertex_count);
                    tangents.resize(total_vertex_count);
                    if (has_skeleton_mesh)
                    {
                        bone_indices.resize(total_vertex_count);
                        bone_weights.resize(total_vertex_count);
                    }
                    u32 vertex_offset = 0;
                    constexpr float minf = std::numeric_limits<float>::lowest();
                    constexpr float maxf = std::numeric_limits<float>::max();
                    Vector3f vertex_min{maxf, maxf, maxf};
                    Vector3f vertex_max{minf, minf, minf};
                    AABB combined_aabb(vertex_min, vertex_max);
                    for (auto &mesh : _loaded_meshes)
                    {
                        const auto &mesh_positions = mesh->GetVertices();
                        const auto &mesh_normals = mesh->GetNormals();
                        const auto &mesh_uv0s = mesh->GetUVs(0);
                        const auto &mesh_tangents = mesh->GetTangents();
                        memcpy(&positions[vertex_offset], mesh_positions.data(), mesh_positions.size() * sizeof(Vector3f));
                        memcpy(&normals[vertex_offset], mesh_normals.data(),
                               mesh_normals.size() * sizeof(Vector3f));
                        memcpy(&uv0s[vertex_offset], mesh_uv0s.data(), mesh_uv0s.size() * sizeof(Vector2f));
                        memcpy(&tangents[vertex_offset], mesh_tangents.data(), mesh_tangents.size() * sizeof(Vector4f));
                        if (has_skeleton_mesh)
                        {
                            auto *skeleton_mesh = dynamic_cast<SkeletonMesh *>(mesh.get());
                            const auto &mesh_bone_indices = skeleton_mesh->GetBoneIndices();
                            const auto &mesh_bone_weights = skeleton_mesh->GetBoneWeights();
                            memcpy(&bone_indices[vertex_offset], mesh_bone_indices.data(),
                                   mesh_bone_indices.size() * sizeof(Vector4D<u32>));
                            memcpy(&bone_weights[vertex_offset], mesh_bone_weights.data(),
                                   mesh_bone_weights.size() * sizeof(Vector4f));
                        }
                        auto &imported_material_info = mesh->GetCacheMaterials();
                        for (u16 i = 0; i < mesh->SubmeshCount(); i++)
                        {
                            Vector<u32> combined_indices;
                            const auto &indices = mesh->GetIndices(i);
                            for (auto index : indices)
                                combined_indices.emplace_back(index + vertex_offset);
                            combined_mesh->AddSubmesh(combined_indices);
                            if (i < imported_material_info.size())
                                combined_mesh->AddCacheMaterial(imported_material_info[i]);
                            auto &cur_aabb = mesh->GetBoundBox(i + 1);
                            combined_mesh->_bounds.push_back(cur_aabb);
                            combined_aabb._max = Max(cur_aabb._max, combined_aabb._max);
                            combined_aabb._min = Min(cur_aabb._min, combined_aabb._min);
                        }
                        vertex_offset += (u32) mesh_positions.size();
                    }
                    combined_mesh->_bounds.insert(combined_mesh->_bounds.begin(), combined_aabb);
                    combined_mesh->SetVertices(std::move(positions));
                    combined_mesh->SetNormals(std::move(normals));
                    combined_mesh->SetUVs(std::move(uv0s), 0u);
                    combined_mesh->SetTangents(std::move(tangents));
                    if (has_skeleton_mesh)
                    {
                        auto *skeleton_mesh = dynamic_cast<SkeletonMesh *>(combined_mesh.get());
                        skeleton_mesh->SetSkeletonAsset(_skeleton_asset);
                        skeleton_mesh->SetMeshBindGlobalTransform(combined_mesh_bind_global);
                        skeleton_mesh->SetBoneIndices({bone_indices.data(), bone_indices.size()});
                        skeleton_mesh->SetBoneWeights({bone_weights.data(), bone_weights.size()});
                    }
                    LOG_INFO("combine {} mesh to one", _loaded_meshes.size());
                    _loaded_meshes.clear();
                    _loaded_meshes.emplace_back(combined_mesh);
                }
            }
            LOG_INFO(L"Fbx file {} parser done with {} mesh,{} skeleton,  {} animation", sys_path, _loaded_meshes.size(), _cur_skeleton.JointNum() > 0 ? 1 : 0, _loaded_anims.size());
            for (auto &it: _loaded_anims)
            {
                auto file_name = ToChar(PathUtils::GetFileName(sys_path));
                file_name.append("_").append(it->Name());
                it->Name(file_name);
                AnimationClipLibrary::AddClip(std::format("{}_raw", it->Name()), it);
            }
            if (!_import_setting.ShouldImportMesh())
            {
                _loaded_meshes.clear();
            }
            _fbx_poses.Clear();
            _fbx_cameras.Clear();
            _fbx_anim_stack_names.Clear();
        }
        else
        {
            LOG_ERROR(L"Import file {} failed!", sys_path)
        }
    }

    void FbxParser::FillCameraArray(FbxScene *pScene, FbxArray<FbxNode *> &pCameraArray)
    {
        pCameraArray.Clear();
        FillCameraArrayRecursive(pScene->GetRootNode(), pCameraArray);
    }

    void FbxParser::FillCameraArrayRecursive(FbxNode *pNode, FbxArray<FbxNode *> &pCameraArray)
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

    void FbxParser::FillPoseArray(FbxScene *pScene, FbxArray<FbxPose *> &pPoseArray)
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
        FbxAnimStack *lCurrentAnimationStack = _p_cur_fbx_scene->FindMember<FbxAnimStack>(_fbx_anim_stack_names[index]->Buffer());
        if (lCurrentAnimationStack == NULL)
        {
            // this is a problem. The anim stack should be found in the scene!
            return false;
        }
        // we assume that the first animation layer connected to the animation stack is the base layer
        // (this is the assumption made in the FBXSDK)
        _p_fbx_anim_layer = lCurrentAnimationStack->GetMember<FbxAnimLayer>();
        _p_cur_fbx_scene->SetCurrentAnimationStack(lCurrentAnimationStack);

        FbxTakeInfo *lCurrentTakeInfo = _p_cur_fbx_scene->GetTakeInfo(*(_fbx_anim_stack_names[index]));
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

    void FbxParser::ParserSceneNodeRecursive(FbxNode *pNode, FbxAnimLayer *pAnimLayer)
    {
        const int mat_count = pNode->GetMaterialCount();
        for (int i = 0; i < mat_count; i++)
        {
            FbxSurfaceMaterial *material = pNode->GetMaterial(i);
        }
        FbxNodeAttribute *node_attribute = pNode->GetNodeAttribute();
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
}// namespace Ailu
