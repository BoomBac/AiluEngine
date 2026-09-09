#include "Render/Material.h"
#include "Assets/Asset.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/GraphicsContext.h"
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
        if (Asset *linked_asset = ResourceMgr::Get().GetLinkedAsset(_p_shader); linked_asset != nullptr)
        {
            _shader_guid = linked_asset->GetGuid();
            UpdateShaderHandle();
        }
        else
        {
            _shader_guid = Guid::EmptyGuid();
            _shader_handle = {};
        }
        if (_p_active_shader != nullptr)
        {
            Construct(true);
            _p_active_shader->AddMaterialRef(this);
        }
        ++s_total_material_num;
    }

    Material &Material::operator=(const Material &other)
    {
        if (this == &other)
            return *this;
        Material copy(other);
        return *this = std::move(copy);
    }

    Material &Material::operator=(Material &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (_p_active_shader != nullptr)
            _p_active_shader->RemoveMaterialRef(this);
        _name = other._name;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _shader_guid = other._shader_guid;
        _shader_handle = other._shader_handle;
        _texture_guids = std::move(other._texture_guids);
        _texture_handles_by_id = std::move(other._texture_handles_by_id);
        _standard_pass_index = other._standard_pass_index;
        _render_queue = other._render_queue;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _pass_variants = std::move(other._pass_variants);
        _all_keywords = std::move(other._all_keywords);
        _property_values = std::move(other._property_values);
        _properties = std::move(other._properties);
        _prop_views = std::move(other._prop_views);
        _property_blocks = std::move(other._property_blocks);
        _binding_cache.clear();
        for (auto &cache : _frame_property_block_cache)
            cache = {};
        _bind_textures_by_id = std::move(other._bind_textures_by_id);
        _bind_buffers_by_id = std::move(other._bind_buffers_by_id);
        _common_uint_property = std::move(other._common_uint_property);
        _common_float_property = std::move(other._common_float_property);
        _common_vector_property = std::move(other._common_vector_property);
        _cull_mode = other._cull_mode;
        _surface = other._surface;
        _material_id = other._material_id;
        _sampler_mask_offset = other._sampler_mask_offset;
        _material_id_offset = other._material_id_offset;
        _sampler_mask = other._sampler_mask;
        _is_standard_lit = other._is_standard_lit;
        _property_data_version = other._property_data_version;
        _resource_binding_version = other._resource_binding_version;
        other._p_shader = nullptr;
        other._p_active_shader = nullptr;
        return *this;
    }

    Material::Material(const Material &other) : Material(other._p_shader, other._name)
    {
        if (_p_active_shader != other._p_active_shader)
        {
            if (_p_active_shader != nullptr)
                _p_active_shader->RemoveMaterialRef(this);
            _p_active_shader = other._p_active_shader;
            if (_p_active_shader != nullptr)
                _p_active_shader->AddMaterialRef(this);
        }
        _shader_guid = other._shader_guid;
        _shader_handle = other._shader_handle;
        _texture_guids = other._texture_guids;
        _texture_handles_by_id = other._texture_handles_by_id;
        _standard_pass_index = other._standard_pass_index;
        _render_queue = other._render_queue;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _all_keywords = other._all_keywords;
        _property_values = other._property_values;
        _common_uint_property = other._common_uint_property;
        _common_float_property = other._common_float_property;
        _common_vector_property = other._common_vector_property;
        _cull_mode = other._cull_mode;
        _surface = other._surface;
        _material_id = other._material_id;
        _sampler_mask_offset = other._sampler_mask_offset;
        _material_id_offset = other._material_id_offset;
        _sampler_mask = other._sampler_mask;
        _is_standard_lit = other._is_standard_lit;
        _bind_textures_by_id = other._bind_textures_by_id;
        _bind_buffers_by_id = other._bind_buffers_by_id;

        _properties.clear();
        _property_blocks.clear();
        Construct(false);
        _standard_pass_index = other._standard_pass_index;
        _render_queue = other._render_queue;
        _property_data_version = other._property_data_version;
        _resource_binding_version = other._resource_binding_version;
    }
    Material::Material(Material &&other) noexcept
    {
        _name = std::move(other._name);
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _shader_guid = other._shader_guid;
        _shader_handle = other._shader_handle;
        _texture_guids = std::move(other._texture_guids);
        _texture_handles_by_id = std::move(other._texture_handles_by_id);
        _standard_pass_index = other._standard_pass_index;
        _render_queue = other._render_queue;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _pass_variants = std::move(other._pass_variants);
        _all_keywords = std::move(other._all_keywords);
        _property_values = std::move(other._property_values);
        _properties = std::move(other._properties);
        _prop_views = std::move(other._prop_views);
        _property_blocks = std::move(other._property_blocks);
        _bind_textures_by_id = std::move(other._bind_textures_by_id);
        _bind_buffers_by_id = std::move(other._bind_buffers_by_id);
        _common_uint_property = std::move(other._common_uint_property);
        _common_float_property = std::move(other._common_float_property);
        _common_vector_property = std::move(other._common_vector_property);
        _cull_mode = other._cull_mode;
        _surface = other._surface;
        _material_id = other._material_id;
        _sampler_mask_offset = other._sampler_mask_offset;
        _material_id_offset = other._material_id_offset;
        _sampler_mask = other._sampler_mask;
        _is_standard_lit = other._is_standard_lit;
        _property_data_version = other._property_data_version;
        _resource_binding_version = other._resource_binding_version;
        other._p_shader = nullptr;
        other._p_active_shader = nullptr;
    }

    Material::~Material()
    {
        if (_p_active_shader != nullptr)
            _p_active_shader->RemoveMaterialRef(this);
        --s_total_material_num;
    }

    void Material::UpdateShaderHandle()
    {
        _shader_handle = _shader_guid.IsEmpty() ? AssetHandle<Shader>{}
                                                : ResourceMgr::Get().GetOrCreateAssetHandle<Shader>(_shader_guid);
    }

    void Material::UpdateTextureHandle(ShaderPropertyId property_id, const Guid &guid)
    {
        if (guid.IsEmpty())
            _texture_handles_by_id.erase(property_id);
        else
            _texture_handles_by_id[property_id] = ResourceMgr::Get().GetOrCreateAssetHandle<Texture>(guid);
    }

    Material::AssetSnapshot Material::CaptureAssetSnapshots() const
    {
        AssetSnapshot snapshot;
        snapshot._shader = _shader_handle.Resolve();
        snapshot._shader_revision = _shader_handle.GetRevision();
        for (const auto &[property_id, handle]: _texture_handles_by_id)
        {
            Ref<const Texture> texture = handle.Resolve();
            if (texture == nullptr)
                continue;
            snapshot._texture_revisions[property_id] = handle.GetRevision();
            snapshot._textures[property_id] = std::move(texture);
        }
        return snapshot;
    }

    void Material::RefreshAssetReferences()
    {
        AssetSnapshot snapshot = CaptureAssetSnapshots();
        if (snapshot._shader != nullptr && snapshot._shader.get() != _p_shader)
            ChangeShader(const_cast<Shader *>(snapshot._shader.get()));

        for (const auto &[property_id, texture]: snapshot._textures)
        {
            auto bound_texture = _bind_textures_by_id.find(property_id);
            if (bound_texture == _bind_textures_by_id.end() || bound_texture->second != texture.get())
                SetTexture(property_id, const_cast<Texture *>(texture.get()));
        }
    }

    MaterialDrawState Material::CaptureDrawState(u16 pass_index, u32 frame_slot, u64 frame_count, FrameAllocator &allocator,
                                                 const HashMap<ShaderPropertyId, CommandResourceBinding> *command_resources,
                                                 CommandRenderingStatesData *statistics)
    {
        if (_p_active_shader == nullptr || pass_index >= _pass_variants.size())
            return {};
        AL_ASSERT(pass_index < _pass_variants.size());
        const auto variant_hash = _pass_variants[pass_index]._variant_hash;
        const ShaderBindingLayout *binding_layout = _p_active_shader->GetBindingLayout(pass_index, variant_hash);
        if (_binding_cache.size() != _pass_variants.size())
            _binding_cache.resize(_pass_variants.size());
        auto &cache = _binding_cache[pass_index];
        const u32 layout_version = binding_layout == nullptr ? 0u : binding_layout->Version();
        BindState cur_state;
        const auto &global_res_registry = Shader::GlobalResourceRegistry();
        u32 material_binding_invalid_reasons = 0u;
        if (cache._resource_binding_version != _resource_binding_version)
            material_binding_invalid_reasons |= 1u << 0u;
        if (cache._layout_version != layout_version)
            material_binding_invalid_reasons |= 1u << 1u;
        if (cache._variant_hash != variant_hash)
            material_binding_invalid_reasons |= 1u << 2u;
        if (cache._global_res_layout_version != global_res_registry.LayoutVersion())
            material_binding_invalid_reasons |= 1u << 3u;
        if (cache._global_res_binding_version != global_res_registry.BindingVersion())
            material_binding_invalid_reasons |= 1u << 4u;
        const bool is_material_cache_hit = material_binding_invalid_reasons == 0u;
        if (cache._resource_binding_version == _resource_binding_version && cache._layout_version == layout_version && cache._variant_hash == variant_hash
            && cache._global_res_layout_version == global_res_registry.LayoutVersion()
            && cache._global_res_binding_version == global_res_registry.BindingVersion())
        {
            if (statistics != nullptr) ++statistics->MaterialBindingCacheHitCount;
            cur_state = cache._state;
        }
        else
        {
            auto apply_resolved_resource = [&](const ResolvedResourceBinding &binding, GpuResource *resource, u16 priority)
            {
                if (resource == nullptr)
                    return;
                if (cur_state._bind_res_priority[binding._bind_slot] > priority)
                    return;
                cur_state._bind_res[binding._bind_slot] = resource;
                cur_state._bind_res_type[binding._bind_slot] = binding._resource_type;
                cur_state._bind_res_priority[binding._bind_slot] = priority;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, binding._bind_slot);
            };
            auto clear_resolved_global_resource = [&](const ResolvedResourceBinding &binding)
            {
                if (cur_state._bind_res_priority[binding._bind_slot] > PipelineResource::kPriorityGlobal)
                    return;
                cur_state._bind_res[binding._bind_slot] = nullptr;
                cur_state._bind_res_type[binding._bind_slot] = EBindResDescType::kUnknown;
                cur_state._bind_res_priority[binding._bind_slot] = 0u;
            };
            auto resolve_resource = [&](ShaderPropertyId property_id, ResolvedResourceBinding &out_binding)
            {
                if (binding_layout == nullptr)
                    return false;
                const auto *binding = binding_layout->Find(property_id);
                if (binding == nullptr || binding->_bind_slot < 0 || binding->_bind_slot >= 32)
                    return false;
                out_binding._property_id = property_id;
                out_binding._bind_slot = static_cast<u8>(binding->_bind_slot);
                out_binding._resource_type = binding->_resource_type;
                return true;
            };
            auto apply_resource = [&](ShaderPropertyId property_id, GpuResource *resource, u16 priority)
            {
                ResolvedResourceBinding resolved_binding;
                if (!resolve_resource(property_id, resolved_binding))
                    return;
                apply_resolved_resource(resolved_binding, resource, priority);
            };
            auto apply_global_bindings = [&]()
            {
                for (const auto &resolved_binding : cache._global_res_bindings)
                {
                    clear_resolved_global_resource(resolved_binding);
                    const auto it = global_res_registry.Resources().find(resolved_binding._property_id);
                    if (it != global_res_registry.Resources().end())
                        apply_resolved_resource(resolved_binding, it->second._resource, PipelineResource::kPriorityGlobal);
                }
            };

            const bool needs_full_rebuild = cache._resource_binding_version != _resource_binding_version || cache._layout_version != layout_version
                || cache._variant_hash != variant_hash || cache._global_res_layout_version != global_res_registry.LayoutVersion();
            if (needs_full_rebuild)
            {
                if (statistics != nullptr) ++statistics->MaterialBindingResolveCount;
                cur_state._pass_index = pass_index;
                cur_state._max_bind_slot = 0u;
                cur_state._variant_hash = variant_hash;
                memset(cur_state._bind_res.data(), 0, sizeof(GpuResource *) * 32);
                cur_state._bind_res_type.fill(EBindResDescType::kUnknown);
                memset(cur_state._bind_res_priority.data(), 0u, sizeof(u16) * 32);
                for (const auto &[property_id, texture] : _bind_textures_by_id)
                    apply_resource(property_id, texture, PipelineResource::kPriorityLocal);
                for (const auto &[property_id, buffer] : _bind_buffers_by_id)
                    apply_resource(property_id, buffer, PipelineResource::kPriorityLocal);
                if (cache._layout_version != layout_version || cache._variant_hash != variant_hash
                    || cache._global_res_layout_version != global_res_registry.LayoutVersion())
                {
                    cache._global_res_bindings.clear();
                    for (const auto &it : global_res_registry.Resources())
                    {
                        ResolvedResourceBinding resolved_binding;
                        if (resolve_resource(it.first, resolved_binding))
                            cache._global_res_bindings.emplace_back(resolved_binding);
                    }
                }
                apply_global_bindings();
            }
            else
            {
                cur_state = cache._state;
                apply_global_bindings();
            }
            cur_state._cbuf_bind_slot = _p_active_shader->_passes[pass_index]._variants[variant_hash]._per_mat_buf_bind_slot;
            cache._resource_binding_version = _resource_binding_version;
            cache._layout_version = layout_version;
            cache._variant_hash = variant_hash;
            cache._global_res_layout_version = global_res_registry.LayoutVersion();
            cache._global_res_binding_version = global_res_registry.BindingVersion();
            cache._state = cur_state;
        }

        // CommandBuffer资源属于当前draw，不能写入材质的跨命令缓存。
        if (command_resources != nullptr && binding_layout != nullptr)
        {
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                const auto *binding = binding_layout->Find(property_id);
                if (binding == nullptr || binding->_bind_slot < 0 || binding->_bind_slot >= 32)
                    continue;
                if (cur_state._bind_res_priority[binding->_bind_slot] > PipelineResource::kPriorityCmd)
                    continue;
                if (command_binding._resource == nullptr)
                    continue;
                if (cur_state._bind_res[binding->_bind_slot] != nullptr && statistics != nullptr)
                    ++statistics->PipelineResourceOverrideCount;
                cur_state._bind_res[binding->_bind_slot] = command_binding._resource;
                cur_state._bind_res_type[binding->_bind_slot] = binding->_resource_type;
                cur_state._bind_res_priority[binding->_bind_slot] = PipelineResource::kPriorityCmd;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, static_cast<u16>(binding->_bind_slot));
            }
        }

        // _is_ready: kReady is terminal — once true, cached permanently.
        // If false, re-check in case variant transitioned from compiling → ready.
        if (!cur_state._is_ready)
        {
            if (_p_active_shader->GetVariantState(pass_index, variant_hash) == EShaderVariantState::kReady)
            {
                cur_state._is_ready = true;
                _binding_cache[pass_index]._state._is_ready = true;
            }
        }

        MaterialDrawState draw_state;
        draw_state._shader = _p_active_shader;
        draw_state._binding_layout = binding_layout;
        draw_state._pass_index = pass_index;
        draw_state._variant_hash = variant_hash;
        draw_state._is_ready = cur_state._is_ready;
        draw_state._pipeline_shader_hash = _p_active_shader->_passes[pass_index]._variants[variant_hash]._shader_hash;
        RasterizerState raster_state = _p_active_shader->PipelineRasterizerState(pass_index);
        raster_state._cull_mode = _cull_mode;
        raster_state.Hash(RasterizerState::_s_hash_obj.GenHash(raster_state));
        draw_state._raster_state_hash = raster_state.Hash();
        draw_state._material_version = PropertyVersion();
        draw_state._material_binding_invalid_reasons = material_binding_invalid_reasons;
        draw_state._material_binding_result = is_material_cache_hit ? 0u :
            ((material_binding_invalid_reasons & ((1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u))) != 0u ? 2u : 1u);
        draw_state._binding_layout_version = layout_version;
        draw_state._global_layout_version = global_res_registry.LayoutVersion();
        draw_state._global_binding_version = global_res_registry.BindingVersion();
        draw_state._cull_mode = _cull_mode;
        auto is_command_cbuffer_slot = [&](u16 slot)
        {
            if (command_resources == nullptr || binding_layout == nullptr)
                return false;
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                if (command_binding._resource_type != EBindResDescType::kConstBuffer
                    && command_binding._resource_type != EBindResDescType::kConstBufferRaw)
                    continue;
                if (command_binding._addi_info._gpu_handle == 0u)
                    continue;
                const auto *binding = binding_layout->Find(property_id);
                if (binding != nullptr && binding->_bind_slot == slot)
                    return true;
            }
            return false;
        };
        u16 entry_count = 0u;
        for (u16 slot = 0u; slot <= cur_state._max_bind_slot; ++slot)
        {
            GpuResource *res = cur_state._bind_res[slot];
            if (res == nullptr || is_command_cbuffer_slot(slot))
                continue;
            ++entry_count;
        }
        // 录制阶段通过SetGlobalBuffer设置的命名const buffer也烘焙进snapshot，
        // 使RHI draw路径无需再遍历_allocations，只回放_bindings即可
        u16 cbuf_entry_count = 0u;
        if (command_resources != nullptr && binding_layout != nullptr)
        {
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                if (command_binding._resource_type != EBindResDescType::kConstBuffer
                    && command_binding._resource_type != EBindResDescType::kConstBufferRaw)
                    continue;
                if (command_binding._addi_info._gpu_handle == 0u)
                    continue;
                const auto *binding = binding_layout->Find(property_id);
                if (binding == nullptr || binding->_bind_slot < 0 || binding->_bind_slot >= 32)
                    continue;
                if (binding->_resource_type != EBindResDescType::kConstBuffer
                    && binding->_resource_type != EBindResDescType::kConstBufferRaw)
                    continue;
                ++cbuf_entry_count;
            }
        }
        // 将材质属性块(raw cbuffer)烘焙进snapshot，RHI draw路径只回放snapshot即可，
        // 无需在D3DContext再单独上传/绑定per-material cbuffer
        const bool has_mat_slot = cur_state._cbuf_bind_slot >= 0;
        const auto block = GetPropertyBlockForFrame(pass_index, frame_slot, frame_count, allocator, has_mat_slot, statistics);
        const bool has_material_block = has_mat_slot && block._size > 0u && block._gpu_handle != 0u;
        auto *entries = allocator.Allocate<PipelineBindingSnapshotEntry>(entry_count + cbuf_entry_count + (has_material_block ? 1u : 0u));
        draw_state._bindings._entries = entries;
        draw_state._bindings._entry_count = static_cast<u16>(entry_count + cbuf_entry_count + (has_material_block ? 1u : 0u));
        u16 entry_index = 0u;
        for (u16 slot = 0u; slot <= cur_state._max_bind_slot; ++slot)
        {
            GpuResource *res = cur_state._bind_res[slot];
            if (res == nullptr || is_command_cbuffer_slot(slot))
                continue;
            auto &binding = entries[entry_index++];
            auto res_type = cur_state._bind_res_type[slot];
            AL_ASSERT_MSG(res_type != EBindResDescType::kUnknown, "Unknown resource type");
            binding._resource = res;
            binding._resource_type = res_type;
            binding._slot = slot;
            binding._priority = cur_state._bind_res_priority[slot];
            binding._addi_info = {};
            draw_state._bindings._max_slot = slot;
            draw_state._bindings._binding_mask |= 1u << slot;
        }
        if (command_resources != nullptr && binding_layout != nullptr)
        {
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                if (command_binding._resource_type != EBindResDescType::kConstBuffer
                    && command_binding._resource_type != EBindResDescType::kConstBufferRaw)
                    continue;
                if (command_binding._addi_info._gpu_handle == 0u)
                    continue;
                const auto *binding = binding_layout->Find(property_id);
                if (binding == nullptr || binding->_bind_slot < 0 || binding->_bind_slot >= 32)
                    continue;
                if (binding->_resource_type != EBindResDescType::kConstBuffer
                    && binding->_resource_type != EBindResDescType::kConstBufferRaw)
                    continue;
                auto &cbuf_binding = entries[entry_index++];
                cbuf_binding._resource = command_binding._resource;
                cbuf_binding._resource_type = command_binding._resource_type;
                cbuf_binding._slot = static_cast<u16>(binding->_bind_slot);
                cbuf_binding._priority = PipelineResource::kPriorityCmd;
                cbuf_binding._addi_info = command_binding._addi_info;
                draw_state._bindings._max_slot = std::max(draw_state._bindings._max_slot, cbuf_binding._slot);
                draw_state._bindings._binding_mask |= 1u << cbuf_binding._slot;
            }
        }
        if (has_material_block)
        {
            auto &mat_binding = entries[entry_index++];
            mat_binding._resource = block._upload_buffer;
            mat_binding._resource_type = EBindResDescType::kConstBufferRaw;
            mat_binding._slot = static_cast<u16>(cur_state._cbuf_bind_slot);
            mat_binding._priority = PipelineResource::kPriorityCmd;
            mat_binding._addi_info._gpu_handle = block._gpu_handle;
            draw_state._bindings._max_slot = std::max(draw_state._bindings._max_slot, mat_binding._slot);
            draw_state._bindings._binding_mask |= 1u << mat_binding._slot;
        }
        return draw_state;
    }

    void Material::ChangeShader(Shader *shader)
    {
        if (_p_shader == shader && _p_active_shader == shader)
            return;
        Shader *old_active_shader = _p_active_shader;
        if (old_active_shader != nullptr)
            old_active_shader->RemoveMaterialRef(this);
        _p_shader = shader;
        _p_active_shader = shader;
        if (Asset *linked_asset = ResourceMgr::Get().GetLinkedAsset(_p_shader); linked_asset != nullptr)
        {
            _shader_guid = linked_asset->GetGuid();
            UpdateShaderHandle();
        }
        else
        {
            _shader_guid = Guid::EmptyGuid();
            _shader_handle = {};
        }
        if (_p_active_shader != nullptr)
            _p_active_shader->AddMaterialRef(this);
        if (_p_shader != nullptr)
            Construct(false);
        ResetMaterialCaches();
    }

    void Material::SetActiveShader(Shader *shader)
    {
        if (_p_active_shader == shader)
            return;
        if (_p_active_shader != nullptr)
            _p_active_shader->RemoveMaterialRef(this);
        _p_active_shader = shader;
        if (_p_active_shader != nullptr)
        {
            _p_active_shader->AddMaterialRef(this);
            Construct(false);
        }
        else
        {
            _pass_variants.clear();
            _property_blocks.clear();
            _properties.clear();
            _prop_views.clear();
        }
        ResetMaterialCaches();
    }

    bool Material::IsReadyForDraw(u16 pass_index) const
    {
        if (_p_active_shader == nullptr || pass_index >= _pass_variants.size())
            return false;
        return _p_active_shader->GetVariantState(pass_index, _pass_variants[pass_index]._variant_hash) == EShaderVariantState::kReady;
    }

    void Material::SetFloat(const String &name, const float &f)
    {
        SetFloat(ShaderPropertyRegistry::Get().Intern(name), f);
    }

    void Material::SetFloat(ShaderPropertyId property_id, const float &f)
    {
        bool is_changed = false;
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
            {
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &f, sizeof(f)) != 0)
                {
                    memcpy(dst, &f, sizeof(f));
                    is_changed = true;
                }
            }
        }
        if (is_changed)
        {
            SyncPropertyValue(property_id);
            MarkPropertyDataDirty();
            _on_property_changed_router.Invoke(property_id);
        }
    }

    void Material::SetInt(const String &name, i32 value)
    {
        _common_uint_property[name] = value;
        SetInt(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void Material::SetInt(ShaderPropertyId property_id, i32 value)
    {
        bool is_changed = false;
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding == nullptr)
                continue;
            if (binding->_resource_type & EBindResDescType::kCBufferInt)
            {
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &value, sizeof(value)) != 0)
                {
                    memcpy(dst, &value, sizeof(value));
                    is_changed = true;
                }
            }
            else if (binding->_resource_type & EBindResDescType::kCBufferUInt)
            {
                const u32 tmp = static_cast<u32>(value);
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &tmp, sizeof(tmp)) != 0)
                {
                    memcpy(dst, &tmp, sizeof(tmp));
                    is_changed = true;
                }
            }
        }
        if (is_changed)
        {
            SyncPropertyValue(property_id);
            MarkPropertyDataDirty();
            _on_property_changed_router.Invoke(property_id);
        }
    }

    void Material::SetVector(const String &name, const Vector4f &vector)
    {
        SetVector(ShaderPropertyRegistry::Get().Intern(name), vector);
    }

    void Material::SetVector(ShaderPropertyId property_id, const Vector4f &vector)
    {
        bool is_changed = false;
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloats)
            {
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &vector, binding->_buffer_size) != 0)
                {
                    memcpy(dst, &vector, binding->_buffer_size);
                    is_changed = true;
                }
            }
        }
        if (is_changed)
        {
            SyncPropertyValue(property_id);
            MarkPropertyDataDirty();
            _on_property_changed_router.Invoke(property_id);
        }
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
        bool is_changed = false;
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferMatrix)
            {
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &matrix, sizeof(matrix)) != 0)
                {
                    memcpy(dst, &matrix, sizeof(matrix));
                    is_changed = true;
                }
            }
        }
        if (is_changed)
        {
            SyncPropertyValue(property_id);
            MarkPropertyDataDirty();
            _on_property_changed_router.Invoke(property_id);
        }
    }

    float Material::GetFloat(const String &name)
    {
        return GetFloat(ShaderPropertyRegistry::Get().Intern(name));
    }

    float Material::GetFloat(ShaderPropertyId property_id)
    {
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
                return *reinterpret_cast<float *>(_property_blocks[pass_index]._data + binding->_buffer_offset);
        }
        return 0.0f;
    }
    void Material::SetCullMode(ECullMode mode)
    {
        _common_uint_property[kCullModeKey] = (u32) mode;
        _cull_mode = mode;
    }
    ECullMode Material::GetCullMode() const
    {
        return _cull_mode;
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
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
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
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloats)
                return *reinterpret_cast<Vector4f *>(_property_blocks[pass_index]._data + binding->_buffer_offset);
        }
        return Vector4f::kZero;
    }

    void Material::SetTexture(const String &name, Texture *texture)
    {
        if (texture == nullptr)
            _texture_guids[name] = Guid::EmptyGuid();
        else if (Asset *linked_asset = ResourceMgr::Get().GetLinkedAsset(texture); linked_asset != nullptr)
            _texture_guids[name] = linked_asset->GetGuid();
        SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture);

        if (!_is_standard_lit)
            return;
        if (name == StandardMaterialProperty::kAlbedo._tex_name)
            MarkTextureUsed({ETextureUsage::kAlbedo}, texture != nullptr);
        else if (name == StandardMaterialProperty::kEmission._tex_name)
            MarkTextureUsed({ETextureUsage::kEmission}, texture != nullptr);
        else if (name == StandardMaterialProperty::kRoughness._tex_name)
            MarkTextureUsed({ETextureUsage::kRoughness}, texture != nullptr);
        else if (name == StandardMaterialProperty::kMetallic._tex_name)
            MarkTextureUsed({ETextureUsage::kMetallic}, texture != nullptr);
        else if (name == StandardMaterialProperty::kSpecular._tex_name)
            MarkTextureUsed({ETextureUsage::kSpecular}, texture != nullptr);
        else if (name == StandardMaterialProperty::kNormal._tex_name)
            MarkTextureUsed({ETextureUsage::kNormal}, texture != nullptr);
        else {}
    }

    void Material::SetTexture(ShaderPropertyId property_id, Texture *texture)
    {
        if (auto it = _bind_textures_by_id.find(property_id); it != _bind_textures_by_id.end() && it->second == texture)
            return;
        _bind_textures_by_id[property_id] = texture;
        if (Asset *linked_asset = ResourceMgr::Get().GetLinkedAsset(texture); linked_asset != nullptr)
            _texture_handles_by_id[property_id] = ResourceMgr::Get().GetOrCreateAssetHandle<Texture>(linked_asset->GetGuid());
        else
            _texture_handles_by_id.erase(property_id);
        const auto &name = ShaderPropertyRegistry::Get().GetName(property_id);
        if (!name.empty())
        {
            if (_properties.find(name) != _properties.end())
                _properties[name]._value_ptr = reinterpret_cast<void *>(texture);
        }
        MarkResourceBindingsDirty();
        _on_property_changed_router.Invoke(property_id);
    }

    void Material::SetVector(ShaderPropertyId property_id, const Vector4Int &vector)
    {
        bool is_changed = false;
        for (u16 pass_index = 0u; pass_index < _p_active_shader->_passes.size(); ++pass_index)
        {
            const auto *layout = _p_active_shader->GetBindingLayout(pass_index, _pass_variants[pass_index]._variant_hash);
            const auto *binding = layout == nullptr ? nullptr : layout->Find(property_id);
            if (binding == nullptr)
                continue;
            if (binding->_resource_type & EBindResDescType::kCBufferUInts)
            {
                Vector4UInt vector_uint = {(u32) vector.x, (u32) vector.y, (u32) vector.z, (u32) vector.w};
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &vector_uint, binding->_buffer_size) != 0)
                {
                    memcpy(dst, &vector_uint, binding->_buffer_size);
                    is_changed = true;
                }
            }
            else if (binding->_resource_type & EBindResDescType::kCBufferInts)
            {
                u8 *dst = _property_blocks[pass_index]._data + binding->_buffer_offset;
                if (memcmp(dst, &vector, binding->_buffer_size) != 0)
                {
                    memcpy(dst, &vector, binding->_buffer_size);
                    is_changed = true;
                }
            }
        }
        if (is_changed)
        {
            SyncPropertyValue(property_id);
            MarkPropertyDataDirty();
            _on_property_changed_router.Invoke(property_id);
        }
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
        if (auto it = _bind_buffers_by_id.find(property_id); it != _bind_buffers_by_id.end() && it->second == buffer)
            return;
        _bind_buffers_by_id[property_id] = buffer;
        MarkResourceBindingsDirty();
        _on_property_changed_router.Invoke(property_id);
    }

    void Material::EnableKeyword(const String &keyword)
    {
        if (_p_active_shader == nullptr)
            return;
        if (_all_keywords.contains(keyword))
            return;
        for (u16 pass_index = 0u; pass_index < _pass_variants.size(); ++pass_index)
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
        }
        MarkResourceBindingsDirty();
    }

    void Material::DisableKeyword(const String &keyword)
    {
        if (_p_active_shader == nullptr)
            return;
        if (!_all_keywords.contains(keyword))
            return;
        for (u16 pass_index = 0u; pass_index < _pass_variants.size(); ++pass_index)
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
        }
        MarkResourceBindingsDirty();
    }

    void Material::RemoveTexture(const String &name)
    {
        _texture_guids[name] = Guid::EmptyGuid();
        ShaderPropertyId property_id = ShaderPropertyRegistry::Get().Intern(name);
        if (auto it = _bind_textures_by_id.find(property_id); it != _bind_textures_by_id.end() && it->second == nullptr)
            return;
        _bind_textures_by_id[property_id] = nullptr;
        MarkResourceBindingsDirty();
        _on_property_changed_router.Invoke(property_id);
    }

    List<std::tuple<String, float>> Material::GetAllFloatValue()
    {
        u16 pass_index = 0;
        Map<String, f32> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_active_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
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
            for (auto &[name, bind_info]: _p_active_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
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
            for (auto &[name, bind_info]: _p_active_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
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
            for (auto &[name, bind_info]: _p_active_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
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
        if (_p_active_shader == nullptr)
            return;
        AL_ASSERT(_p_active_shader->PassCount() != 0);

        Map<ShaderPropertyId, CachedPropertyValue> cached_values = _property_values;
        if (!first_time && !_properties.empty())
        {
            for (const auto &[name, property] : _properties)
            {
                if (property._value_ptr == nullptr || property._type == EShaderPropertyType::kTexture2D)
                    continue;
                u32 value_size = 0u;
                switch (property._type)
                {
                    case EShaderPropertyType::kBool:
                    case EShaderPropertyType::kEnum:
                    case EShaderPropertyType::kFloat:
                    case EShaderPropertyType::kRange:
                        value_size = sizeof(f32);
                        break;
                    case EShaderPropertyType::kColor:
                    case EShaderPropertyType::kVector:
                        value_size = sizeof(Vector4f);
                        break;
                    default:
                        break;
                }
                if (value_size == 0u)
                    continue;
                auto &cached = cached_values[property._property_id]._data;
                cached.resize(value_size);
                memcpy(cached.data(), property._value_ptr, value_size);
            }
        }

        _common_uint_property[kCullModeKey] = (u32) _p_active_shader->GetCullMode();
        _cull_mode = _p_active_shader->GetCullMode();
        ConstructKeywords(_p_active_shader);
        u16 pass_count = _p_active_shader->PassCount();
        //u16 cur_shader_cbuf_size = 0; //每个passcbuffer大小一致。
        Vector<u16> cbuf_size_per_passes(pass_count);
        _prop_views.clear();
        _properties.clear();
        _mat_cbuf_per_pass_size.clear();
        _mat_cbuf_per_pass_size.resize(pass_count);
        _property_blocks.clear();
        _property_blocks.resize(pass_count);
        for (int i = 0; i < pass_count; i++)
        {
            for (auto &bind_info: _p_active_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash))
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
            _mat_cbuf_per_pass_size[i] = cbuf_size_per_passes[i];
            _property_blocks[i]._size = cbuf_size_per_passes[i];
            _property_blocks[i]._data = AL_ALLOC_TAG(EMemoryTag::kRenderer, u8, cbuf_size_per_passes[i]);
            memset(_property_blocks[i]._data, 0, cbuf_size_per_passes[i]);
            //处理纹理和属性
            auto &bind_info = _p_active_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
            for (auto &prop_info: _p_active_shader->GetShaderPropertyInfos(i))
            {
                ShaderPropertyInfo cur_prop = prop_info;
                if (prop_info._type == EShaderPropertyType::kTexture2D)
                {
                    if (auto it = _bind_textures_by_id.find(prop_info._property_id); it != _bind_textures_by_id.end())
                        cur_prop._value_ptr = it->second;
                }
                else if (auto it = bind_info.find(prop_info._value_name); it != bind_info.end())
                {
                    const auto &binding = it->second;
                    u8 *dst = _property_blocks[i]._data + ShaderBindResourceInfo::GetVariableOffset(binding);
                    const u32 value_size = std::min<u32>(ShaderBindResourceInfo::GetVariableSize(binding), sizeof(Vector4f));
                    if (auto cached = cached_values.find(prop_info._property_id); cached != cached_values.end())
                        memcpy(dst, cached->second._data.data(), std::min<u32>(value_size, cached->second._data.size()));
                    else if (prop_info._type == EShaderPropertyType::kFloat || prop_info._type == EShaderPropertyType::kRange)
                        memcpy(dst, &prop_info._default_value.x, std::min<u32>(value_size, sizeof(f32)));
                    else if (prop_info._type == EShaderPropertyType::kColor || prop_info._type == EShaderPropertyType::kVector)
                        memcpy(dst, &prop_info._default_value, value_size);
                    cur_prop._value_ptr = dst;
                }
                else if (prop_info._type == EShaderPropertyType::kFloat || prop_info._type == EShaderPropertyType::kRange)
                {
                    auto &value = _common_float_property[prop_info._value_name];
                    if (auto cached = cached_values.find(prop_info._property_id); cached != cached_values.end())
                        memcpy(&value, cached->second._data.data(), sizeof(value));
                    else
                        value = prop_info._default_value.x;
                    cur_prop._value_ptr = &value;
                }
                else if (prop_info._type == EShaderPropertyType::kColor || prop_info._type == EShaderPropertyType::kVector)
                {
                    auto &value = _common_vector_property[prop_info._value_name];
                    if (auto cached = cached_values.find(prop_info._property_id); cached != cached_values.end())
                        memcpy(&value, cached->second._data.data(), sizeof(value));
                    else
                        value = prop_info._default_value;
                    cur_prop._value_ptr = &value;
                }
                else if (prop_info._type == EShaderPropertyType::kBool || prop_info._type == EShaderPropertyType::kEnum)
                {
                    auto &value = _common_uint_property[prop_info._value_name];
                    if (auto cached = cached_values.find(prop_info._property_id); cached != cached_values.end())
                        memcpy(&value, cached->second._data.data(), sizeof(value));
                    else
                        value = static_cast<u32>(prop_info._default_value.x);
                    cur_prop._value_ptr = &value;
                }
                _properties.insert(std::make_pair(prop_info._value_name, cur_prop));
            }
        }
        _render_queue = _p_active_shader->RenderQueue(0);
        for (auto &it: _properties)
        {
            _prop_views.emplace_back(&it.second);
        }
        _property_values.clear();
        for (const auto &[name, property] : _properties)
        {
            if (property._value_ptr == nullptr || property._type == EShaderPropertyType::kTexture2D)
                continue;
            u32 value_size = 0u;
            switch (property._type)
            {
                case EShaderPropertyType::kBool:
                case EShaderPropertyType::kEnum:
                case EShaderPropertyType::kFloat:
                case EShaderPropertyType::kRange:
                    value_size = sizeof(f32);
                    break;
                case EShaderPropertyType::kColor:
                case EShaderPropertyType::kVector:
                    value_size = sizeof(Vector4f);
                    break;
                default:
                    break;
            }
            if (value_size == 0u)
                continue;
            auto &cached = _property_values[property._property_id]._data;
            cached.resize(value_size);
            memcpy(cached.data(), property._value_ptr, value_size);
        }
        ResolveStandardMaterialLayout();
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

    ShaderPropertyInfo *Material::GetShaderProperty(ShaderPropertyId property_id)
    {
        return GetShaderProperty(ShaderPropertyRegistry::Get().GetName(property_id));
    }

    void Material::ResetMaterialCaches()
    {
        _binding_cache.clear();
        for (auto &cache : _frame_property_block_cache)
            cache = {};
    }

    void Material::SyncPropertyValue(ShaderPropertyId property_id)
    {
        const String &name = ShaderPropertyRegistry::Get().GetName(property_id);
        auto property = _properties.find(name);
        if (property == _properties.end() || property->second._value_ptr == nullptr
            || property->second._type == EShaderPropertyType::kTexture2D)
            return;
        u32 value_size = 0u;
        switch (property->second._type)
        {
            case EShaderPropertyType::kBool:
            case EShaderPropertyType::kEnum:
            case EShaderPropertyType::kFloat:
            case EShaderPropertyType::kRange:
                value_size = sizeof(f32);
                break;
            case EShaderPropertyType::kColor:
            case EShaderPropertyType::kVector:
                value_size = sizeof(Vector4f);
                break;
            default:
                break;
        }
        if (value_size == 0u)
            return;
        auto &cached = _property_values[property_id]._data;
        cached.resize(value_size);
        memcpy(cached.data(), property->second._value_ptr, value_size);
    }

    Material::PropertyBlockView Material::GetPropertyBlockForFrame(u16 pass_index, u32 frame_slot, u64 frame_count,
                                                                    FrameAllocator &allocator, bool upload_to_gpu,
                                                                    CommandRenderingStatesData *statistics)
    {
        AL_ASSERT(pass_index < _property_blocks.size());
        AL_ASSERT(frame_slot < _frame_property_block_cache.size());
        auto &frame_cache = _frame_property_block_cache[frame_slot];
        if (frame_cache.size() != _property_blocks.size())
            frame_cache.resize(_property_blocks.size());
        auto &cache = frame_cache[pass_index];
        if (cache._property_data_version == _property_data_version && cache._frame_count == frame_count && cache._block._data != nullptr)
        {
            // 帧内块缓存命中，复用已上传的GPU handle，无需重复上传
            if (upload_to_gpu && cache._block._size > 0u && cache._block._gpu_handle != 0u)
                if (statistics != nullptr) ++statistics->MaterialCBufferCacheHitCount;
            return cache._block;
        }
        auto &block = _property_blocks[pass_index];
        cache._block._data = allocator.Allocate<u8>(block._size);
        cache._block._size = block._size;
        memcpy(cache._block._data, block._data, block._size);
        if (upload_to_gpu && block._size > 0u)
        {
            // 直接烘焙进binding snapshot，RHI draw路径无需再单独上传/绑定per-material cbuffer
            auto upload = FrameResourceManager::Get().AllocFrameUpload(block._size, 256u);
            if (upload._cpu_ptr != nullptr)
                memcpy(upload._cpu_ptr, block._data, block._size);
            cache._block._upload_buffer = upload._buffer;
            cache._block._gpu_handle = upload._gpu_handle;
            if (statistics != nullptr)
            {
                ++statistics->MaterialCBufferUploadCount;
                statistics->MaterialCBufferUploadBytes += block._size;
            }
        }
        cache._property_data_version = _property_data_version;
        cache._frame_count = frame_count;
        return cache._block;
    }

    //-------------------------------------------Standard Lit capability--------------------------------------------------------
    static void MarkTextureUsedHelper(u32 &mask, const ETextureUsage &usage, const bool &b_use)
    {
        switch (usage)
        {
            case ETextureUsage::kAlbedo:
                mask = b_use ? mask | StandardMaterialProperty::kAlbedo._mask_flag : mask & (~StandardMaterialProperty::kAlbedo._mask_flag);
                break;
            case ETextureUsage::kNormal:
                mask = b_use ? mask | StandardMaterialProperty::kNormal._mask_flag : mask & (~StandardMaterialProperty::kNormal._mask_flag);
                break;
            case ETextureUsage::kEmission:
                mask = b_use ? mask | StandardMaterialProperty::kEmission._mask_flag : mask & (~StandardMaterialProperty::kEmission._mask_flag);
                break;
            case ETextureUsage::kRoughness:
                mask = b_use ? mask | StandardMaterialProperty::kRoughness._mask_flag : mask & (~StandardMaterialProperty::kRoughness._mask_flag);
                break;
            case ETextureUsage::kMetallic:
                mask = b_use ? mask | StandardMaterialProperty::kMetallic._mask_flag : mask & (~StandardMaterialProperty::kMetallic._mask_flag);
                break;
            case ETextureUsage::kSpecular:
                mask = b_use ? StandardMaterialProperty::kSpecular._mask_flag : mask & (~StandardMaterialProperty::kSpecular._mask_flag);
                break;
            default:
                break;
        }
    }
    Ref<Material> Material::CreateInstance() const
    {
        return MakeRef<Material>(*this);
    }

    Ref<Material> Material::CreateStandard(String name)
    {
        auto mat = MakeRef<Material>(Shader::s_p_defered_standart_lit.lock().get(), std::move(name));
        mat->SetVector(StandardMaterialProperty::kAlbedo._value_name, Colors::kWhite);
        mat->SetFloat(StandardMaterialProperty::kRoughness._value_name, 1.0f);
        mat->SetFloat(StandardMaterialProperty::kMetallic._value_name, 0.0f);
        return mat;
    }
    bool Material::IsStandardLit() const
    {
        return _is_standard_lit;
    }
    void Material::ResolveStandardMaterialLayout()
    {
        _sampler_mask_offset = 0u;
        _material_id_offset = 0u;
        _standard_pass_index = static_cast<u16>(-1);
        // 通过 shader identity 识别标准 Lit，与旧 is_standard_mat 判定保持一致
        auto standard_lit_shader = Shader::s_p_defered_standart_lit.lock().get();
        const bool is_deferred_standard_lit = _p_shader == standard_lit_shader
            || (_p_shader != nullptr && _p_shader->Name() == "defered_standard_lit");
        const bool is_forward_standard_lit = _p_active_shader != nullptr
            && _p_active_shader->Name() == "forward_standard_lit";
        _is_standard_lit = is_deferred_standard_lit || is_forward_standard_lit;
        if (!_is_standard_lit)
            return;
        //只有首个支持默认着色的pass承载 _SamplerMask / _MaterialID
        for (i16 i = 0; i < _p_active_shader->PassCount(); i++)
        {
            auto &cur_variant_bind_infos = _p_active_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
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
            if (_standard_pass_index != static_cast<u16>(-1))
                break;
        }
        if (_common_uint_property.contains("_MaterialID"))
            _material_id = (EMaterialID) _common_uint_property["_MaterialID"];
        if (_common_uint_property.contains("_surface"))
            _surface = (ESurfaceType) _common_uint_property["_surface"];
        if (_standard_pass_index != static_cast<u16>(-1))
        {
            auto *property_block = _property_blocks[_standard_pass_index]._data;
            memcpy(property_block + _sampler_mask_offset, &_sampler_mask, sizeof(_sampler_mask));
            u32 material_id = _material_id == EMaterialID::kChecker ? static_cast<u32>(_material_id) : 0u;
            memcpy(property_block + _material_id_offset, &material_id, sizeof(material_id));
        }
    }
    void Material::MarkTextureUsed(std::initializer_list<ETextureUsage> usages, bool b_use)
    {
        // 根据shader中MaterialBuf计算offset，可能会有变动
        if (!_is_standard_lit || _standard_pass_index == static_cast<u16>(-1))
            return;
        const u32 old_sampler_mask = _sampler_mask;
        //*sampler_mask = 0;
        for (auto &usage: usages)
        {
            MarkTextureUsedHelper(_sampler_mask, usage, b_use);
        }
        if (_sampler_mask != old_sampler_mask)
        {
            memcpy(_property_blocks[_standard_pass_index]._data + _sampler_mask_offset, &_sampler_mask, sizeof(_sampler_mask));
            MarkPropertyDataDirty();
        }
    }

    bool Material::IsTextureUsed(ETextureUsage use_info) const
    {
        if (!_is_standard_lit || _standard_pass_index == static_cast<u16>(-1))
            return false;
        u32 sampler_mask = _sampler_mask;
        switch (use_info)
        {
            case ETextureUsage::kAlbedo:
                return sampler_mask & StandardMaterialProperty::kAlbedo._mask_flag;
            case ETextureUsage::kNormal:
                return sampler_mask & StandardMaterialProperty::kNormal._mask_flag;
            case ETextureUsage::kEmission:
                return sampler_mask & StandardMaterialProperty::kEmission._mask_flag;
            case ETextureUsage::kRoughness:
                return sampler_mask & StandardMaterialProperty::kRoughness._mask_flag;
            case ETextureUsage::kMetallic:
                return sampler_mask & StandardMaterialProperty::kMetallic._mask_flag;
            case ETextureUsage::kSpecular:
                return sampler_mask & StandardMaterialProperty::kSpecular._mask_flag;
            default:
                break;
        }
        return false;
    }
    EMaterialID Material::MaterialID() const
    {
        return _material_id;
    }
    void Material::MaterialID(EMaterialID value)
    {
        if (!_is_standard_lit)
        {
            LOG_WARNING("Material {} is not standard lit, MaterialID ignored!", _name);
            return;
        }
        if (_material_id == value)
            return;
        _material_id = value;
        _common_uint_property["_MaterialID"] = (u32) _material_id;
        if (_standard_pass_index != static_cast<u16>(-1))
        {
            u32 id = _material_id == EMaterialID::kChecker ? static_cast<u32>(_material_id) : 0u;
            memcpy(_property_blocks[_standard_pass_index]._data + _material_id_offset, &id, sizeof(u32));
        }
        SyncPropertyValue(ShaderPropertyRegistry::Get().Intern("_MaterialID"));
        MarkPropertyDataDirty();
    }
    ESurfaceType Material::SurfaceType() const
    {
        return _surface;
    }
    void Material::SurfaceType(ESurfaceType value)
    {
        if (!_is_standard_lit)
        {
            LOG_WARNING("Material {} is not standard lit, SurfaceType ignored!", _name);
            return;
        }
        if (_surface == value)
            return;
        Shader *new_active_shader = _p_shader;
        if (value == ESurfaceType::kTransparent)
            new_active_shader = ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/forwardlit.alasset");
        if (new_active_shader == nullptr)
        {
            LOG_ERROR("Material {} cannot switch surface: forwardlit shader is unavailable!", _name);
            return;
        }

        Shader *old_active_shader = _p_active_shader;
        if (old_active_shader != new_active_shader)
        {
            old_active_shader->RemoveMaterialRef(this);
            _p_active_shader = new_active_shader;
            _p_active_shader->AddMaterialRef(this);
        }
        if (value == ESurfaceType::kAlphaTest)
            _all_keywords.insert("ALPHA_TEST");
        else
            _all_keywords.erase("ALPHA_TEST");
        _surface = value;
        _common_uint_property[kSurfaceKey] = static_cast<u32>(_surface);
        Construct(false);
        if (value == ESurfaceType::kOpaque)
            _render_queue = Shader::kRenderQueueOpaque;
        else if (value == ESurfaceType::kTransparent)
            _render_queue = Shader::kRenderQueueTransparent;
        else if (value == ESurfaceType::kAlphaTest)
            _render_queue = Shader::kRenderQueueAlphaTest;
        MarkResourceBindingsDirty();
        _on_property_changed_router.Invoke(SurfacePropertyId());
    }
    void Material::SetTexture(ETextureUsage usage, Texture *tex)
    {
        if (!_is_standard_lit)
            return;
        MarkTextureUsed({usage}, tex != nullptr);
        const auto &info = StandardMaterialProperty::GetInfoByUsage(usage);
        SetTexture(info._tex_name, tex);
    }
    const Texture *Material::MainTex(ETextureUsage usage) const
    {
        if (!_is_standard_lit)
            return nullptr;
        const auto &info = StandardMaterialProperty::GetInfoByUsage(usage);
        auto it = _properties.find(info._tex_name);
        if (it == _properties.end())
            return nullptr;
        const auto &prop = it->second;
        if (prop._type != EShaderPropertyType::kTexture2D || prop._value_ptr == nullptr)
            return nullptr;
        return reinterpret_cast<const Texture *>(prop._value_ptr);
    }
    const ShaderPropertyInfo *Material::MainProperty(ETextureUsage usage) const
    {
        if (!_is_standard_lit)
            return nullptr;
        const auto &info = StandardMaterialProperty::GetInfoByUsage(usage);
        auto it = _properties.find(info._value_name);
        if (it == _properties.end())
            return nullptr;
        return &it->second;
    }
    //-------------------------------------------Standard Lit capability--------------------------------------------------------
}// namespace Ailu
