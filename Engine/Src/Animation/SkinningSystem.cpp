#include "Animation/SkinningSystem.h"
#include "Framework/Common/EngineConfig.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Mesh.h"
#include "Render/RenderConstants.h"
#include "Render/RenderGraph/RenderGraph.h"

#include <algorithm>

namespace Ailu::ECS
{
    namespace
    {
        constexpr i32 kInvalidBindlessIndex = -1;

        void SkinTask(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette, Vector3f *vertices,
                      Vector3f *normals, Vector4f *tangents, u32 begin, u32 end)
        {
            PROFILE_BLOCK_CPU("SkinTask")
            const auto bone_weights = mesh->GetBoneWeights();
            const auto bone_indices = mesh->GetBoneIndices();
            // The CPU output stream can alias _vertices before the asynchronous GPU upload completes.
            // Keep the bind-pose source isolated from the stream we overwrite.
            const auto source_vertices = mesh->GetPreviousVertices();
            const auto source_normals = mesh->GetNormals();
            const auto source_tangents = mesh->GetTangents();
            for (u32 index = begin; index < end; ++index)
            {
                if (index >= bone_weights.size() || index >= bone_indices.size() || index >= source_vertices.size())
                    continue;

                Vector3f skinned_position = Vector3f::kZero;
                Vector3f skinned_normal = Vector3f::kZero;
                Vector3f skinned_tangent = Vector3f::kZero;
                f32 valid_weight = 0.0f;
                const auto &weights = bone_weights[index];
                const auto &indices = bone_indices[index];
                for (u16 influence = 0u; influence < 4u; ++influence)
                {
                    if (weights[influence] <= 0.0f)
                        continue;
                    const u16 joint_index = indices[influence];
                    if (joint_index >= pose_palette.size())
                        continue;
                    skinned_position +=
                        weights[influence] * TransformCoord(pose_palette[joint_index], source_vertices[index]);
                    if (normals != nullptr && index < source_normals.size())
                        skinned_normal += weights[influence] *
                                          TransformNormal(pose_palette[joint_index], source_normals[index]);
                    if (tangents != nullptr && index < source_tangents.size())
                        skinned_tangent += weights[influence] *
                                           TransformNormal(pose_palette[joint_index], source_tangents[index].xyz);
                    valid_weight += weights[influence];
                }
                vertices[index] = valid_weight > Math::kFloatEpsilon ? skinned_position : source_vertices[index];
                if (normals != nullptr && index < source_normals.size())
                    normals[index] = valid_weight > Math::kFloatEpsilon ? Normalize(skinned_normal) :
                                                                            source_normals[index];
                if (tangents != nullptr && index < source_tangents.size())
                {
                    Vector4f tangent = source_tangents[index];
                    if (valid_weight > Math::kFloatEpsilon)
                    {
                        const Vector3f normalized_tangent = Normalize(skinned_tangent);
                        tangent.x = normalized_tangent.x;
                        tangent.y = normalized_tangent.y;
                        tangent.z = normalized_tangent.z;
                    }
                    tangents[index] = tangent;
                }
            }
        }
    }

    bool SkinningSystem::BeginFrame()
    {
        Clear();
        ++_frame_index;
        _jobs.clear();
        _job_handles.clear();
        _palette_data.clear();
        _pose_offsets.clear();
        _entity_poses.clear();
        _entity_handles.clear();
        _output_handles.clear();
        _recorded = false;
        _gpu_enabled = g_engine_config._enable_compute_skinning && EnsureComputeShader();
        s_active_system = this;

        for (auto it = _cache.begin(); it != _cache.end();)
        {
            if (_frame_index > it->second._last_used_frame + kCacheKeepFrames)
            {
                LOG_INFO("Compute skin cache evict: frame={}, mesh_id={}, pose_id={}, skin_id={}, last_used_frame={}",
                         _frame_index, it->first._mesh_id, it->first._pose_id, it->second._handle._id,
                         it->second._last_used_frame);
                _handles.erase(it->second._handle._id);
                it = _cache.erase(it);
            }
            else
                ++it;
        }
        return _gpu_enabled;
    }

    PoseHandle SkinningSystem::CreatePoseHandle()
    {
        return PoseHandle{_next_pose_id++};
    }

