#pragma once
#ifndef __FPX_PARSER_H__
#define __FPX_PARSER_H__
#include "Ext/fbxsdk/include/fbxsdk.h"
#include "Framework/Interface/IParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Animation/Clip.h"


namespace Ailu
{
	class FbxParser : public IMeshParser
	{
	public:
		inline static constexpr u16 kMaxInfluenceBoneNum = 4;
		FbxParser();
		List<Ref<Mesh>> Parser(std::string_view sys_path) final;
		List<Ref<Mesh>> Parser(const WString& sys_path) final;
		const List<std::pair<String, u8>>& GetTextureInfos() const { return _loaded_textures; }
		virtual ~FbxParser();
	private:
		//void ParserFbxNode(FbxNode* node, List<Ref<Mesh>>& loaded_meshes);
		void ParserFbxNode(FbxNode* node, Queue<FbxNode*>& mesh_node, Queue<FbxNode*>& skeleton_node);
		void ParserSkeleton(FbxNode* node,Skeleton& sk);
		bool ParserMesh(FbxNode* node, List<Ref<Mesh>>& loaded_meshes);
		bool ParserAnimation(FbxNode* node, Skeleton& sk);
		bool ReadNormal(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh);
		bool ReadVertex(fbxsdk::FbxNode* node, Mesh* mesh);
		bool ReadUVs(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh);
		bool ReadTangent(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh);
		bool CalculateTangant(Mesh* mesh);
		void GenerateIndexdMesh(Mesh* mesh);

		List<Ref<Mesh>> ParserImpl(WString sys_path);
		List<Ref<Mesh>> ParserImpl(WString sys_path,bool ph);

		void FillCameraArray(FbxScene* pScene, FbxArray<FbxNode*>& pCameraArray);
		void FillCameraArrayRecursive(FbxNode* pNode, FbxArray<FbxNode*>& pCameraArray);
		void FillPoseArray(FbxScene* pScene, FbxArray<FbxPose*>& pPoseArray);
		bool SetCurrentAnimStack(u16 index);

		void ParserSceneNodeRecursive(FbxNode* pNode, FbxAnimLayer* pAnimLayer);
	private:
		FbxScene* _p_cur_fbx_scene;
		Skeleton _cur_skeleton;
		FbxManager* fbx_manager_;
		FbxIOSettings* fbx_ios_;
		FbxImporter* fbx_importer_;

		FbxArray<FbxPose*> _fbx_poses;
		FbxArray<FbxNode*> _fbx_cameras;

		List<std::pair<String, u8>> _loaded_textures;
		WString _cur_file_sys_path;
		//Record the location and its corresponding control point index, 
		//when the normal mapping method is control point, we need to get the normal based on this information to generate the index mesh
		Vector<u32> _positon_conrtol_index_mapper;
		Vector<u32> _positon_material_index_mapper;
		bool _b_normal_by_controlpoint = false;
		List<AnimationClip*> _loaded_anims;
		FbxArray<FbxString*> _fbx_anim_stack_names;
		FbxAnimLayer* _p_fbx_anim_layer; 
		FbxTime _start_time;
		FbxTime _end_time;

		inline static TimeMgr _time_mgr{};
	};
}


#endif // !FPX_PARSER_H__

