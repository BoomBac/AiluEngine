#include "pch.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/SceneActor.h"
#include "Render/Renderer.h"

#include "Framework/Common/LogMgr.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Parser/AssetParser.h"
#include "Render/Gizmo.h"

namespace Ailu
{
	StaticMeshComponent::StaticMeshComponent() : _p_mesh(nullptr), _p_mat(nullptr)
	{
		IMPLEMENT_REFLECT_FIELD(StaticMeshComponent)
	}
	StaticMeshComponent::StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat) : _p_mesh(mesh) , _p_mat(mat)
	{
		IMPLEMENT_REFLECT_FIELD(StaticMeshComponent)
	}

	void StaticMeshComponent::BeginPlay()
	{

	}
	void StaticMeshComponent::Tick(const float& delta_time)
	{
		auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
		_aabb = AABB::CaclulateBoundBox(_p_mesh->_bound_box, Transform::GetWorldMatrix(transf));
	}
	void StaticMeshComponent::OnGizmo()
	{
		Gizmo::DrawAABB(_aabb,Colors::kGreen);
	}
	void StaticMeshComponent::SetMesh(Ref<Mesh>& mesh)
	{
		_p_mesh = mesh;
	}

	void StaticMeshComponent::SetMaterial(Ref<Material>& mat)
	{
		_p_mat = mat;
	}

	void Ailu::StaticMeshComponent::Serialize(std::ofstream& file, String indent)
	{
		Component::Serialize(file,indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		file << prop_indent <<"MeshPath: " << _p_mesh->OriginPath() << endl;
		if(_p_mat)
			file << prop_indent <<"MaterialPath: " << _p_mat->OriginPath() << endl;
		else
			file << prop_indent << "MaterialPath: " << "missing" << endl;
	}
	void StaticMeshComponent::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		Component::Serialize(os, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		os << prop_indent << "MeshPath: " << _p_mesh->OriginPath() << endl;
		if (_p_mat)
			os << prop_indent << "MaterialPath: " << _p_mat->OriginPath() << endl;
		else
			os << prop_indent << "MaterialPath: " << "missing" << endl;
	}
	void* StaticMeshComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		if (formated_str.size() < 3)
		{
			g_pLogMgr->LogError("StaticMeshComponent deserialize failed!");
			return nullptr;
		}
		else
		{
			formated_str.pop();
			auto mesh_path = TP_ONE(formated_str.front());
			formated_str.pop();
			auto mat_path = TP_ONE(formated_str.front());
			auto mesh = MeshPool::GetMesh(mesh_path);
			if (mesh == nullptr)
			{
				auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
				mesh = parser->Parser(mesh_path).front();
				if(mesh != nullptr) MeshPool::AddMesh(mesh);
			}
			auto mat = MaterialLibrary::GetMaterial(mat_path);
			auto loc = std::source_location::current();
			if (mat == nullptr)
			{
				g_pLogMgr->LogErrorFormat(loc,"Load material failed with id {};",mat_path);
			}
			StaticMeshComponent* comp = new StaticMeshComponent(mesh, mat);
			formated_str.pop();
			return comp;
		}
	}
}