    void SkinningSystem::UploadPose(PoseHandle handle, std::span<const Matrix4x4f> pose_palette)
    {
        if (!_gpu_enabled || !handle.IsValid() || pose_palette.empty())
            return;
        if (pose_palette.size() > kMaxPaletteBones || _palette_data.size() + pose_palette.size() > kMaxPaletteBones)
        {
            LOG_WARNING("Compute skinning palette is too large: {} bones", pose_palette.size());
            return;
        }
        const u32 offset = static_cast<u32>(_palette_data.size());
        _pose_offsets[handle._id] = offset;
        _palette_data.insert(_palette_data.end(), pose_palette.begin(), pose_palette.end());
    }

    void SkinningSystem::SetEntityPose(u32 entity, PoseHandle pose_handle)
    {
        if (pose_handle.IsValid())
            _entity_poses[entity] = pose_handle;
    }

    Ref<Render::GPUBuffer> SkinningSystem::CreateOutputBuffer(u32 vertex_count, u32 element_size, const String &name)
    {
        Render::BufferDesc desc;
        desc._element_num = vertex_count;
        desc._element_size = element_size;
        desc._size = vertex_count * element_size;
        desc._target = static_cast<Render::EGPUBufferTarget>(Render::kVertex | Render::kStructured);
        desc._is_create_srv = false;
        desc._is_create_uav = true;
        desc._is_random_write = true;
        return Render::GPUBuffer::Create(desc, name);
    }

    SkinningSystem::SkinCacheEntry *SkinningSystem::FindEntry(SkinHandle handle)
    {
        const auto handle_it = _handles.find(handle._id);
        if (handle_it == _handles.end())
            return nullptr;
        const auto cache_it = _cache.find(handle_it->second);
        return cache_it == _cache.end() ? nullptr : &cache_it->second;
    }

    const SkinningSystem::SkinCacheEntry *SkinningSystem::FindEntry(SkinHandle handle) const
    {
        const auto handle_it = _handles.find(handle._id);
        if (handle_it == _handles.end())
            return nullptr;
        const auto cache_it = _cache.find(handle_it->second);
        return cache_it == _cache.end() ? nullptr : &cache_it->second;
    }

    SkinningSystem::SkinCacheEntry *SkinningSystem::CreateEntry(Render::SkeletonMesh *mesh, PoseHandle pose_handle,
                                                                  SkinCacheKey key)
    {
        (void)pose_handle;
        auto source = mesh != nullptr ? mesh->GetVertexBuffer().get() : nullptr;
        if (source == nullptr || mesh->GetVertexCount() == 0u)
            return nullptr;

        const u32 vertex_count = mesh->GetVertexCount();
        auto entry = SkinCacheEntry{};
        entry._mesh = mesh;
        entry._vertex_count = vertex_count;
        entry._last_used_frame = _frame_index;
        entry._handle = SkinHandle{_next_skin_id++};
        entry._position_buffer = CreateOutputBuffer(vertex_count, sizeof(Vector3f),
                                                     std::format("{}_SkinPosition_{}", mesh->Name(), entry._handle._id));
        if (mesh->GetNormalStream() >= 0)
            entry._normal_buffer = CreateOutputBuffer(vertex_count, sizeof(Vector3f),
                                                      std::format("{}_SkinNormal_{}", mesh->Name(), entry._handle._id));
        if (mesh->GetTangentStream() >= 0)
            entry._tangent_buffer = CreateOutputBuffer(vertex_count, sizeof(Vector4f),
                                                       std::format("{}_SkinTangent_{}", mesh->Name(), entry._handle._id));
        if (entry._position_buffer == nullptr ||
            (mesh->GetNormalStream() >= 0 && entry._normal_buffer == nullptr) ||
            (mesh->GetTangentStream() >= 0 && entry._tangent_buffer == nullptr))
            return nullptr;

        entry._vertex_buffer.reset(Render::VertexBuffer::Create(source->GetLayout(),
                                                                  std::format("{}_SkinOutput_{}", mesh->Name(),
                                                                              entry._handle._id)));
        if (entry._vertex_buffer == nullptr)
            return nullptr;

        for (u8 stream = 0u; stream < source->GetLayout().GetStreamCount(); ++stream)
        {
            const u32 stream_size = vertex_count * source->GetLayout().GetStride(stream);
            const auto &layout = source->GetLayout();
            const auto &desc = layout.GetBufferDesc();
            const auto desc_it = std::find_if(desc.begin(), desc.end(), [stream](const auto &element)
                                              { return element.Stream == stream; });
            if (desc_it == desc.end())
                continue;

            if (desc_it->_semantic == Render::EVertexSemantic::kPosition)
                entry._vertex_buffer->SetGpuStream(entry._position_buffer.get(), stream_size, stream);
            else if (desc_it->_semantic == Render::EVertexSemantic::kNormal && entry._normal_buffer != nullptr)
                entry._vertex_buffer->SetGpuStream(entry._normal_buffer.get(), stream_size, stream);
            else if (desc_it->_semantic == Render::EVertexSemantic::kTangent && entry._tangent_buffer != nullptr)
                entry._vertex_buffer->SetGpuStream(entry._tangent_buffer.get(), stream_size, stream);
            else if (auto *gpu_stream = source->GetGpuStream(stream); gpu_stream != nullptr)
                entry._vertex_buffer->SetGpuStream(gpu_stream, stream_size, stream);
            else
                entry._vertex_buffer->SetStream(source->GetStream(stream), stream_size, stream, false);
        }
        Render::GraphicsContext::Get().CreateResource(entry._vertex_buffer.get());
        auto [cache_it, inserted] = _cache.emplace(key, std::move(entry));
        if (!inserted)
            return &cache_it->second;
        _handles[cache_it->second._handle._id] = key;
        return &cache_it->second;
    }

