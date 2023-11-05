#include "pch.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/SceneActor.h"
#include "Render/Renderer.h"

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
	//StaticMeshComponent::StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat) : _p_mesh(mesh), _p_mat(mat)
	//{
	//}
	void StaticMeshComponent::BeginPlay()
	{

	}
	void StaticMeshComponent::Tick(const float& delta_time)
	{
		auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
		Renderer::Submit(_p_mesh, _p_mat,Transpose(transf.GetTransformMat()), 1);
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
		file << prop_indent <<"MaterialPath: " << _p_mat->OriginPath() << endl;
	}
	void* StaticMeshComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		formated_str.pop();
		auto mesh_path = TP_ONE(formated_str.front());
		formated_str.pop();
		auto mat_path = TP_ONE(formated_str.front());
		StaticMeshComponent* comp = new StaticMeshComponent(MeshPool::GetMesh(mesh_path), MaterialPool::GetMaterial(mat_path));
		formated_str.pop();
		return comp;
	}
}
