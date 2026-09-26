#include "Render/RayTracing/RayTracingShader.h"
#include "Render/Buffer.h"
#include "Render/CoreType.h"
#include "Render/RayTracing/RayTracingScene.h"
#include "Render/ShaderDependencyPath.h"
#include "Framework/Common/FileManager.h"
#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "pch.h"

namespace Ailu::Render
{
    namespace
    {
        bool IsInternalConstantBuffer(const ShaderPropertyBinding &binding)
        {
            return binding._resource_type == EBindResDescType::kConstBuffer &&
                   (binding._bind_flag & ShaderBindResourceInfo::kBindFlagInternal ||
                    binding._bind_flag & ShaderBindResourceInfo::kBindFlagLocal);
        }

        bool IsVectorCompatible(EBindResDescType resource_type)
        {
            return resource_type & EBindResDescType::kCBufferFloats ||
                   resource_type & EBindResDescType::kCBufferInts ||
                   resource_type & EBindResDescType::kCBufferUInts ||
                   resource_type == EBindResDescType::kCBufferAttribute;
        }
    }

    Ref<RayTracingShader> RayTracingShader::Create(const WString &sys_path, const String &name)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::ERenderAPI::kNone:
            AL_ASSERT_MSG(false, "None render api used!");
            return nullptr;
        case RendererAPI::ERenderAPI::kDirectX12:
        {
            auto shader = AdoptGpuResource(new RHI::DX12::D3DRayTracingShader(sys_path));
            shader->Name(name);
            return shader;
        }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }

    RayTracingShader::RayTracingShader(const WString &sys_path) : _src_file_path(sys_path)
    {
        _name = ToChar(PathUtils::GetFileName(sys_path));
        memset(_cbuf_data, 0, sizeof(_cbuf_data));
        memset(_cache_cbuf_data, 0, sizeof(_cache_cbuf_data));
        std::scoped_lock lock(s_live_instances_mutex);
        s_live_instances.emplace_back(this);
    }

    RayTracingShader::~RayTracingShader()
    {
        std::scoped_lock lock(s_live_instances_mutex);
        std::erase(s_live_instances, this);
    }

    Vector<RayTracingShader *> RayTracingShader::GetLiveInstances()
    {
        std::scoped_lock lock(s_live_instances_mutex);
        return s_live_instances;
    }

    void RayTracingShader::SetGlobalTexture(const String &name, RTHandle texture)
    {
        SetGlobalTexture(name, g_pRenderTexturePool->Get(texture));
    }

    void RayTracingShader::SetScene(RayTracingScene *scene)
    {
        _scene = scene;
        if (scene == nullptr || _binding_layout == nullptr)
            return;
        for (const auto &[property_id, binding] : _binding_layout->Bindings())
        {
            if (binding._resource_type == EBindResDescType::kAccelerationStructure)
                _resource_bindings[property_id] = ShaderResourceBinding{scene, {}};
        }
    }

    void RayTracingShader::SetTexture(const String &name, Texture *texture)
    {
        SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture);
    }

    void RayTracingShader::SetTexture(ShaderPropertyId property_id, Texture *texture)
    {
        if (texture == nullptr)
            return;
        auto &binding = _resource_bindings[property_id];
        binding._resource = texture;
        binding._params = ShaderResourceBindParams{};
        if (texture->Dimension() == ETextureDimension::kTex3D)
            binding._params._slice = dynamic_cast<Texture3D *>(texture)->Depth();
    }

    void RayTracingShader::SetTexture(const String &name, RTHandle handle)
    {
        SetTexture(ShaderPropertyRegistry::Get().Intern(name), g_pRenderTexturePool->Get(handle));
    }

    void RayTracingShader::SetBuffer(const String &name, ConstantBuffer *buf)
    {
        SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buf);
    }

    void RayTracingShader::SetBuffer(ShaderPropertyId property_id, ConstantBuffer *buf)
    {
        if (buf == nullptr)
            return;
        auto &binding = _resource_bindings[property_id];
        binding._resource = buf;
        binding._params = ShaderResourceBindParams{};
        if (_binding_layout != nullptr)
        {
            if (const auto *layout_binding = _binding_layout->Find(property_id))
                binding._params._is_internal_cbuf = layout_binding->_bind_flag & ShaderBindResourceInfo::kBindFlagInternal;
        }
    }

    void RayTracingShader::SetBuffer(const String &name, GPUBuffer *buf)
    {
        SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buf);
    }

    void RayTracingShader::SetBuffer(ShaderPropertyId property_id, GPUBuffer *buf)
    {
        if (buf != nullptr)
            _resource_bindings[property_id] = ShaderResourceBinding{buf, {}};
    }

    void RayTracingShader::SetFloat(const String &name, f32 value)
    {
        SetFloat(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void RayTracingShader::SetFloat(ShaderPropertyId property_id, f32 value)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
            memcpy(_cbuf_data + binding->_buffer_offset, &value, std::min<u32>(sizeof(value), binding->_buffer_size));
    }

    void RayTracingShader::SetFloats(const String &name, Vector<f32> values)
    {
        SetFloats(ShaderPropertyRegistry::Get().Intern(name), values);
    }

    void RayTracingShader::SetFloats(ShaderPropertyId property_id, const Vector<f32> &values)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
        {
            const u32 count = binding->_array_size > 1u
                                  ? std::min<u32>(binding->_array_size, values.size())
                                  : values.size();
            memcpy(_cbuf_data + binding->_buffer_offset, values.data(),
                   std::min<u32>(binding->_buffer_size, count * sizeof(f32)));
        }
    }

    void RayTracingShader::SetBool(const String &name, bool value)
    {
        SetBool(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void RayTracingShader::SetBool(ShaderPropertyId property_id, bool value)
    {
        const u32 bool_value = value ? 1u : 0u;
        if (_binding_layout != nullptr)
        {
            if (const auto *binding = _binding_layout->Find(property_id))
                memcpy(_cbuf_data + binding->_buffer_offset, &bool_value,
                       std::min<u32>(sizeof(bool_value), binding->_buffer_size));
        }
    }

    void RayTracingShader::SetInt(const String &name, i32 value)
    {
        SetInt(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void RayTracingShader::SetInt(ShaderPropertyId property_id, i32 value)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
            memcpy(_cbuf_data + binding->_buffer_offset, &value, std::min<u32>(sizeof(value), binding->_buffer_size));
    }

    void RayTracingShader::SetInts(const String &name, Vector<i32> values)
    {
        SetInts(ShaderPropertyRegistry::Get().Intern(name), values);
    }

    void RayTracingShader::SetInts(ShaderPropertyId property_id, const Vector<i32> &values)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
        {
            const u32 count = binding->_array_size > 1u
                                  ? std::min<u32>(binding->_array_size, values.size())
                                  : values.size();
            memcpy(_cbuf_data + binding->_buffer_offset, values.data(),
                   std::min<u32>(binding->_buffer_size, count * sizeof(i32)));
        }
    }

    void RayTracingShader::SetVector(const String &name, Vector4f vector)
    {
        SetVector(ShaderPropertyRegistry::Get().Intern(name), vector);
    }

    void RayTracingShader::SetVector(ShaderPropertyId property_id, Vector4f vector)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id);
            binding != nullptr && IsVectorCompatible(binding->_resource_type))
            memcpy(_cbuf_data + binding->_buffer_offset, &vector, std::min<u32>(sizeof(vector), binding->_buffer_size));
    }

    void RayTracingShader::SetVectorArray(const String &name, const Vector<Vector4f> &vectors)
    {
        SetVectorArray(ShaderPropertyRegistry::Get().Intern(name), vectors);
    }

    void RayTracingShader::SetVectorArray(ShaderPropertyId property_id, const Vector<Vector4f> &vectors)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
        {
            const u32 count = binding->_array_size > 1u
                                  ? std::min<u32>(binding->_array_size, vectors.size())
                                  : vectors.size();
            memcpy(_cbuf_data + binding->_buffer_offset, vectors.data(),
                   std::min<u32>(binding->_buffer_size, count * sizeof(Vector4f)));
        }
    }

    void RayTracingShader::SetVectorArray(const String &name, Vector4f *vectors, u16 num)
    {
        SetVectorArray(ShaderPropertyRegistry::Get().Intern(name), Vector<Vector4f>(vectors, vectors + num));
    }

    void RayTracingShader::SetMatrix(const String &name, Matrix4x4f mat)
    {
        SetMatrix(ShaderPropertyRegistry::Get().Intern(name), mat);
    }

    void RayTracingShader::SetMatrix(ShaderPropertyId property_id, Matrix4x4f mat)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
            memcpy(_cbuf_data + binding->_buffer_offset, &mat, std::min<u32>(sizeof(mat), binding->_buffer_size));
    }

    void RayTracingShader::SetMatrixArray(const String &name, Vector<Matrix4x4f> matrix_arr)
    {
        SetMatrixArray(ShaderPropertyRegistry::Get().Intern(name), matrix_arr);
    }

    void RayTracingShader::SetMatrixArray(ShaderPropertyId property_id, const Vector<Matrix4x4f> &matrix_arr)
    {
        if (_binding_layout == nullptr)
            return;
        if (const auto *binding = _binding_layout->Find(property_id))
        {
            const u32 count = binding->_array_size > 1u
                                  ? std::min<u32>(binding->_array_size, matrix_arr.size())
                                  : matrix_arr.size();
            memcpy(_cbuf_data + binding->_buffer_offset, matrix_arr.data(),
                   std::min<u32>(binding->_buffer_size, count * sizeof(Matrix4x4f)));
        }
    }

    bool RayTracingShader::IsDependencyFile(const WString &sys_path) const
    {
        return _all_dep_file_pathes.contains(NormalizeShaderDependencyPath(sys_path));
    }

    void RayTracingShader::RegisterDependencyFile(const WString &path)
    {
        _all_dep_file_pathes.insert(NormalizeShaderDependencyPath(path));
    }

    void RayTracingShader::EnableKeyword(const String &keyword)
    {
        if (_active_keywords.contains(keyword))
            return;
        const auto group = _variant_mgr.KeywordsSameGroup(keyword);
        if (group.empty())
            return;
        for (const auto &group_keyword : group)
            _active_keywords.erase(group_keyword);
        _active_keywords.insert(keyword);
        _active_variant.store(_variant_mgr.GetVariantHash(_active_keywords));
        if (const auto it = _variants.find(_active_variant.load()); it != _variants.end())
            _binding_layout = it->second._binding_layout;
    }

    void RayTracingShader::DisableKeyword(const String &keyword)
    {
        if (_active_keywords.erase(keyword) != 0u)
        {
            _active_variant.store(_variant_mgr.GetVariantHash(_active_keywords));
            if (const auto it = _variants.find(_active_variant.load()); it != _variants.end())
                _binding_layout = it->second._binding_layout;
        }
    }

    bool RayTracingShader::PreprocessVariants()
    {
        String source;
        if (!FileManager::ReadFile(_src_file_path, source))
            return false;
        Vector<std::set<String>> keyword_groups;
        for (const auto &line : su::Split(source, "\n"))
        {
            if (!su::BeginWith(line, "#pragma multi_compile "))
                continue;
            std::istringstream stream(line);
            String pragma, directive, keyword;
            stream >> pragma >> directive;
            auto &group = keyword_groups.emplace_back();
            while (stream >> keyword)
                group.insert(keyword);
        }
        _variant_mgr.BuildIndex(keyword_groups);
        Vector<std::set<String>> permutations(1);
        for (const auto &group : keyword_groups)
        {
            Vector<std::set<String>> next;
            for (const auto &keywords : permutations)
            {
                for (const auto &keyword : group)
                {
                    auto selected = keywords;
                    if (keyword != "_")
                        selected.insert(keyword);
                    next.emplace_back(std::move(selected));
                }
            }
            permutations = std::move(next);
        }
        auto previous_variants = std::move(_variants);
        HashMap<ShaderVariantHash, Variant> variants;
        for (const auto &keywords : permutations)
        {
            const auto hash = _variant_mgr.GetVariantHash(keywords);
            auto &variant = variants[hash];
            if (const auto old = previous_variants.find(hash); old != previous_variants.end() &&
                old->second._active_keywords == keywords)
                variant = std::move(old->second);
            variant._active_keywords = keywords;
        }
        _variants = std::move(variants);
        _active_variant.store(_variant_mgr.GetVariantHash(_active_keywords));
        return !_variants.empty();
    }

    void RayTracingShader::CaptureDispatchState(
        RayTracingScene *scene, DispatchBindingSnapshot &snapshot,
        const HashMap<ShaderPropertyId, CommandResourceBinding> *command_resources)
    {
        snapshot = DispatchBindingSnapshot{};
        if (scene != nullptr)
            SetScene(scene);
        if (_binding_layout == nullptr)
            return;

        memcpy(snapshot._cbuf_data.data(), _cbuf_data, sizeof(_cbuf_data));
        for (const auto &[property_id, value] : s_global_floats)
        {
            if (const auto *binding = _binding_layout->Find(property_id);
                binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
                memcpy(snapshot._cbuf_data.data() + binding->_buffer_offset, &value,
                       std::min<u32>(sizeof(value), binding->_buffer_size));
        }
        for (const auto &[property_id, value] : s_global_ints)
        {
            if (const auto *binding = _binding_layout->Find(property_id))
            {
                if (binding->_resource_type & EBindResDescType::kCBufferInt)
                    memcpy(snapshot._cbuf_data.data() + binding->_buffer_offset, &value,
                           std::min<u32>(sizeof(value), binding->_buffer_size));
                else if (binding->_resource_type & EBindResDescType::kCBufferUInt)
                {
                    const u32 uint_value = static_cast<u32>(value);
                    memcpy(snapshot._cbuf_data.data() + binding->_buffer_offset, &uint_value,
                           std::min<u32>(sizeof(uint_value), binding->_buffer_size));
                }
            }
        }

        auto *captured = snapshot._entries.data();
        u16 captured_count = 0u;
        auto find_captured = [&](u16 slot) -> DispatchBindingSnapshotEntry *
        {
            for (u16 index = 0u; index < captured_count; ++index)
            {
                if (captured[index]._slot == slot)
                    return &captured[index];
            }
            return nullptr;
        };
        auto apply_resource = [&](u16 slot, GpuResource *resource, EBindResDescType resource_type, u16 priority,
                                  const ShaderResourceBindParams &params,
                                  const PipelineResource::AddiInfo &addi_info = PipelineResource::AddiInfo{})
        {
            if (resource == nullptr || slot >= 32u)
                return;
            auto *entry = find_captured(slot);
            if (entry == nullptr)
            {
                if (captured_count >= 32u)
                    return;
                entry = &captured[captured_count++];
                entry->_slot = slot;
            }
            if (entry->_resource.IsValid() && entry->_priority > priority)
                return;
            entry->_resource = resource->Handle();
            entry->_data_version = {};
            if (auto *buffer = dynamic_cast<GPUBuffer *>(resource))
                entry->_data_version = buffer->CurrentDataVersion();
            entry->_resource_type = resource_type;
            entry->_priority = priority;
            entry->_face = static_cast<u16>(params._face);
            entry->_mipmap = params._mipmap;
            entry->_slice = params._slice;
            entry->_view_index = params._view_index;
            entry->_sub_res = params._sub_res;
            entry->_is_internal_cbuf = params._is_internal_cbuf;
            entry->_addi_info = addi_info;
            if (entry->_view_index == static_cast<u16>(-1) &&
                (resource_type == EBindResDescType::kTexture2D || resource_type == EBindResDescType::kCubeMap ||
                 resource_type == EBindResDescType::kTexture3D || resource_type == EBindResDescType::kUAVTexture2D ||
                 resource_type == EBindResDescType::kRWTexture3D))
            {
                const bool is_uav = resource_type == EBindResDescType::kUAVTexture2D ||
                                    resource_type == EBindResDescType::kRWTexture3D;
                const auto view_type = is_uav ? Texture::ETextureViewType::kUAV : Texture::ETextureViewType::kSRV;
                auto *texture = dynamic_cast<Texture *>(resource);
                if (!is_uav && params._face == static_cast<u16>(ECubemapFace::kUnknown) && texture != nullptr &&
                    texture->Dimension() == ETextureDimension::kCube)
                    entry->_view_index = Texture::kMainSRVIndex;
                else if (auto *texture3d = dynamic_cast<Texture3D *>(resource))
                    entry->_view_index = texture3d->CalculateViewIndex(
                        view_type, params._mipmap, params._slice);
                else
                    entry->_view_index = static_cast<Texture *>(resource)->CalculateViewIndex(
                        view_type, static_cast<ECubemapFace>(params._face), params._mipmap, 0u);
            }
        };

        for (const auto &[property_id, binding] : _binding_layout->Bindings())
        {
            if (binding._bind_slot < 0 || binding._bind_slot >= 32)
                continue;
            const auto resource_it = _resource_bindings.find(property_id);
            if (resource_it != _resource_bindings.end() && resource_it->second._resource != nullptr)
            {
                apply_resource(static_cast<u16>(binding._bind_slot), resource_it->second._resource,
                               binding._resource_type, PipelineResource::kPriorityLocal, resource_it->second._params);
            }
            else if (IsInternalConstantBuffer(binding))
            {
                auto *constant_buffer = ConstBufferPool::Acquire(kCBufferSize);
                constant_buffer->SetData(snapshot._cbuf_data.data());
                ShaderResourceBindParams params{};
                params._is_internal_cbuf = true;
                apply_resource(static_cast<u16>(binding._bind_slot), constant_buffer, binding._resource_type,
                               PipelineResource::kPriorityLocal, params);
            }
        }

        const auto global_resources = s_global_res_registry.CaptureSnapshot();
        for (const auto &[property_id, global_binding] : global_resources._resources)
        {
            const auto *binding = _binding_layout->Find(property_id);
            if (binding == nullptr || binding->_bind_slot < 0)
                continue;
            apply_resource(static_cast<u16>(binding->_bind_slot), global_binding._resource, binding->_resource_type,
                           PipelineResource::kPriorityGlobal, ShaderResourceBindParams{});
        }

        if (command_resources != nullptr)
        {
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                const auto *binding = _binding_layout->Find(property_id);
                if (binding == nullptr || binding->_bind_slot < 0)
                    continue;
                const auto resource_type = command_binding._resource_type == EBindResDescType::kConstBufferRaw
                                                ? EBindResDescType::kConstBufferRaw : binding->_resource_type;
                apply_resource(static_cast<u16>(binding->_bind_slot), command_binding._resource, resource_type,
                               PipelineResource::kPriorityCmd, ShaderResourceBindParams{}, command_binding._addi_info);
            }
        }

        snapshot._entry_count = captured_count;
        snapshot._variant_hash = _active_variant.load();
        if (const auto it = _variants.find(snapshot._variant_hash); it != _variants.end())
            snapshot._is_ready = it->second._is_ready;
    }

    bool RayTracingShader::Compile(EShaderCompilePolicy policy)
    {
        _is_compiling.store(true);
        bool is_succeed = PreprocessVariants();
        if (is_succeed)
        {
            _all_dep_file_pathes.clear();
            RegisterDependencyFile(_src_file_path);
            for (auto &[variant_hash, variant] : _variants)
            {
                const bool compiled = RHICompileImpl(variant_hash, policy);
                variant._is_ready = compiled || variant._is_ready;
                if (compiled)
                {
                    variant._bind_res_infos = std::move(_bind_res_infos);
                    variant._binding_layout = _binding_layout;
                    LOG_INFO("RayTracingShaderVariant READY shader={} variant={}", _name, variant_hash);
                }
                is_succeed &= compiled;
            }
            if (const auto it = _variants.find(_active_variant.load()); it != _variants.end())
                _binding_layout = it->second._binding_layout;
        }
        _is_compiling.store(false);
        return is_succeed;
    }

    bool RayTracingShader::RHICompileImpl(ShaderVariantHash, EShaderCompilePolicy)
    {
        return true;
    }
}
