#include "pch.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/SceneActor.h"

#include "Framework/Common/LogMgr.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsContext.h"
#include "Framework/Math/Random.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
	StaticMeshComponent::StaticMeshComponent() : _p_mesh(nullptr), _p_mat(nullptr)
	{
		IMPLEMENT_REFLECT_FIELD(StaticMeshComponent)
	}
	StaticMeshComponent::StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat) : _p_mesh(mesh), _p_mat(mat)
	{
		IMPLEMENT_REFLECT_FIELD(StaticMeshComponent)
	}

	void StaticMeshComponent::BeginPlay()
	{

	}
	void StaticMeshComponent::Tick(const float& delta_time)
	{
		if (!_b_enable) return;
		if (!_p_mesh) return;
		_aabb = AABB::CaclulateBoundBox(_p_mesh->_bound_box, static_cast<SceneActor*>(_p_onwer)->GetTransformComponent()->GetMatrix());
	}
	void StaticMeshComponent::OnGizmo()
	{
		Gizmo::DrawAABB(_aabb, Colors::kGreen);
	}
	void StaticMeshComponent::SetMesh(Ref<Mesh>& mesh)
	{
		_p_mesh = mesh;
	}

	void StaticMeshComponent::SetMaterial(Ref<Material>& mat)
	{
		_p_mat = mat;
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
				mesh_path = PathUtils::GetResSysPath(mesh_path);
				auto meshes = g_pResourceMgr->LoadMesh(ToWChar(mesh_path));
				if (!meshes.empty())
				{
					mesh = meshes.front();
					mesh->OriginPath(mesh_path);
					g_pGfxContext->SubmitRHIResourceBuildTask([=]() {mesh->BuildRHIResource(); });
				}
				else
				{
					g_pLogMgr->LogErrorFormat(std::source_location::current(), "Deserialize failed when load mesh with path {};", mesh_path);
				}
			}
			auto mat = MaterialLibrary::GetMaterial(mat_path);
			auto loc = std::source_location::current();
			if (mat == nullptr)
			{
				g_pLogMgr->LogErrorFormat(loc, "Load material failed with id {};", mat_path);
			}
			StaticMeshComponent* comp = new StaticMeshComponent(mesh, mat);
			formated_str.pop();
			return comp;
		}
	}

	//-------------------------------------------------------------------SkinedMeshComponent-------------------------------------------------------------------
	SkinedMeshComponent::SkinedMeshComponent() : StaticMeshComponent(nullptr, nullptr)
	{
		IMPLEMENT_REFLECT_FIELD(SkinedMeshComponent);
		_per_skin_task_vertex_num = 0;
	}
	SkinedMeshComponent::SkinedMeshComponent(Ref<Mesh> mesh, Ref<Material> mat) : StaticMeshComponent(mesh, mat)
	{
		IMPLEMENT_REFLECT_FIELD(SkinedMeshComponent);
		_per_skin_task_vertex_num = (mesh->_vertex_count + s_skin_thread_num - 1) / s_skin_thread_num;
		_skin_tasks.resize(s_skin_thread_num);
	}
	void* SkinedMeshComponent::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		if (formated_str.size() < 5)
		{
			g_pLogMgr->LogError("SkinedMeshComponent deserialize failed!");
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
				mesh_path = PathUtils::GetResSysPath(mesh_path);
				auto meshes = g_pResourceMgr->LoadMesh(ToWChar(mesh_path));
				if (!meshes.empty())
				{
					mesh = meshes.front();
					mesh->OriginPath(mesh_path);
					g_pGfxContext->SubmitRHIResourceBuildTask([=]() {mesh->BuildRHIResource(); });
				}
				else
				{
					g_pLogMgr->LogErrorFormat(std::source_location::current(), "Deserialize failed when load mesh with path {};", mesh_path);
				}
			}
			auto mat = MaterialLibrary::GetMaterial(mat_path);
			auto loc = std::source_location::current();
			if (mat == nullptr)
			{
				g_pLogMgr->LogErrorFormat(loc, "Load material failed with id {};", mat_path);
			}
			SkinedMeshComponent* comp = new SkinedMeshComponent(mesh, mat);
			formated_str.pop();
			auto anim_path = TP_ONE(formated_str.front());
			comp->_p_clip = AnimationClipLibrary::GetClip(anim_path);
			formated_str.pop();
			comp->_anim_time = LoadFloat(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
			return comp;
		}
	}

	void SkinedMeshComponent::BeginPlay()
	{
	}

	void SkinedMeshComponent::Tick(const float& delta_time)
	{
		if (!_b_enable) return;
		StaticMeshComponent::Tick(delta_time);
		if (_p_clip && _pre_anim_time != _anim_time && _p_mesh->_is_rhi_res_ready)
			Skin(_anim_time);
		_pre_anim_time = _anim_time;
	}

	static void DrawJoint(const Skeleton& sk, const Joint& j, AnimationClip* clip, u32 time, Vector3f origin, const Matrix4x4f& obj_world_mat)
	{
		auto cur = TransformCoord(clip->SampleDebug(j._self, time) * obj_world_mat, Vector3f::kZero);
		Gizmo::DrawLine(origin, cur, Random::RandomColor(j._parent));
		for (auto& cj : j._children)
		{
			DrawJoint(sk, sk[cj], clip, time, cur, obj_world_mat);
		}
	}

	void SkinedMeshComponent::OnGizmo()
	{
		if (!_p_clip || !_is_draw_debug_skeleton)
			return;
		auto transf = static_cast<SceneActor*>(_p_onwer)->GetTransformComponent();
		SkinedMesh* skined_mesh = dynamic_cast<SkinedMesh*>(_p_mesh.get());
		u32 utime = u32(_anim_time) % _p_clip->FrameCount();
		auto& sk = skined_mesh->CurSkeleton();
		DrawJoint(sk, sk[0], _p_clip.get(), utime, transf->GetPosition(), transf->GetMatrix());
	}

	void SkinedMeshComponent::Skin(float time)
	{
		//static Matrix4x4f mats[96];
		//u32 jc = 0;
		// for gpu skin
		//for (auto& j : joints)
		//{
		//	j._cur_pose = j._inv_bind_pos * j._cur_pose * j._node_inv_world_mat;
		//}
		//Shader::SetGlobalMatrixArray("_JointMatrix", mats, jc);
		SkinedMesh* skined_mesh = dynamic_cast<SkinedMesh*>(_p_mesh.get());
		u32 vert_count = skined_mesh->_vertex_count;
		u32 utime = u32(time) % _p_clip->FrameCount();
		auto vert = reinterpret_cast<Vector3f*>(skined_mesh->GetVertexBuffer()->GetStream(0));
		g_pTimeMgr->Mark();
		if (!_is_mt_skin)
		{
			SkinTask(_p_clip.get(), utime, vert, 0, vert_count);
		}
		else
		{
			for (u32 i = 0; i < s_skin_thread_num; ++i)
			{
				u32 startIdx = i * _per_skin_task_vertex_num;
				u32 endIdx = min((i + 1) * _per_skin_task_vertex_num, vert_count);
				_skin_tasks[i] = g_thread_pool->Enqueue(&SkinedMeshComponent::SkinTask, this, _p_clip.get(), utime, vert, startIdx, endIdx);
			}
			for (auto& future : _skin_tasks)
			{
				future.wait();
			}
		}
		_skin_time = g_pTimeMgr->GetElapsedSinceLastMark();
	}
	void SkinedMeshComponent::SkinTask(AnimationClip* clip, u32 time, Vector3f* vert, u32 begin, u32 end)
	{
		SkinedMesh* skined_mesh = dynamic_cast<SkinedMesh*>(_p_mesh.get());
		auto bone_weights = skined_mesh->GetBoneWeights();
		auto bone_indices = skined_mesh->GetBoneIndices();
		auto vertices = skined_mesh->GetVertices();
		for (u32 i = begin; i < end; i++)
		{
			auto& cur_weight = bone_weights[i];
			auto& cur_indices = bone_indices[i];
			Vector3f tmp = vertices[i];
			Vector3f new_pos = Vector3f::kZero;
			for (u16 j = 0; j < 4; j++)
			{
				new_pos += cur_weight[j] * TransformCoord(clip->Sample(cur_indices[j], time), tmp);
			}
			vert[i] = new_pos;
		}
	}
	void SkinedMeshComponent::SetMesh(Ref<Mesh>& mesh)
	{
		_p_mesh = mesh;
		_per_skin_task_vertex_num = (mesh->_vertex_count + s_skin_thread_num - 1) / s_skin_thread_num;
		_skin_tasks.clear();
		_skin_tasks.resize(s_skin_thread_num);
		auto skined_mesh = dynamic_cast<SkinedMesh*>(_p_mesh.get());
		if (_p_clip && skined_mesh->CurSkeleton() != _p_clip->CurSkeletion())
		{
			_p_clip.reset();
			_anim_time = 0.0f;
			_pre_anim_time = 0.0f;
		}
	}
	void SkinedMeshComponent::Serialize(std::ostream& os, String indent)
	{
		StaticMeshComponent::Serialize(os, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		os << prop_indent << "AnimationAlip: " << (_p_clip ? _p_clip->OriginPath() : "missing") << endl;
		os << prop_indent << "AnimationTime: " << _anim_time << endl;
	}
	//-------------------------------------------------------------------SkinedMeshComponent-------------------------------------------------------------------
}