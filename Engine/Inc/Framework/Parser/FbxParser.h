#pragma once
#ifndef __FPX_PARSER_H__
#define __FPX_PARSER_H__
#include "Animation/Clip.h"
#include "Ext/fbxsdk/include/fbxsdk.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Interface/IParser.h"


namespace Ailu
{
    using Render::Mesh;
    class FbxParser : public IMeshParser
    {
    public:
        inline static constexpr u16 kMaxInfluenceBoneNum = 4;
        FbxParser();
        virtual ~FbxParser();
        void Parser(const WString &sys_path, const MeshImportSetting &import_setting) final;
        const List<Ref<AnimationClip>> &GetAnimationClips() const final { return _loaded_anims; }
        void GetMeshes(List<Ref<Mesh>> &out_mesh) final
        {
            out_mesh = std::move(_loaded_meshes);
        }

    private:
        struct RawMeshData
        {
            Vector<Vector3f> _positions;
            Vector<Vector3f> _normals;
            Vector<Vector3f> _tangents;
            Vector<Vector<Vector2f>> _uvs;
            Vector<Vector4f> _bone_weights;
            Vector<Vector4D<u32>> _bone_indices;
        };
        void ParserImpl(WString sys_path);

        void ParserFbxNode(FbxNode *node, Queue<FbxNode *> &mesh_node, Queue<FbxNode *> &skeleton_node);
        void ParserSkeleton(FbxNode *node, Skeleton &sk);
        bool ParserMesh(FbxNode *node, List<Ref<Mesh>> &loaded_meshes);
        bool ParserAnimation(FbxNode *node, Skeleton &sk);
        bool ReadNormal(const fbxsdk::FbxMesh &fbx_mesh, Vector<Vector3f> &normals);
        bool ReadVertex(fbxsdk::FbxNode *node, Vector<Vector3f>& positions, Vector<Vector4f>& weights, Vector<Vector4D<u32>>& bone_indices);
        bool ReadUVs(const fbxsdk::FbxMesh &fbx_mesh, Vector<Vector<Vector2f>> &uvs);
        bool ReadTangent(const fbxsdk::FbxMesh &fbx_mesh, Vector<Vector3f>& tangents);
        bool CalculateTangant(Mesh *mesh);
        void GenerateIndexdMesh(Mesh *mesh);
        void GenerateIndexdMesh(RawMeshData* mesh_data,Mesh *out_mesh);
        void FillCameraArray(FbxScene *pScene, FbxArray<FbxNode *> &pCameraArray);
        void FillCameraArrayRecursive(FbxNode *pNode, FbxArray<FbxNode *> &pCameraArray);
        void FillPoseArray(FbxScene *pScene, FbxArray<FbxPose *> &pPoseArray);
        bool SetCurrentAnimStack(u16 index);

        void ParserSceneNodeRecursive(FbxNode *pNode, FbxAnimLayer *pAnimLayer);

    private:
        FbxScene *_p_cur_fbx_scene = nullptr;
        FbxManager *fbx_manager_ = nullptr;
        FbxIOSettings *fbx_ios_ = nullptr;
        FbxImporter *fbx_importer_ = nullptr;
        FbxAnimLayer *_p_fbx_anim_layer = nullptr;
        FbxArray<FbxPose *> _fbx_poses;
        FbxArray<FbxNode *> _fbx_cameras;
        FbxArray<FbxString *> _fbx_anim_stack_names;

        std::mutex _parser_lock;
        List<Ref<AnimationClip>> _loaded_anims;
        List<Ref<Mesh>> _loaded_meshes;
        WString _cur_file_sys_path;
        Skeleton _cur_skeleton;
        MeshImportSetting _import_setting;
        //Record the location and its corresponding control point index,
        //when the normal mapping method is control point, we need to get the normal based on this information to generate the index mesh
        Vector<u32> _positon_conrtol_index_mapper;
        Vector<u32> _positon_material_index_mapper;
        bool _b_normal_by_controlpoint = false;
        FbxTime _start_time;
        FbxTime _end_time;
        FbxAMatrix _cur_node_transform;

        inline static TimeMgr _time_mgr{};
    };
}// namespace Ailu


#endif// !FPX_PARSER_H__
