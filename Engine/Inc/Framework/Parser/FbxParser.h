#pragma once
#ifndef __FPX_PARSER_H__
#define __FPX_PARSER_H__
#include "Ext/fbxsdk/include/fbxsdk.h"
#include "Framework/Interface/IParser.h"
#include "Framework/Common/TimeMgr.h"



namespace Ailu
{
	class FbxParser : public IMeshParser
	{
	public:
		inline static constexpr u16 kMaxInfluenceBoneNum = 4;
		FbxParser();
		List<Ref<Mesh>> Parser(std::string_view sys_path) final;
		List<Ref<Mesh>> Parser(const WString& sys_path) final;
		//temp test
		void Parser(std::string_view sys_path, struct Skeleton& sk, Vector<Vector<Matrix4x4f>>& anim);
		virtual ~FbxParser();
	private:
		//void ParserFbxNode(FbxNode* node, List<Ref<Mesh>>& loaded_meshes);
		void ParserFbxNode(FbxNode* node, Queue<FbxNode*>& mesh_node, Queue<FbxNode*>& skeleton_node);
		void ParserSkeleton(FbxNode* node,Skeleton& sk);
		bool ParserMesh(FbxNode* node, List<Ref<Mesh>>& loaded_meshes);
		void ParserAnimation(Queue<FbxNode*>& skeleton_node, Skeleton& sk);
		bool ReadNormal(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh);
		bool ReadVertex(fbxsdk::FbxNode* node, Mesh* mesh);
		bool ReadUVs(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh);
		bool ReadTangent(const fbxsdk::FbxMesh& fbx_mesh, Mesh* mesh);
		bool CalculateTangant(Mesh* mesh);
		void GenerateIndexdMesh(Mesh* mesh);

		List<Ref<Mesh>> ParserImpl(WString sys_path);

		FbxAMatrix GetGeometryTransformation(FbxNode* node);
	private:
		FbxScene* _p_cur_fbx_scene;
		Skeleton _cur_skeleton;
		FbxManager* fbx_manager_;
		FbxIOSettings* fbx_ios_;
		FbxImporter* fbx_importer_;
		inline static TimeMgr _time_mgr{};
	};
}


#endif // !FPX_PARSER_H__