    SkinHandle SkinningSystem::Acquire(Render::SkeletonMesh *mesh, PoseHandle pose_handle, u32 entity)
    {
        if (!_gpu_enabled || mesh == nullptr || !pose_handle.IsValid() || _jobs.size() >= kMaxSkinningJobs)
            return {};
        const auto pose_it = _pose_offsets.find(pose_handle._id);
        if (pose_it == _pose_offsets.end())
            return {};

        const i32 position_srv = mesh->GetBindlessVertexStreamIndex(Render::EVertexSemantic::kPosition);
        const i32 normal_srv = mesh->GetBindlessVertexStreamIndex(Render::EVertexSemantic::kNormal);
        const i32 tangent_srv = mesh->GetBindlessVertexStreamIndex(Render::EVertexSemantic::kTangent);
        const i32 bone_index_srv = mesh->GetBindlessVertexStreamIndex(Render::EVertexSemantic::kBoneIndex);
        const i32 bone_weight_srv = mesh->GetBindlessVertexStreamIndex(Render::EVertexSemantic::kBoneWeight);
        if (position_srv == kInvalidBindlessIndex || bone_index_srv == kInvalidBindlessIndex ||
            bone_weight_srv == kInvalidBindlessIndex)
            return {};

        const auto key = SkinCacheKey{mesh->ID(), pose_handle._id};
        auto cache_it = _cache.find(key);
        if (cache_it == _cache.end())
        {
            LOG_INFO("Compute skin cache miss: frame={}, entity={}, mesh_id={}, pose_id={}, cache_size={}",
                     _frame_index, entity, key._mesh_id, key._pose_id, _cache.size());
        }
        SkinCacheEntry *entry = cache_it == _cache.end() ? CreateEntry(mesh, pose_handle, key) : &cache_it->second;
        if (entry == nullptr)
            return {};
        entry->_last_used_frame = _frame_index;
        _entity_handles[entity] = entry->_handle;
        if (std::find(_job_handles.begin(), _job_handles.end(), entry->_handle) != _job_handles.end())
            return entry->_handle;

        SkinningJobData job;
        job._position_srv = static_cast<u32>(position_srv);
        job._normal_srv = normal_srv >= 0 ? static_cast<u32>(normal_srv) : Render::RenderConstants::kInvalidBindlessHandle;
        job._tangent_srv = tangent_srv >= 0 ? static_cast<u32>(tangent_srv) : Render::RenderConstants::kInvalidBindlessHandle;
        job._bone_index_srv = static_cast<u32>(bone_index_srv);
        job._bone_weight_srv = static_cast<u32>(bone_weight_srv);
        job._position_uav = static_cast<u32>(entry->_position_buffer->GetBindlessUAVIndex());
        job._normal_uav = entry->_normal_buffer != nullptr ?
            static_cast<u32>(entry->_normal_buffer->GetBindlessUAVIndex()) : Render::RenderConstants::kInvalidBindlessHandle;
        job._tangent_uav = entry->_tangent_buffer != nullptr ?
            static_cast<u32>(entry->_tangent_buffer->GetBindlessUAVIndex()) : Render::RenderConstants::kInvalidBindlessHandle;
        job._palette_offset = pose_it->second;
        job._vertex_count = mesh->GetVertexCount();
        job._vertex_offset = 0u;
        if (job._position_uav == Render::RenderConstants::kInvalidBindlessHandle)
            return {};
        _jobs.emplace_back(job);
        _job_handles.emplace_back(entry->_handle);
        return entry->_handle;
    }

