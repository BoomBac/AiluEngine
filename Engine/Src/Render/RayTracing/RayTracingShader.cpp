#include "Render/RayTracing/RayTracingShader.h"
#include "Render/Buffer.h"
#include "Render/CoreType.h"
#include "Render/RayTracing/RayTracingScene.h"
#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "pch.h"

namespace Ailu::Render
{
    namespace
    {
        bool IsInternalConstantBuffer(const ShaderBindResourceInfo &bind_info)
        {
            return bind_info._res_type == EBindResDescType::kConstBuffer &&
                   (bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagInternal || bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagLocal);
        }

        bool IsVectorCompatible(const ShaderBindResourceInfo &bind_info)
        {
            return bind_info._res_type & EBindResDescType::kCBufferFloats ||
                   bind_info._res_type & EBindResDescType::kCBufferInts ||
                   bind_info._res_type & EBindResDescType::kCBufferUInts ||
                   bind_info._res_type == EBindResDescType::kCBufferAttribute;
        }
    }

    Ref<RayTracingShader> RayTracingShader::Create(const WString &sys_path,const String& name)
    {
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                auto shader = MakeRef<RHI::DX12::D3DRayTracingShader>(sys_path);
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
        s_global_textures_bind_info[name] = g_pRenderTexturePool->Get(texture);
    }

    void RayTracingShader::SetScene(RayTracingScene *scene)
    {
        _scene = scene;
        for (auto &[name, bind_info] : _bind_res_infos)
        {
            if (bind_info._res_type == EBindResDescType::kAccelerationStructure)
                bind_info._p_res = scene;
        }
    }

    void RayTracingShader::SetTexture(const String &name, Texture *texture)
    {
        auto it = _bind_res_infos.find(name);
        if (texture != nullptr && it != _bind_res_infos.end())
        {
            it->second._p_res = texture;
            u16 depth_slice = 0u;
            if (texture->Dimension() == ETextureDimension::kTex3D)
                depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
            _bind_params[it->second._bind_slot] = RayTracingBindParams{ECubemapFace::kUnknown, 0u, depth_slice, UINT32_MAX, Texture::kMainSRVIndex, false};
        }
    }

    void RayTracingShader::SetTexture(const String &name, RTHandle handle)
    {
        SetTexture(name, g_pRenderTexturePool->Get(handle));
    }

    void RayTracingShader::SetBuffer(const String &name, ConstantBuffer *buf)
    {
        auto it = _bind_res_infos.find(name);
        if (buf != nullptr && it != _bind_res_infos.end())
        {
            it->second._p_res = buf;
            _bind_params[it->second._bind_slot] = RayTracingBindParams{};
            _bind_params[it->second._bind_slot]._is_internal_cbuf = it->second._bind_flag & ShaderBindResourceInfo::kBindFlagInternal;
        }
    }

    void RayTracingShader::SetBuffer(const String &name, GPUBuffer *buf)
    {
        auto it = _bind_res_infos.find(name);
        if (buf != nullptr && it != _bind_res_infos.end())
        {
            it->second._p_res = buf;
            _bind_params[it->second._bind_slot] = RayTracingBindParams{};
        }
    }

