#include "Render/Material.h"
#include "Assets/Asset.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/FrameAllocator.h"
#include "Render/FrameResource.h"
#include "Render/RenderingStates.h"
#include "pch.h"
#include <iosfwd>

namespace Ailu::Render
{
    Material::Material(Shader *shader, String name) : _p_shader(shader)
    {
        //AL_ASSERT(s_total_material_num < RenderConstants::kMaxMaterialDataCount);
        _name = name;
        _p_active_shader = _p_shader;
        Construct(true);
        shader->AddMaterialRef(this);
        ++s_total_material_num;
    }

    Material &Material::operator=(const Material &other)
    {
        //标准pass才会使用上面两个变量
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        for (auto &cbuf: other._property_blocks)
        {
            u32 buffer_size = cbuf._size;
            _property_blocks.emplace_back(PropertyBlock());
            _property_blocks.back()._data = new u8[buffer_size];
            _property_blocks.back()._size = buffer_size;
            memcpy(_property_blocks.back()._data, cbuf._data, buffer_size);
        }
        _bind_textures = other._bind_textures;
        _bind_textures_by_id = other._bind_textures_by_id;
        _bind_buffers_by_id = other._bind_buffers_by_id;
        return *this;
    }

    Material &Material::operator=(Material &&other) noexcept
    {
        //标准pass才会使用上面两个变量
        _name = other._name;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _property_blocks = std::move(other._property_blocks);
        other._property_blocks.clear();
        _bind_textures = std::move(other._bind_textures);
        other._bind_textures.clear();
        _bind_textures_by_id = std::move(other._bind_textures_by_id);
        other._bind_textures_by_id.clear();
        _bind_buffers_by_id = std::move(other._bind_buffers_by_id);
        other._bind_buffers_by_id.clear();
        return *this;
    }

    Material::Material(const Material &other)
    {
        _name = other._name;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        for (auto &cbuf: other._property_blocks)
        {
            u32 buffer_size = cbuf._size;
            _property_blocks.emplace_back(PropertyBlock());
            _property_blocks.back()._data = new u8[buffer_size];
            _property_blocks.back()._size = buffer_size;
            memcpy(_property_blocks.back()._data, cbuf._data, buffer_size);
        }
        _bind_textures = other._bind_textures;
        _bind_textures_by_id = other._bind_textures_by_id;
        _bind_buffers_by_id = other._bind_buffers_by_id;
    }
    Material::Material(Material &&other) noexcept
    {
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _property_blocks = std::move(other._property_blocks);
        other._property_blocks.clear();
        _bind_textures = std::move(other._bind_textures);
        other._bind_textures.clear();
        _bind_textures_by_id = std::move(other._bind_textures_by_id);
        other._bind_textures_by_id.clear();
        _bind_buffers_by_id = std::move(other._bind_buffers_by_id);
        other._bind_buffers_by_id.clear();
    }