    void SkinningSystem::PrepareVisibleSkinning(const Render::CullResult &cull_results)
    {
        if (!_gpu_enabled)
            return;
        for (const auto &[queue, objects] : cull_results)
        {
            (void)queue;
            for (const auto &object : objects)
            {
                auto *mesh = dynamic_cast<Render::SkeletonMesh *>(object._mesh);
                const auto pose_it = _entity_poses.find(object._entity);
                if (mesh != nullptr && pose_it != _entity_poses.end())
                    Acquire(mesh, pose_it->second, object._entity);
            }
        }
    }

    void SkinningSystem::EnsureStructuredBuffers()
    {
        if (_palette_buffer == nullptr)
            _palette_buffer = Render::GPUBuffer::Create(Render::kStructured, sizeof(Matrix4x4f), kMaxPaletteBones,
                                                        "ComputeSkinning_Palette");
        if (_job_buffer == nullptr)
            _job_buffer = Render::GPUBuffer::Create(Render::kStructured, sizeof(SkinningJobData), kMaxSkinningJobs,
                                                    "ComputeSkinning_Jobs");
    }

    bool SkinningSystem::EnsureComputeShader()
    {
        if (_compute_shader == nullptr)
            _compute_shader = ResourceMgr::Get().Load<Render::ComputeShader>(
                L"Shaders/hlsl/Compute/compute_skinning.alasset");
        if (_compute_shader == nullptr)
            return false;
        if (!_compute_shader->IsKernelValid(_kernel))
            _kernel = _compute_shader->FindKernel("Skinning");
        return _compute_shader->IsKernelValid(_kernel);
    }

    void SkinningSystem::RecordRenderGraph(Render::RDG::RenderGraph &graph, Render::RenderingData &data)
    {
        (void)data;
        if (!_gpu_enabled || _recorded || _jobs.empty())
            return;
        EnsureStructuredBuffers();
        if (_palette_buffer == nullptr || _job_buffer == nullptr || _palette_data.empty())
            return;
        _palette_buffer->SetData(_palette_data);
        _job_buffer->SetData(_jobs);
        const auto palette_handle = graph.Import(_palette_buffer.get());
        const auto job_handle = graph.Import(_job_buffer.get());
        _output_handles.clear();
        graph.AddPass("ComputeSkinning", Render::RDG::PassDesc(Render::RDG::EPassType::kCompute),
                      [this, palette_handle, job_handle](Render::RDG::RenderGraphBuilder &builder)
                      {
                          builder.Read(palette_handle, Render::EResourceUsage::kReadSRV);
                          builder.Read(job_handle, Render::EResourceUsage::kReadSRV);
                          for (size_t i = 0u; i < _jobs.size(); ++i)
                          {
                              const auto *entry = FindEntry(_job_handles[i]);
                              if (entry == nullptr)
                                  continue;
                              auto *source_vertex_buffer = entry->_mesh->GetVertexBuffer().get();
                              builder.Read(builder.Import(source_vertex_buffer), Render::EResourceUsage::kReadSRV);
                              const u8 stream_count = source_vertex_buffer->GetLayout().GetStreamCount();
                              for (u8 stream = 0u; stream < stream_count; ++stream)
                              {
                                  if (auto *source_stream = source_vertex_buffer->GetGpuStream(stream);
                                      source_stream != nullptr)
                                      builder.Read(builder.Import(source_stream),
                                                   Render::EResourceUsage::kReadSRV);
                              }
                              const auto output_handle = builder.Write(builder.Import(entry->_position_buffer.get()),
                                                                        Render::EResourceUsage::kWriteUAV);
                              _output_handles.emplace_back(output_handle);
                              if (entry->_normal_buffer != nullptr)
                                  _output_handles.emplace_back(
                                      builder.Write(builder.Import(entry->_normal_buffer.get()),
                                                    Render::EResourceUsage::kWriteUAV));
                              if (entry->_tangent_buffer != nullptr)
                                  _output_handles.emplace_back(
                                      builder.Write(builder.Import(entry->_tangent_buffer.get()),
                                                    Render::EResourceUsage::kWriteUAV));
                          }
                      },
                      [this, palette_handle, job_handle](Render::RDG::RenderGraph &render_graph,
                                                         Render::CommandBuffer *cmd,
                                                         const Render::RenderingData &render_data)
                      {
                          (void)render_graph;
                          (void)render_data;
                          _compute_shader->SetBuffer("_bone_palette", _palette_buffer.get());
                          _compute_shader->SetBuffer("_skinning_jobs", _job_buffer.get());
                          const u32 max_vertices = std::max_element(_jobs.begin(), _jobs.end(),
                                                                     [](const auto &lhs, const auto &rhs)
                                                                     { return lhs._vertex_count < rhs._vertex_count; })->_vertex_count;
                          auto [group_x, group_y, group_z] = _compute_shader->CalculateDispatchNum(
                              _kernel, static_cast<u16>(std::min(max_vertices, 65535u)),
                              static_cast<u16>(_jobs.size()), 1u);
                          cmd->Dispatch(_compute_shader.get(), _kernel, group_x, group_y, group_z);
                          (void)palette_handle;
                          (void)job_handle;
                      });
        _recorded = true;
    }

