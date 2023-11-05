#pragma once
#ifndef __FPX_PARSER_H__
#define __FPX_PARSER_H__
#include "Framework/Interface/IParser.h"
#include "Ext/fbxsdk/include/fbxsdk.h"
#include "Framework/Assets/Mesh.h"
#include "Framework/Common/TimeMgr.h"


namespace Ailu
{
	class FbxParser : public IMeshParser
	{
	public:
		FbxParser();
		List<Ref<Mesh>> Parser(std::string_view sys_path) final;
		virtual ~FbxParser();
	private:
		bool GenerateMesh(Ref<Mesh>& mesh, fbxsdk::FbxMesh* fbx_mesh);
		bool ReadNormal(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh);
		bool ReadVertex(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh);
		bool ReadUVs(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh);
		bool ReadTangent(const fbxsdk::FbxMesh& fbx_mesh, Ref<Mesh>& mesh);
		bool CalculateTangant(Ref<Mesh>& mesh);
		void GenerateIndexdMesh(Ref<Mesh>& mesh);
	private:
		FbxManager* fbx_manager_;
		FbxIOSettings* fbx_ios_;
		FbxImporter* fbx_importer_;
		inline static TimeMgr _time_mgr{};
	};
}


#endif // !FPX_PARSER_H__

