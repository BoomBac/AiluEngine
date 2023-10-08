#include "pch.h"
#include "Objects/StaticMeshComponent.h"
#include "Render/Renderer.h"

namespace Ailu
{
	StaticMeshComponent::StaticMeshComponent(Ref<Mesh>& mesh, Ref<Material>& mat) : _p_mesh(mesh) , _p_mat(mat)
	{
		SerializableProperty prop{ nullptr,_p_mesh->Name(),"StaticMesh" };
		properties.insert(std::make_pair(prop._name, prop));
	}
	void StaticMeshComponent::BeginPlay()
	{

	}
	void StaticMeshComponent::Tick(const float& delta_time)
	{
		//auto& transf = static_cast<SceneActor*>(_p_onwer)->GetTransform();
		//Renderer::Submit(_p_mesh, _p_mat,Transpose(transf.GetTransformMat()), 1);
	}
	void StaticMeshComponent::SetMesh(Ref<Mesh>& mesh)
	{
		_p_mesh = mesh;
	}
	void StaticMeshComponent::SetMaterial(Ref<Material>& mat)
	{
		_p_mat = mat;
	}
}