    Render::VertexBuffer *SkinningSystem::GetVertexBuffer(SkinHandle handle) const
    {
        const auto *entry = FindEntry(handle);
        return entry != nullptr ? entry->_vertex_buffer.get() : nullptr;
    }

    Render::VertexBuffer *SkinningSystem::ResolveVertexBuffer(Render::Mesh *mesh, u32 entity)
    {
        if (s_active_system == nullptr || mesh == nullptr)
            return nullptr;
        const auto handle_it = s_active_system->_entity_handles.find(entity);
        if (handle_it == s_active_system->_entity_handles.end())
            return nullptr;
        const auto *entry = s_active_system->FindEntry(handle_it->second);
        return entry != nullptr && entry->_mesh == mesh ? entry->_vertex_buffer.get() : nullptr;
    }

    void SkinningSystem::Submit(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette,
                                u32 vertices_per_task)
    {
        if (mesh == nullptr || pose_palette.empty() || vertices_per_task == 0u)
            return;

        if (mesh->GetVertexBuffer() == nullptr)
            return;

        Vector3f *vertices = reinterpret_cast<Vector3f *>(mesh->GetVertexBuffer()->GetStream(0));
        Vector3f *normals = nullptr;
        Vector4f *tangents = nullptr;
        const i32 normal_stream = mesh->GetNormalStream();
        const i32 tangent_stream = mesh->GetTangentStream();
        if (normal_stream >= 0)
            normals = reinterpret_cast<Vector3f *>(
                mesh->GetVertexBuffer()->GetStream(static_cast<u8>(normal_stream)));
        if (tangent_stream >= 0)
            tangents = reinterpret_cast<Vector4f *>(
                mesh->GetVertexBuffer()->GetStream(static_cast<u8>(tangent_stream)));
        const u32 vertex_count = mesh->GetVertexCount();
        const u32 task_count = (vertex_count + vertices_per_task - 1u) / vertices_per_task;
        _tasks.reserve(_tasks.size() + task_count);
        for (u32 task_index = 0u; task_index < task_count; ++task_index)
        {
            const u32 begin = task_index * vertices_per_task;
            const u32 end = std::min(begin + vertices_per_task, vertex_count);
            auto wait_handle = JobSystem::Get().Dispatch(
                [mesh, pose_palette, vertices, normals, tangents, begin, end]()
                { SkinTask(mesh, pose_palette, vertices, normals, tangents, begin, end); });
            _tasks.emplace_back(MakeRef<WaitHandle>(std::move(wait_handle)));
        }
    }

    void SkinningSystem::WaitFor() const
    {
        for (const auto &task : _tasks)
            JobSystem::Get().Wait(*task);
    }

    void SkinningSystem::Clear()
    {
        WaitFor();
        _tasks.clear();
    }
}
