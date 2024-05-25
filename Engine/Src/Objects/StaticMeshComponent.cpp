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
	StaticMeshComponent::StaticMeshComponent() : StaticMeshComponent(nullptr, nullptr)
	{
		IMPLEMENT_REFLECT_FIELD(StaticMeshComponent)
	}
	StaticMeshComponent::StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat) : _p_mesh(mesh)
	{
		IMPLEMENT_REFLECT_FIELD(StaticMeshComponent);
		if (_p_mesh && _p_mats.size() != _p_mesh->SubmeshCount())
		{
			for (auto it = _p_mesh->GetCacheMaterials().begin(); it != _p_mesh->GetCacheMaterials().end(); it++)
			{
				
				auto loadded_mat = g_pResourceMgr->Get<Material>(ToWStr(it->_name.c_str()));
				if(loadded_mat)
					_p_mats.emplace_back(loadded_mat);
			}
			_transformed_aabbs = _p_mesh->_bound_boxs;
		}
		if (!_p_mats.empty() && _p_mats[0] == nullptr)
			_p_mats[0] = mat;
	}

	void StaticMeshComponent::BeginPlay()
	{

	}
	void StaticMeshComponent::Tick(const float& delta_time)
	{
		if (!_b_enable) return;
		if (!_p_mesh) return;
		for (int i = 0; i < _p_mesh->_bound_boxs.size(); i++)
		{
			_transformed_aabbs[i] = AABB::CaclulateBoundBox(_p_mesh->_bound_boxs[i], static_cast<SceneActor*>(_p_onwer)->GetTransformComponent()->GetMatrix());
		}
	}
	void StaticMeshComponent::OnGizmo()
	{
		for (auto& aabb : _transformed_aabbs)
		{
			Gizmo::DrawAABB(aabb, Colors::kGreen);
		}
	}
	void StaticMeshComponent::SetMesh(Ref<Mesh>& mesh)
	{
		_p_mesh = mesh;
		_transformed_aabbs.resize(_p_mesh->_bound_boxs.size());
	}

	void StaticMeshComponent::AddMaterial(Ref<Material> mat)
	{
		_p_mats.emplace_back(mat);
	}

	void StaticMeshComponent::AddMaterial(Ref<Material> mat, u16 slot)
	{
		_p_mats.insert(_p_mats.begin() + slot, mat);
	}

	void StaticMeshComponent::RemoveMaterial(u16 slot)
	{
		_p_mats.erase(_p_mats.begin() + slot);
	}

	void StaticMeshComponent::SetMaterial(Ref<Material>& mat, u16 slot)
	{
		if (slot < _p_mats.size())
			_p_mats[slot] = mat;
	}

	void StaticMeshComponent::Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent)
	{
		Component::Serialize(os, indent);
		using namespace std;
		String prop_indent = indent.append("  ");
		os << prop_indent << "MeshPath: " << (_p_mesh ? g_pResourceMgr->GetLinkedAsset(_p_mesh.get())->GetGuid().ToString() : String("Missing")) << endl;
		for(int i = 0; i < _p_mats.size(); ++i)
		{
			if (_p_mats[i] != nullptr && g_pResourceMgr->GetLinkedAsset(_p_mats[i].get()))
			{
				auto asset = g_pResourceMgr->GetLinkedAsset(_p_mats[i].get());
				AL_ASSERT(asset->_asset_type != EAssetType::kMaterial);
				os << prop_indent << std::format("MaterialPath{}: {}", i, asset->GetGuid().ToString()) << endl;
			}		
			else
				os << prop_indent << std::format("MaterialPath{}: {}", i, "missing") << endl;
		}
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
			Guid mesh_guid(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
			Ref<Mesh> mesh = nullptr;
			if (!g_pResourceMgr->GuidToAssetPath(mesh_guid).empty())
			{
				g_pResourceMgr->Load<Mesh>(mesh_guid);
				mesh = g_pResourceMgr->GetRef<Mesh>(mesh_guid);
			}
			if (!mesh)
			{
				g_pLogMgr->LogErrorFormat(std::source_location::current(), "Deserialize failed when load mesh with guid {};", mesh_guid.ToString());
			}

			//auto mat_path = TP_ONE(formated_str.front());
			Vector<String> mat_paths;
			String cur_mat_path;
			StaticMeshComponent* comp = new StaticMeshComponent(mesh, nullptr);
			while ((cur_mat_path = TP_ZERO(formated_str.front())).starts_with("MaterialPath"))
			{
				cur_mat_path = TP_ONE(formated_str.front());
				auto guid = Guid(cur_mat_path);
				g_pResourceMgr->Load<Material>(guid);
				auto mat = g_pResourceMgr->GetRef<Material>(guid);
				if (mat == nullptr)
				{
					g_pLogMgr->LogErrorFormat(std::source_location::current(), "Load material failed with id {};", cur_mat_path);
					mat = g_pResourceMgr->GetRef<Material>(L"Runtime/Material/StandardLit");	
				}
				else
					comp->AddMaterial(mat);
				formated_str.pop();
			}
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
			Guid mesh_guid(TP_ONE(formated_str.front()).c_str());
			formated_str.pop();
			auto mat_path = TP_ONE(formated_str.front());
			Ref<Mesh> mesh = nullptr;
			if (!g_pResourceMgr->GuidToAssetPath(mesh_guid).empty())
			{
				g_pResourceMgr->Load<Mesh>(mesh_guid);
				mesh = g_pResourceMgr->GetRef<Mesh>(mesh_guid);
			}
			if (!mesh)
			{
				g_pLogMgr->LogErrorFormat(std::source_location::current(), "Deserialize failed when load mesh with guid {};", mesh_guid.ToString());
			}
			//auto mat = MaterialLibrary::GetMaterial(mat_path);
			auto guid = Guid(mat_path);
			g_pResourceMgr->Load<Material>(guid);
			auto mat = g_pResourceMgr->GetRef<Material>(guid);
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
		Gizmo::DrawLine(origin, cur, Random::RandomColor32(j._parent));
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
				_skin_tasks[i] = g_pThreadTool->Enqueue(&SkinedMeshComponent::SkinTask, this, _p_clip.get(), utime, vert, startIdx, endIdx);
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