    Material::~Material()
    {
        --s_total_material_num;
    }
    MaterialDrawState Material::CaptureDrawState(u16 pass_index, u32 frame_slot, u64 frame_count, FrameAllocator &allocator)
    {
        AL_ASSERT(pass_index < _pass_variants.size());
        const auto variant_hash = _pass_variants[pass_index]._variant_hash;
        auto &bind_infos = _p_active_shader->_passes[pass_index]._variants[variant_hash]._bind_res_infos;
        const ShaderBindingLayout *binding_layout = _p_active_shader->GetBindingLayout(pass_index, variant_hash);
        if (_binding_cache.size() != _pass_variants.size())
            _binding_cache.resize(_pass_variants.size());
        auto &cache = _binding_cache[pass_index];
        const u32 layout_version = binding_layout == nullptr ? 0u : binding_layout->Version();
        BindState cur_state;
        if (cache._material_version == _property_version && cache._layout_version == layout_version && cache._variant_hash == variant_hash)
        {
            ++RenderingStates::RenderData().MaterialBindingCacheHitCount;
            cur_state = cache._state;
        }
        else
        {
            ++RenderingStates::RenderData().MaterialBindingResolveCount;
            cur_state._pass_index = pass_index;
            cur_state._max_bind_slot = 0u;
            cur_state._variant_hash = variant_hash;
            memset(cur_state._bind_res.data(), 0, sizeof(GpuResource *) * 32);
            cur_state._bind_res_type.fill(EBindResDescType::kUnknown);
            memset(cur_state._bind_res_priority.data(), 0u, sizeof(u16) * 32);
            auto apply_local_resource = [&](ShaderPropertyId property_id, GpuResource *resource)
            {
                if (binding_layout == nullptr || resource == nullptr)
                    return;
                const auto *binding = binding_layout->Find(property_id);
                if (binding == nullptr || binding->_bind_slot < 0 || binding->_bind_slot >= 32)
                    return;
                const u8 slot = static_cast<u8>(binding->_bind_slot);
                cur_state._bind_res[slot] = resource;
                cur_state._bind_res_type[slot] = binding->_resource_type;
                cur_state._bind_res_priority[slot] = PipelineResource::kPriorityLocal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, slot);
            };
            for (const auto &[property_id, texture] : _bind_textures_by_id)
                apply_local_resource(property_id, texture);
            for (const auto &[property_id, buffer] : _bind_buffers_by_id)
                apply_local_resource(property_id, buffer);
            for (auto it = _bind_textures.begin(); it != _bind_textures.end(); ++it)
            {
                if (const auto &bind_it = bind_infos.find(it->first); bind_it != bind_infos.end())
                {
                    const u8 slot = bind_it->second._bind_slot;
                    AL_ASSERT(slot < 32);
                    if (it->second != nullptr)
                    {
                        cur_state._bind_res[slot] = it->second;
                        cur_state._bind_res_type[slot] = bind_it->second._res_type;
                        cur_state._bind_res_priority[slot] = PipelineResource::kPriorityLocal;
                        cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, slot);
                    }
                }
            }
            for (const auto &it : bind_infos)
            {
                if (it.second._bind_slot >= 32 || it.second._p_res == nullptr)
                    continue;
                cur_state._bind_res[it.second._bind_slot] = it.second._p_res;
                cur_state._bind_res_type[it.second._bind_slot] = it.second._res_type;
                cur_state._bind_res_priority[it.second._bind_slot] = PipelineResource::kPriorityLocal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, it.second._bind_slot);
            }
            cur_state._cbuf_bind_slot = _p_active_shader->_passes[pass_index]._variants[variant_hash]._per_mat_buf_bind_slot;
            cache._material_version = _property_version;
            cache._layout_version = layout_version;
            cache._variant_hash = variant_hash;
            cache._state = cur_state;
        }
        for (const auto &it : Shader::s_global_textures_bind_info)
        {
            const auto &bind_it = bind_infos.find(it.first);
            if (bind_it == bind_infos.end())
                continue;
            const auto type = bind_it->second._res_type;
            if (type != EBindResDescType::kCubeMap && type != EBindResDescType::kTexture2DArray &&
                type != EBindResDescType::kTexture2D && type != EBindResDescType::kTexture3D)
                continue;
            AL_ASSERT(bind_it->second._bind_slot < 32);
            const u8 slot = bind_it->second._bind_slot;
            if (cur_state._bind_res_priority[slot] <= PipelineResource::kPriorityGlobal)
            {
                cur_state._bind_res[slot] = it.second;
                cur_state._bind_res_type[slot] = bind_it->second._res_type;
                cur_state._bind_res_priority[slot] = PipelineResource::kPriorityGlobal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, slot);
            }
        }
        for (const auto &it : Shader::s_global_buffer_bind_info)
        {
            const auto &bind_it = bind_infos.find(it.first);
            if (bind_it == bind_infos.end() || bind_it->second._bind_slot >= 32)
                continue;
            const u8 slot = bind_it->second._bind_slot;
            if (cur_state._bind_res_priority[slot] <= PipelineResource::kPriorityGlobal)
            {
                cur_state._bind_res[slot] = it.second;
                cur_state._bind_res_type[slot] = bind_it->second._res_type;
                cur_state._bind_res_priority[slot] = PipelineResource::kPriorityGlobal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, slot);
            }
        }

        MaterialDrawState draw_state;
        draw_state._shader = _p_active_shader;
        draw_state._binding_layout = binding_layout;
        draw_state._pass_index = pass_index;
        draw_state._variant_hash = variant_hash;
        draw_state._is_ready = _p_active_shader->GetVariantState(pass_index, variant_hash) == EShaderVariantState::kReady;
        draw_state._pipeline_shader_hash = _p_active_shader->_passes[pass_index]._variants[variant_hash]._shader_hash;
        draw_state._material_version = _property_version;
        draw_state._material_cbuffer_slot = cur_state._cbuf_bind_slot;
        draw_state._cull_mode = static_cast<ECullMode>(_common_uint_property[kCullModeKey]);
        for (u16 slot = 0u; slot <= cur_state._max_bind_slot; ++slot)
        {
            GpuResource *res = cur_state._bind_res[slot];
            if (res == nullptr)
                continue;
            auto &binding = draw_state._bindings[slot];
            auto res_type = cur_state._bind_res_type[slot];
            if (res_type == EBindResDescType::kUnknown)
            {
                if (res->GetResourceType() == EGpuResType::kTexture || res->GetResourceType() == EGpuResType::kRenderTexture)
                    res_type = static_cast<Texture *>(res)->Dimension() == ETextureDimension::kTex3D ? EBindResDescType::kTexture3D : EBindResDescType::kTexture2D;
                else if (res->GetResourceType() == EGpuResType::kConstBuffer)
                    res_type = EBindResDescType::kConstBuffer;
                else if (res->GetResourceType() == EGpuResType::kBuffer)
                    res_type = EBindResDescType::kBuffer;
                else
                    AL_ASSERT_MSG(false, "Unknown resource type");
            }
            binding._resource = res;
            binding._resource_type = res_type;
            binding._slot = slot;
            binding._priority = cur_state._bind_res_priority[slot];
            draw_state._binding_mask |= 1u << slot;
        }
        const auto block = GetPropertyBlockForFrame(pass_index, frame_slot, frame_count, allocator);
        draw_state._property_data = block._data;
        draw_state._property_size = block._size;
        return draw_state;
    }

    void Material::ChangeShader(Shader *shader)
    {
        _p_shader->RemoveMaterialRef(this);
        _p_shader = shader;
        _p_shader->AddMaterialRef(this);
        _p_active_shader->RemoveMaterialRef(this);
        _p_active_shader = shader;
        _p_active_shader->AddMaterialRef(this);
        Construct(false);
        ResetMaterialCaches();
    }

    bool Material::IsReadyForDraw(u16 pass_index) const
    {
        return _p_active_shader->GetVariantState(pass_index, _pass_variants[pass_index]._variant_hash) == EShaderVariantState::kReady;
    }

    void Material::SetFloat(const String &name, const float &f)
    {
        SetFloat(ShaderPropertyRegistry::Get().Intern(name), f);
    }

    void Material::SetFloat(ShaderPropertyId property_id, const float &f)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &f, sizeof(f));
        }
        MarkPropertiesDirty();
    }

    void Material::SetInt(const String &name, i32 value)
    {
        _common_uint_property[name] = value;
        SetInt(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void Material::SetInt(ShaderPropertyId property_id, i32 value)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding == nullptr)
                continue;
            if (binding->_resource_type & EBindResDescType::kCBufferInt)
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &value, sizeof(value));
            else if (binding->_resource_type & EBindResDescType::kCBufferUInt)
            {
                const u32 tmp = static_cast<u32>(value);
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &tmp, sizeof(tmp));
            }
        }
        MarkPropertiesDirty();
    }

    void Material::SetVector(const String &name, const Vector4f &vector)
    {
        SetVector(ShaderPropertyRegistry::Get().Intern(name), vector);
    }

    void Material::SetVector(ShaderPropertyId property_id, const Vector4f &vector)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloats)
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &vector, binding->_buffer_size);
        }
        MarkPropertiesDirty();
    }

    void Material::SetVector(const String &name, const Vector4Int &vector)
    {
        SetVector(ShaderPropertyRegistry::Get().Intern(name), vector);
    }


    void Material::SetMatrix(const String &name, const Matrix4x4f &matrix)
    {
        SetMatrix(ShaderPropertyRegistry::Get().Intern(name), matrix);
    }

    void Material::SetMatrix(ShaderPropertyId property_id, const Matrix4x4f &matrix)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferMatrix)
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &matrix, sizeof(matrix));
        }
        MarkPropertiesDirty();
    }

    float Material::GetFloat(const String &name)
    {
        return GetFloat(ShaderPropertyRegistry::Get().Intern(name));
    }

    float Material::GetFloat(ShaderPropertyId property_id)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
                return *reinterpret_cast<float *>(_property_blocks[pass_index]._data + binding->_buffer_offset);
        }
        return 0.0f;
    }
    void Material::SetCullMode(ECullMode mode)
    {
        _common_uint_property[kCullModeKey] = (u32) mode;
    }
    ECullMode Material::GetCullMode() const
    {
        return (ECullMode) _common_uint_property.at(kCullModeKey);
    };

    ShaderVariantHash Material::ActiveVariantHash(u16 pass_index) const
    {
        AL_ASSERT(pass_index < _pass_variants.size());
        return _pass_variants[pass_index]._variant_hash;
    }
    std::set<String> Material::ActiveKeywords(u16 pass_index) const
    {
        AL_ASSERT(pass_index < _pass_variants.size());
        return _pass_variants[pass_index]._keywords;
    };

    u32 Material::GetUint(const String &name)
    {
        const auto value = GetUint(ShaderPropertyRegistry::Get().Intern(name));
        if (value != static_cast<u32>(-1))
            return value;
        if (_common_uint_property.contains(name))
        {
            return _common_uint_property[name];
        }
        return -1;
    }

    u32 Material::GetUint(ShaderPropertyId property_id)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferUInt)
                return *reinterpret_cast<u32 *>(_property_blocks[pass_index]._data + binding->_buffer_offset);
        }
        return static_cast<u32>(-1);
    }

    Vector4f Material::GetVector(const String &name)
    {
        return GetVector(ShaderPropertyRegistry::Get().Intern(name));
    }

    Vector4f Material::GetVector(ShaderPropertyId property_id)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloats)
                return *reinterpret_cast<Vector4f *>(_property_blocks[pass_index]._data + binding->_buffer_offset);
        }
        return Vector4f::kZero;
    }

    void Material::SetTexture(const String &name, Texture *texture)
    {
        SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture);
    }

    void Material::SetTexture(ShaderPropertyId property_id, Texture *texture)
    {
        _bind_textures_by_id[property_id] = texture;
        const auto &name = ShaderPropertyRegistry::Get().GetName(property_id);
        if (!name.empty())
        {
            _bind_textures[name] = texture;
            if (_properties.find(name) != _properties.end())
                _properties[name]._value_ptr = reinterpret_cast<void *>(texture);
        }
        MarkPropertiesDirty();
    }

    void Material::SetVector(ShaderPropertyId property_id, const Vector4Int &vector)
    {
        for (u16 pass_index = 0u; pass_index < _p_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding == nullptr)
                continue;
            if (binding->_resource_type & EBindResDescType::kCBufferUInts)
            {
                Vector4UInt vector_uint = {(u32) vector.x, (u32) vector.y, (u32) vector.z, (u32) vector.w};
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &vector_uint, binding->_buffer_size);
            }
            else if (binding->_resource_type & EBindResDescType::kCBufferInts)
                memcpy(_property_blocks[pass_index]._data + binding->_buffer_offset, &vector, binding->_buffer_size);
        }
        MarkPropertiesDirty();
    }

    void Material::SetTexture(const String &name, const WString &texture_path)
    {
        auto texture = ResourceMgr::Get().Get<Texture2D>(texture_path);
        if (texture == nullptr)
        {
            LOG_ERROR("Cann't find texture: {} when set material {} texture{}!", ToChar(texture_path), _name, name);
            return;
        }
        SetTexture(name, texture);
    }

    void Material::SetTexture(const String &name, RTHandle texture)
    {
        auto raw_texture = g_pRenderTexturePool->Get(texture);
        SetTexture(name, raw_texture);
    }

    void Material::SetBuffer(const String& name,GPUBuffer* buffer)
    {
        SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buffer);
    }

    void Material::SetBuffer(ShaderPropertyId property_id, GPUBuffer *buffer)
    {
        _bind_buffers_by_id[property_id] = buffer;
        MarkPropertiesDirty();
    }

    void Material::EnableKeyword(const String &keyword)
    {
        u16 pass_index = 0;
        if (_all_keywords.contains(keyword))
            return;
        for (auto &p: _pass_variants)
        {
            if (_p_active_shader->IsKeywordValid(pass_index, keyword))
            {
                auto group_kws = _p_active_shader->KeywordsSameGroup(pass_index, keyword);
                for (auto &kw: group_kws)
                {
                    _pass_variants[pass_index]._keywords.erase(kw);
                    _all_keywords.erase(kw);
                }
                _pass_variants[pass_index]._keywords.insert(keyword);
                _pass_variants[pass_index]._variant_hash = _p_active_shader->ConstructVariantHash(pass_index, _pass_variants[pass_index]._keywords);
                _all_keywords.insert(keyword);
                UpdateBindTexture(pass_index, _pass_variants[pass_index]._variant_hash);
            }
            ++pass_index;
        }
        MarkPropertiesDirty();
    }

    void Material::DisableKeyword(const String &keyword)
    {
        if (!_all_keywords.contains(keyword))
            return;
        u16 pass_index = 0;
        for (auto &p: _pass_variants)
        {
            if (_p_active_shader->IsKeywordValid(pass_index, keyword))
            {
                if (_pass_variants[pass_index]._keywords.contains(keyword))
                {
                    _pass_variants[pass_index]._keywords.erase(keyword);
                    _pass_variants[pass_index]._variant_hash = _p_active_shader->ConstructVariantHash(pass_index, _pass_variants[pass_index]._keywords);
                    UpdateBindTexture(pass_index, _pass_variants[pass_index]._variant_hash);
                }
            }
            _all_keywords.erase(keyword);
            ++pass_index;
        }
        MarkPropertiesDirty();
    }

    void Material::RemoveTexture(const String &name)
    {
        if (_bind_textures.contains(name))
            _bind_textures[name] = nullptr;
        _bind_textures_by_id[ShaderPropertyRegistry::Get().Intern(name)] = nullptr;
        MarkPropertiesDirty();
    }

    List<std::tuple<String, float>> Material::GetAllFloatValue()
    {
        u16 pass_index = 0;
        Map<String, f32> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferFloat && ShaderBindResourceInfo::GetVariableSize(bind_info) == 4 && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    if (!value_map.contains((name)))
                        value_map[name] = *reinterpret_cast<f32 *>(_property_blocks[pass_index]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, float>> ret{};
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }

    List<std::tuple<String, Vector4f>> Material::GetAllVectorValue()
    {
        u16 pass_index = 0;
        Map<String, Vector4f> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferFloats && (ShaderBindResourceInfo::GetVariableSize(bind_info) == 16 || ShaderBindResourceInfo::GetVariableSize(bind_info) == 12) && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    bool three_dim = ShaderBindResourceInfo::GetVariableSize(bind_info) == 12;
                    Vector4f buf{};
                    if (three_dim)
                    {
                        auto value = *reinterpret_cast<Vector3f *>(_property_blocks[pass_index]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                        buf = Vector4f(value.x, value.y, value.z, 1.0f);
                    }
                    else
                    {
                        buf = *reinterpret_cast<Vector4f *>(_property_blocks[pass_index]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                    }
                    if (!value_map.contains((name)))
                        value_map[name] = buf;
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, Vector4f>> ret{};
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }
    List<std::tuple<String, Vector4Int>> Material::GetAllIntVectorValue()
    {
        u16 pass_index = 0;
        Map<String, Vector4Int> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) &&
                    ShaderBindResourceInfo::GetVariableSize(bind_info) == 16 && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    if (!value_map.contains((name)))
                    {
                        Vector4Int v;
                        if (bind_info._res_type & EBindResDescType::kCBufferUInts)
                        {
                            auto value = *reinterpret_cast<Vector4UInt *>(_property_blocks[pass_index]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                            v.x = (i32) value.x;
                            v.y = (i32) value.y;
                            v.z = (i32) value.z;
                            v.w = (i32) value.w;
                            value_map[name] = v;
                        }
                        else if (bind_info._res_type & EBindResDescType::kCBufferInts)
                        {
                            v = *reinterpret_cast<Vector4Int *>(_property_blocks[pass_index]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                            value_map[name] = v;
                        }
                    }
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, Vector4Int>> ret{};
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }

    List<std::tuple<String, u32>> Material::GetAllUintValue()
    {
        Map<String, u32> value_map{};
        u16 pass_index = 0;
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferUInt && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    if (!value_map.contains((name)))
                        value_map[name] = *reinterpret_cast<u32 *>(_property_blocks[pass_index]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, u32>> ret{};
        for (auto &prop: _common_uint_property)
        {
            if (!value_map.contains(prop.first))
                value_map[prop.first] = prop.second;
        }
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }

    void Material::Construct(bool first_time)
    {
        AL_ASSERT(_p_shader->PassCount() != 0);
        _common_uint_property["_cull"] = (u32) _p_shader->GetCullMode();
        ConstructKeywords(_p_shader);
        static u8 s_unused_shader_prop_buf[256]{0};
        u16 unused_shader_prop_buf_offset = 0u;
        u16 pass_count = _p_shader->PassCount();
        //u16 cur_shader_cbuf_size = 0; //每个passcbuffer大小一致。
        Vector<u16> cbuf_size_per_passes(pass_count);
        if (!first_time)
        {
            _properties.clear();
        }
        else
        {
            _mat_cbuf_per_pass_size.resize(pass_count);
        }
        _prop_views.clear();
        _properties.clear();
        _mat_cbuf_per_pass_size.resize(pass_count);
        _property_blocks.resize(pass_count);
        Vector<Map<String, std::tuple<u8, Texture *>>> _tmp_textures_all_passes(pass_count);
        for (int i = 0; i < pass_count; i++)
        {
            for (auto &bind_info: _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash))
            {
                if (bind_info.second._res_type == EBindResDescType::kTexture2D)
                {
                }
                else if (bind_info.second._res_type & EBindResDescType::kCBufferAttribute && bind_info.second._bind_flag & ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    auto byte_size = ShaderBindResourceInfo::GetVariableSize(bind_info.second);
                    cbuf_size_per_passes[i] += byte_size;
                }
            }
        }
        for (int i = 0; i < pass_count; i++)
        {
            //处理cbuffer
            if (cbuf_size_per_passes[i] == 0)
                cbuf_size_per_passes[i] = 256;
            cbuf_size_per_passes[i] = AlignTo(cbuf_size_per_passes[i],256);
            AL_ASSERT(cbuf_size_per_passes[i] <= 256);
            if (first_time)
            {
                _mat_cbuf_per_pass_size[i] = cbuf_size_per_passes[i];
                //_p_cbufs[i].reset(ConstantBuffer::Create(_mat_cbuf_per_pass_size[i]));
                //memset(_p_cbufs[i]->GetData(), 0, _mat_cbuf_per_pass_size[i]);
                _property_blocks[i]._size = _mat_cbuf_per_pass_size[i];
                _property_blocks[i]._data = new u8[_mat_cbuf_per_pass_size[i]];
                memset(_property_blocks[i]._data, 0, _mat_cbuf_per_pass_size[i]);
            }
            else if (_mat_cbuf_per_pass_size[i] != cbuf_size_per_passes[i])
            {
                throw std::runtime_error("Material: " + _name + " shader cbuf size not equal!");
                LOG_ERROR("Material: " + _name + " shader cbuf size not equal!");
                //u8* new_cbuf_data = new u8[cur_shader_cbuf_size];
                //memcpy(new_cbuf_data, _p_cbuf_cpu, _mat_cbuf_size);
                //delete[] _p_cbuf_cpu; _p_cbuf_cpu = nullptr;
                //_p_cbuf_cpu = new_cbuf_data;
            }
            else {}
            //处理纹理和属性
            auto &bind_info = _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
            for (auto &prop_info: _p_shader->GetShaderPropertyInfos(i))
            {
                ShaderPropertyInfo cur_prop = prop_info;
                if (prop_info._type == EShaderPropertyType::kTexture2D)
                {
                    _properties.insert(std::make_pair(prop_info._value_name, cur_prop));
                    if (!first_time)
                    {
                        if (auto it = _bind_textures.find(prop_info._value_name); it != _bind_textures.end())
                        {
                            _properties[prop_info._value_name]._value_ptr = it->second;
                            _bind_textures_by_id[prop_info._property_id] = it->second;
                        }
                    }
                }
                else
                {
                    if (auto it = bind_info.find(prop_info._value_name); it != bind_info.end())
                    {
                        cur_prop = prop_info;
                        cur_prop._value_ptr = (void *) (_property_blocks[i]._data + ShaderBindResourceInfo::GetVariableOffset(bind_info.find(prop_info._value_name)->second));
                    }
                    else
                    {
                        _common_float_property[prop_info._value_name] = prop_info._default_value.z;
                        cur_prop._value_ptr = (void *) (&_common_float_property[prop_info._value_name]);
                    }
                    //memcpy(prop._param, prop_info._param, sizeof(Vector4f));
                    if (cur_prop._type == EShaderPropertyType::kFloat || cur_prop._type == EShaderPropertyType::kRange)
                    {
                        f32 *value = (f32 *) cur_prop._value_ptr;
                        if (*value == 0.0f)
                            *value = prop_info._default_value[0];
                    }
                    else if (cur_prop._type == EShaderPropertyType::kColor || cur_prop._type == EShaderPropertyType::kVector)
                    {
                        auto *value = (Vector4f *) cur_prop._value_ptr;
                        if (*value == Vector4f::kZero)
                            *value = prop_info._default_value;
                    }
                    else if (cur_prop._type == EShaderPropertyType::kBool || cur_prop._type == EShaderPropertyType::kEnum)
                    {
                        // f32 *value = (f32 *) cur_prop._value_ptr;
                        // if (*value == 0.0f)
                        //     *value = prop_info._param.z;
                    }
                    _properties.insert(std::make_pair(prop_info._value_name, cur_prop));
                }
            }
        }
        _render_queue = _p_shader->RenderQueue(0);
        for (auto &it: _properties)
        {
            _prop_views.emplace_back(&it.second);
        }
        ResetMaterialCaches();
    }

    void Material::ConstructKeywords(Shader *shader)
    {
        _pass_variants.clear();
        for (u16 i = 0; i < shader->_passes.size(); i++)
        {
            auto &new_shader_pass = shader->_passes[i];
            ShaderVariantHash new_variant_hash;
            std::set<String> active_kws;
            new_variant_hash = shader->ConstructVariantHash(i, _all_keywords, active_kws);
            PassVariantInfo info;
            info._pass_name = new_shader_pass._name;
            info._keywords = std::move(active_kws);
            info._variant_hash = new_variant_hash;
            _pass_variants.emplace_back(info);
        }
    }

    void Material::UpdateBindTexture(u16 pass_index, ShaderVariantHash new_hash)
    {
        //auto &bind_infos = _p_active_shader->_passes[pass_index]._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
        //for (auto it = _bind_textures.begin(); it != _bind_textures.end(); it++)
        //{
        //    auto shader_bind_info_it = bind_infos.find(it->first);
        //    if (shader_bind_info_it != bind_infos.end())
        //    {
        //        u8 new_slot = shader_bind_info_it->second._bind_slot;
        //        it->second = std::make_pair(new_slot, texture);
        //    }
        //}
    }
    ShaderPropertyInfo *Material::GetShaderProperty(const String &name)
    {
        if (_properties.contains(name))
        {
            return &_properties[name];
        }
        return nullptr;
    }

    void Material::ResetMaterialCaches()
    {
        _binding_cache.clear();
        for (auto &cache : _frame_property_block_cache)
            cache = {};
    }

    Material::PropertyBlockView Material::GetPropertyBlockForFrame(u16 pass_index, u32 frame_slot, u64 frame_count,
                                                                    FrameAllocator &allocator)
    {
        AL_ASSERT(pass_index < _property_blocks.size());
        AL_ASSERT(frame_slot < _frame_property_block_cache.size());
        auto &frame_cache = _frame_property_block_cache[frame_slot];
        if (frame_cache.size() != _property_blocks.size())
            frame_cache.resize(_property_blocks.size());
        auto &cache = frame_cache[pass_index];
        if (cache._material_version == _property_version && cache._frame_count == frame_count && cache._block._data != nullptr)
        {
            return cache._block;
        }
        auto &block = _property_blocks[pass_index];
        cache._block._data = allocator.Allocate<u8>(block._size);
        cache._block._size = block._size;
        memcpy(cache._block._data, block._data, block._size);
        cache._material_version = _property_version;
        cache._frame_count = frame_count;
        return cache._block;
    }

    //-------------------------------------------StandardMaterial--------------------------------------------------------
    static void MarkTextureUsedHelper(u32 &mask, const ETextureUsage &usage, const bool &b_use)
    {
        switch (usage)
        {
            case ETextureUsage::kAlbedo:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kAlbedo._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kAlbedo._mask_flag);
                break;
            case ETextureUsage::kNormal:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kNormal._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kNormal._mask_flag);
                break;
            case ETextureUsage::kEmission:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kEmission._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kEmission._mask_flag);
                break;
            case ETextureUsage::kRoughness:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kRoughness._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kRoughness._mask_flag);
                break;
            case ETextureUsage::kMetallic:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kMetallic._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kMetallic._mask_flag);
                break;
            case ETextureUsage::kSpecular:
                mask = b_use ? StandardMaterial::StandardPropertyName::kSpecular._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kSpecular._mask_flag);
                break;
        }
    }
    StandardMaterial::StandardMaterial(String name) : Material(Shader::s_p_defered_standart_lit.lock().get(), name)
    {
        Construct(true);
        SetVector(StandardMaterial::StandardPropertyName::kAlbedo._value_name, Colors::kWhite);
        SetFloat(StandardMaterial::StandardPropertyName::kRoughness._value_name, 1.0f);
        SetFloat(StandardMaterial::StandardPropertyName::kMetallic._value_name, 0.0f);
    }
    StandardMaterial::~StandardMaterial()
    {
    }
    void StandardMaterial::MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use)
    {
        //40 根据shader中MaterialBuf计算，可能会有变动
        u32 *sampler_mask = reinterpret_cast<u32 *>(_property_blocks[_standard_pass_index]._data + _sampler_mask_offset);
        //*sampler_mask = 0;
        for (auto &usage: use_infos)
        {
            MarkTextureUsedHelper(*sampler_mask, usage, b_use);
        }
        MarkPropertiesDirty();
    }

    bool StandardMaterial::IsTextureUsed(ETextureUsage use_info)
    {
        u32 sampler_mask = *reinterpret_cast<u32 *>(_property_blocks[_standard_pass_index]._data + _sampler_mask_offset);
        switch (use_info)
        {
            case ETextureUsage::kAlbedo:
                return sampler_mask & StandardPropertyName::kAlbedo._mask_flag;
            case ETextureUsage::kNormal:
                return sampler_mask & StandardPropertyName::kNormal._mask_flag;
            case ETextureUsage::kEmission:
                return sampler_mask & StandardPropertyName::kEmission._mask_flag;
            case ETextureUsage::kRoughness:
                return sampler_mask & StandardPropertyName::kRoughness._mask_flag;
            case ETextureUsage::kMetallic:
                return sampler_mask & StandardPropertyName::kMetallic._mask_flag;
            case ETextureUsage::kSpecular:
                return sampler_mask & StandardPropertyName::kSpecular._mask_flag;
        }
        return false;
    }
    void StandardMaterial::MaterialID(const EMaterialID &value)
    {
        _material_id = value;
        _common_uint_property["_MaterialID"] = (u32) _material_id;
        if (_material_id_offset != 0)
        {
            u32 id = _material_id == EMaterialID::kChecker ? static_cast<u32>(_material_id) : 0u;
            memcpy(_property_blocks[_standard_pass_index]._data + _material_id_offset, &id, sizeof(u32));
        }
        MarkPropertiesDirty();
    }
    void StandardMaterial::SurfaceType(const ESurfaceType &value)
    {
        if (_surface == value)
            return;
        _p_active_shader->RemoveMaterialRef(this);
        if (value == ESurfaceType::kOpaque)
        {
            _p_active_shader = _p_shader;
            _render_queue = Shader::kRenderQueueOpaque;
            DisableKeyword("ALPHA_TEST");
        }
        else if (value == ESurfaceType::kTransparent)
        {
            DisableKeyword("ALPHA_TEST");
            _p_active_shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/forwardlit.alasset");
            _render_queue = Shader::kRenderQueueTransparent;
        }
        else if (value == ESurfaceType::kAlphaTest)
        {
            if (_p_active_shader != _p_shader)
                _p_active_shader = _p_shader;
            EnableKeyword("ALPHA_TEST");
            _render_queue = Shader::kRenderQueueAlphaTest;
        }
        _p_active_shader->AddMaterialRef(this);
        _surface = value;
        _common_uint_property[kSurfaceKey] = static_cast<u32>(_surface);
    }
    void StandardMaterial::SetTexture(const String &name, Texture *texture)
    {
        bool use_tex = texture != nullptr;
        if (name == StandardPropertyName::kAlbedo._tex_name) MarkTextureUsed({ETextureUsage::kAlbedo}, use_tex);
        else if (name == StandardPropertyName::kEmission._tex_name)
            MarkTextureUsed({ETextureUsage::kEmission}, use_tex);
        else if (name == StandardPropertyName::kRoughness._tex_name)
            MarkTextureUsed({ETextureUsage::kRoughness}, use_tex);
        else if (name == StandardPropertyName::kMetallic._tex_name)
            MarkTextureUsed({ETextureUsage::kMetallic}, use_tex);
        else if (name == StandardPropertyName::kSpecular._tex_name)
            MarkTextureUsed({ETextureUsage::kSpecular}, use_tex);
        else if (name == StandardPropertyName::kNormal._tex_name)
            MarkTextureUsed({ETextureUsage::kNormal}, use_tex);
        else {};
        Material::SetTexture(name, texture);
    }

    void StandardMaterial::SetTexture(const String &name, const WString &texture_path)
    {
        auto texture = ResourceMgr::Get().Get<Texture2D>(texture_path);
        if (texture == nullptr)
        {
            LOG_ERROR("Cann't find texture: {} when set material {} texture{}!", ToChar(texture_path), _name, name);
            return;
        }
        SetTexture(name, texture);
    }

    void StandardMaterial::SetTexture(const String &name, RTHandle texture)
    {
        auto raw_texture = g_pRenderTexturePool->Get(texture);
        SetTexture(name, raw_texture);
    }
    const Texture *StandardMaterial::MainTex(ETextureUsage usage) const
    {
        auto &info = StandardPropertyName::GetInfoByUsage(usage);
        auto &prop = _properties.at(info._tex_name);
        AL_ASSERT(prop._type == EShaderPropertyType::kTexture2D);
        return reinterpret_cast<Texture *>(prop._value_ptr);
    }
    const ShaderPropertyInfo &StandardMaterial::MainProperty(ETextureUsage usage)
    {
        auto &info = StandardPropertyName::GetInfoByUsage(usage);
        return _properties[info._value_name];
    }
    void StandardMaterial::SetTexture(ETextureUsage usage, Texture *tex)
    {
        MarkTextureUsed({usage}, tex != nullptr);
        auto &info = StandardPropertyName::GetInfoByUsage(usage);
        SetTexture(info._tex_name, tex);
    }
    void StandardMaterial::Construct(bool first_time)
    {
        //只有首个pass支持默认着色
        for (i16 i = 0; i < _p_shader->PassCount(); i++)
        {
            auto &cur_variant_bind_infos = _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
            auto it = cur_variant_bind_infos.find("_SamplerMask");
            if (it != cur_variant_bind_infos.end())
            {
                _sampler_mask_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                _standard_pass_index = i;
            }
            it = cur_variant_bind_infos.find("_MaterialID");
            if (it != cur_variant_bind_infos.end())
            {
                _material_id_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
            }
            if (_standard_pass_index != -1)
                break;
        }
        _material_id = (EMaterialID) _common_uint_property["_MaterialID"];
        _surface = (ESurfaceType) _common_uint_property["_surface"];
    }
    //-------------------------------------------StandardMaterial--------------------------------------------------------
}// namespace Ailu