    void RayTracingShader::SetFloat(const String &name, f32 value)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end())
            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, std::min<u16>(sizeof(f32), ShaderBindResourceInfo::GetVariableSize(it->second)));
    }

    void RayTracingShader::SetFloats(const String &name, Vector<f32> values)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end())
        {
            auto &bind_info = it->second;
            const u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
            if (bind_info._array_size > 0)
            {
                const u16 count = std::min<u16>(bind_info._array_size, static_cast<u16>(values.size()));
                for (u16 index = 0; index < count; ++index)
                    memcpy(_cbuf_data + offset + index * 16u, &values[index], sizeof(f32));
            }
            else
            {
                const u16 size = std::min<u16>(ShaderBindResourceInfo::GetVariableSize(bind_info), static_cast<u16>(values.size() * sizeof(f32)));
                memcpy(_cbuf_data + offset, values.data(), size);
            }
        }
    }

    void RayTracingShader::SetBool(const String &name, bool value)
    {
        const u32 bool_value = value ? 1u : 0u;
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end())
            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &bool_value, std::min<u16>(sizeof(bool_value), ShaderBindResourceInfo::GetVariableSize(it->second)));
    }

    void RayTracingShader::SetInt(const String &name, i32 value)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end())
            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, std::min<u16>(sizeof(i32), ShaderBindResourceInfo::GetVariableSize(it->second)));
    }

    void RayTracingShader::SetInts(const String &name, Vector<i32> values)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end())
        {
            auto &bind_info = it->second;
            const u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
            if (bind_info._array_size > 0)
            {
                const u16 count = std::min<u16>(bind_info._array_size, static_cast<u16>(values.size()));
                for (u16 index = 0; index < count; ++index)
                    memcpy(_cbuf_data + offset + index * 16u, &values[index], sizeof(i32));
            }
            else
            {
                const u16 size = std::min<u16>(ShaderBindResourceInfo::GetVariableSize(bind_info), static_cast<u16>(values.size() * sizeof(i32)));
                memcpy(_cbuf_data + offset, values.data(), size);
            }
        }
    }

    void RayTracingShader::SetVector(const String &name, Vector4f vector)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end() && IsVectorCompatible(it->second))
        {
            const u16 offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
            const u16 size = ShaderBindResourceInfo::GetVariableSize(it->second);
            memcpy(_cbuf_data + offset, &vector, size);
        }
    }

    void RayTracingShader::SetVectorArray(const String &name, const Vector<Vector4f> &vectors)
    {
        SetVectorArray(name, const_cast<Vector4f *>(vectors.data()), static_cast<u16>(vectors.size()));
    }

    void RayTracingShader::SetVectorArray(const String &name, Vector4f *vectors, u16 num)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end() && it->second._array_size > 0)
        {
            const u16 array_size = std::min<u16>(it->second._array_size, num);
            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), vectors, array_size * sizeof(Vector4f));
        }
    }

    void RayTracingShader::SetMatrix(const String &name, Matrix4x4f mat)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end())
            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &mat, std::min<u16>(sizeof(Matrix4x4f), ShaderBindResourceInfo::GetVariableSize(it->second)));
    }

    void RayTracingShader::SetMatrixArray(const String &name, Vector<Matrix4x4f> matrix_arr)
    {
        if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end() && it->second._array_size > 0)
        {
            const u16 matrix_num = std::min<u16>(it->second._array_size, static_cast<u16>(matrix_arr.size()));
            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), matrix_arr.data(), sizeof(Matrix4x4f) * matrix_num);
        }
    }

    bool RayTracingShader::IsDependencyFile(const WString &sys_path) const
    {
        return _all_dep_file_pathes.contains(sys_path);
    }

    void RayTracingShader::PushState(RayTracingScene *scene)
    {
        if (scene != nullptr)
            SetScene(scene);

        BindState cur_state;
        for (auto &[name, bind_info] : _bind_res_infos)
        {
            if (bind_info._bind_slot >= cur_state._bind_res.size())
                continue;

            // if (IsInternalConstantBuffer(bind_info))
            // {
            //     ConstantBuffer *cb = ConstBufferPool::Acquire(bind_info._cbuf_size);
            //     cb->SetData(_cbuf_data);
            //     cur_state._bind_res[bind_info._bind_slot] = cb;
            //     cur_state._bind_params[bind_info._bind_slot] = RayTracingBindParams{};
            //     cur_state._bind_params[bind_info._bind_slot]._is_internal_cbuf = true;
            //     cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
            // }
            // else if (bind_info._p_res != nullptr)
            // {
            //     cur_state._bind_res[bind_info._bind_slot] = bind_info._p_res;
            //     if (auto params_it = _bind_params.find(bind_info._bind_slot); params_it != _bind_params.end())
            //         cur_state._bind_params[bind_info._bind_slot] = params_it->second;
            //     cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
            // }
            if (bind_info._p_res != nullptr)
            {    
                cur_state._bind_res[bind_info._bind_slot] = bind_info._p_res;
                cur_state._bind_params[bind_info._bind_slot] = RayTracingBindParams{};
                cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, bind_info._bind_slot);
            }
            else
            {
                continue;
            }

            cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, bind_info._bind_slot);
        }

        for (auto &[name, buffer] : s_global_cbuffer_bind_info)
        {
            if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end() && cur_state._bind_res_priority[it->second._bind_slot] <= PipelineResource::kPriorityGlobal)
            {
                cur_state._bind_res[it->second._bind_slot] = buffer;
                cur_state._bind_params[it->second._bind_slot] = RayTracingBindParams{};
                cur_state._bind_res_priority[it->second._bind_slot] = PipelineResource::kPriorityGlobal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, it->second._bind_slot);
            }
        }
        for (auto &[name, buffer] : s_global_buffer_bind_info)
        {
            if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end() && cur_state._bind_res_priority[it->second._bind_slot] <= PipelineResource::kPriorityGlobal)
            {
                cur_state._bind_res[it->second._bind_slot] = buffer;
                cur_state._bind_params[it->second._bind_slot] = RayTracingBindParams{};
                cur_state._bind_res_priority[it->second._bind_slot] = PipelineResource::kPriorityGlobal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, it->second._bind_slot);
            }
        }
        for (auto &[name, texture] : s_global_textures_bind_info)
        {
            if (auto it = _bind_res_infos.find(name); it != _bind_res_infos.end() && texture != nullptr && cur_state._bind_res_priority[it->second._bind_slot] <= PipelineResource::kPriorityGlobal)
            {
                cur_state._bind_res[it->second._bind_slot] = texture;
                u16 depth_slice = 0u;
                if (texture->Dimension() == ETextureDimension::kTex3D)
                    depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
                cur_state._bind_params[it->second._bind_slot] = RayTracingBindParams{ECubemapFace::kUnknown, 0u, depth_slice, UINT32_MAX, Texture::kMainSRVIndex, false};
                cur_state._bind_res_priority[it->second._bind_slot] = PipelineResource::kPriorityGlobal;
                cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, it->second._bind_slot);
            }
        }

        for (auto &[name, value] : s_global_floats)
            SetFloat(name, value);
        for (auto &[name, value] : s_global_ints)
            SetInt(name, value);

        std::unique_lock lock(_state_mutex);
        cur_state._id = BindState::s_global_id++;
        _bind_state.push(cur_state);
    }

    bool RayTracingShader::Compile(bool is_load_cache)
    {
        _is_compiling.store(true);
        const auto old_bind_infos = _bind_res_infos;
        memcpy(_cache_cbuf_data, _cbuf_data, sizeof(_cbuf_data));
        ShaderVariantHash variant_hash = 0u;
        const bool is_succeed = RHICompileImpl(variant_hash, is_load_cache);
        if (is_succeed)
        {
            for (auto &[name, info] : _bind_res_infos)
            {
                if (auto old_it = old_bind_infos.find(name); old_it != old_bind_infos.end() && old_it->second._p_res != nullptr)
                    info._p_res = old_it->second._p_res;
            }
        }
        _is_compiling.store(false);
        return is_succeed;
    }

    bool RayTracingShader::RHICompileImpl(ShaderVariantHash variant_hash, bool is_load_cache)
    {
        return true;
    }
}