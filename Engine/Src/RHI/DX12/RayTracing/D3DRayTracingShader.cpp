#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/dxrhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/RenderConstants.h"
#include "RHI/DX12/D3DShaderCompiler.h"
#include "RHI/DX12/D3DShaderReflectionUtils.h"
#include "Render/Buffer.h"
#include "Render/Texture.h"
#include "pch.h"

namespace Ailu::RHI::DX12
{
    namespace
    {
        constexpr u32 kDxrHitAttributeSize = 2u * sizeof(float);
        constexpr u32 kDxrRayPayloadSize = 96u;
        constexpr u32 kDxrMaxRecursionDepth = 1u;

        const Vector<CD3DX12_STATIC_SAMPLER_DESC> &CreateStaticSampler()
        {
            static Vector<CD3DX12_STATIC_SAMPLER_DESC> samplers{
                    CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
                    CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
                    CD3DX12_STATIC_SAMPLER_DESC(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER),
                    CD3DX12_STATIC_SAMPLER_DESC(3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
                    CD3DX12_STATIC_SAMPLER_DESC(4, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
                    CD3DX12_STATIC_SAMPLER_DESC(5, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER),
                    CD3DX12_STATIC_SAMPLER_DESC(6, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 16, D3D12_COMPARISON_FUNC_LESS, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK),
                    CD3DX12_STATIC_SAMPLER_DESC(7, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP)};
            samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            return samplers;
        }

        struct DXILExportDesc
        {
            std::wstring name;
            std::optional<std::wstring> rename;// ExportToRename
        };

        class DXILLibrary
        {
        public:
            DXILLibrary(const void *bytecode, size_t size)
            {
                _bytecode.pShaderBytecode = bytecode;
                _bytecode.BytecodeLength = size;
            }
            // 添加一个导出项，name为在RTPSO中使用的名称，rename为DXIL中的名称，也就是shader中写的（如果不提供则与name相同）
            DXILLibrary &AddExport(std::wstring name, std::optional<std::wstring> rename = std::nullopt)
            {
                _exports.emplace_back(DXILExportDesc{
                        std::move(name),
                        std::move(rename)});
                return *this;
            }

            const D3D12_DXIL_LIBRARY_DESC &GetDesc()
            {
                _native_exports.clear();
                _native_exports.reserve(_exports.size());

                for (auto &e: _exports)
                {
                    D3D12_EXPORT_DESC desc = {};
                    desc.Name = e.name.c_str();
                    desc.ExportToRename = e.rename ? e.rename->c_str() : nullptr;
                    desc.Flags = D3D12_EXPORT_FLAG_NONE;
                    _native_exports.push_back(desc);
                }

                _desc.DXILLibrary = _bytecode;
                _desc.NumExports = (UINT) _native_exports.size();
                _desc.pExports = _native_exports.empty() ? nullptr : _native_exports.data();

                return _desc;
            }

        private:
            D3D12_SHADER_BYTECODE _bytecode{};
            std::vector<DXILExportDesc> _exports;
            std::vector<D3D12_EXPORT_DESC> _native_exports;
            D3D12_DXIL_LIBRARY_DESC _desc{};
        };

        struct HitGroup
        {
            std::wstring _name;
            std::optional<std::wstring> _chs;
            std::optional<std::wstring> _ahs;
            std::optional<std::wstring> _is;
            D3D12_HIT_GROUP_TYPE _type;
        };


        class LocalRootSignature
        {
        public:
            LocalRootSignature(ID3D12Device5 *device, const D3D12_ROOT_SIGNATURE_DESC &desc)
            {
                D3D12_ROOT_SIGNATURE_DESC local_desc = desc;
                local_desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
                ComPtr<ID3DBlob> signature_blob;
                ComPtr<ID3DBlob> error_blob;
                HRESULT hr = D3D12SerializeRootSignature(&local_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
                if (FAILED(hr))
                {
                    if (error_blob)
                    {
                        OutputDebugStringA((char *) error_blob->GetBufferPointer());
                    }
                    throw std::runtime_error("Failed to serialize root signature");
                }

                hr = device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&_root_signature));
                if (FAILED(hr))
                {
                    throw std::runtime_error("Failed to create root signature");
                }
            }
            const void *Data() const { return _root_signature.GetAddressOf(); }

        private:
            ComPtr<ID3D12RootSignature> _root_signature;
        };

        class GlobalRootSignature
        {
        public:
            GlobalRootSignature(ID3D12Device5 *device, const D3D12_ROOT_SIGNATURE_DESC &desc)
            {
                ComPtr<ID3DBlob> signature_blob;
                ComPtr<ID3DBlob> error_blob;
                HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
                if (FAILED(hr))
                {
                    if (error_blob)
                    {
                        OutputDebugStringA((char *) error_blob->GetBufferPointer());
                    }
                    throw std::runtime_error("Failed to serialize root signature");
                }

                hr = device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&_root_signature));
                if (FAILED(hr))
                {
                    throw std::runtime_error("Failed to create root signature");
                }
            }
            ID3D12RootSignature *Get() const { return _root_signature.Get(); }

        private:
            ComPtr<ID3D12RootSignature> _root_signature;
        };

        struct ShaderConfig
        {
            u32 _max_attr_size;
            u32 _max_payload_size;
        };

        struct LocalRootSignatureBinding
        {
            ID3D12RootSignature *_root_signature = nullptr;
            Vector<WString> _export_shaders;// 绑定到哪个shader export
        };

        struct ShaderConfigBinding
        {
            D3D12_RAYTRACING_SHADER_CONFIG _config{};
            Vector<WString> _export_shaders;// 绑定到哪个shader export
        };

        class RTPSOBuilder
        {
        public:
            DXILLibrary &SetDXILibrary(const void *bytecode, size_t size)
            {
                _dxil_libs.emplace_back(bytecode, size);
                return _dxil_libs.back();
            }

            RTPSOBuilder &AddHitGroup(const WString &name, const WString &chs, const WString &ahs, const WString &is, D3D12_HIT_GROUP_TYPE type)
            {
                _hit_groups.emplace_back(name, chs, ahs, is, type);
                return *this;
            }

            RTPSOBuilder &AddHitGroup(const WString &name, const WString &chs, D3D12_HIT_GROUP_TYPE type = D3D12_HIT_GROUP_TYPE_TRIANGLES)
            {
                _hit_groups.emplace_back(name, chs, std::nullopt, std::nullopt, type);
                return *this;
            }

            RTPSOBuilder &AddLocalRootSignature(ID3D12RootSignature *root_signature, const Vector<WString> &export_shaders)
            {
                _lrs_bindings.push_back({root_signature, export_shaders});
                return *this;
            }

            RTPSOBuilder &SetGlobalRootSignature(ID3D12RootSignature *root_signature)
            {
                _global_root_signature = root_signature;
                return *this;
            }

            RTPSOBuilder &AddShaderConfig(u32 max_attr_size, u32 max_payload_size, const Vector<WString> &export_shaders = {})
            {
                auto &binding = _shader_config_bindings.emplace_back();
                binding._config.MaxAttributeSizeInBytes = max_attr_size;
                binding._config.MaxPayloadSizeInBytes = max_payload_size;
                binding._export_shaders = export_shaders;
                return *this;
            }

            RTPSOBuilder &SetMaxRecursionDepth(u32 max_depth)
            {
                _max_depth = max_depth;
                return *this;
            }


            ID3D12StateObject *Build(ID3D12Device5 *device)
            {
                _subobjects.clear();
                _association_descs.clear();
                _association_exports.clear();
                _hit_group_descs.clear();

                size_t association_count = 0;
                for (const auto &binding: _lrs_bindings)
                    association_count += binding._export_shaders.empty() ? 0u : 1u;
                for (const auto &binding: _shader_config_bindings)
                    association_count += binding._export_shaders.empty() ? 0u : 1u;

                const size_t total_subobject_count =
                        _dxil_libs.size() +
                        _hit_groups.size() +
                        _lrs_bindings.size() +
                        _shader_config_bindings.size() +
                        association_count +
                        (_global_root_signature ? 1u : 0u) +
                        1u;

                _subobjects.reserve(total_subobject_count);
                _association_descs.reserve(association_count);
                _association_exports.reserve(association_count);
                _hit_group_descs.reserve(_hit_groups.size());

                // 1. dxil libs
                for (auto &lib: _dxil_libs)
                {
                    auto &desc = lib.GetDesc();

                    D3D12_STATE_SUBOBJECT sub{};
                    sub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
                    sub.pDesc = &desc;

                    _subobjects.push_back(sub);
                }
                // 2. hit groups
                for (auto &hg: _hit_groups)
                {
                    auto &hit_group_desc = _hit_group_descs.emplace_back();
                    hit_group_desc.HitGroupExport = hg._name.c_str();
                    hit_group_desc.ClosestHitShaderImport = hg._chs ? hg._chs->c_str() : nullptr;
                    hit_group_desc.AnyHitShaderImport = hg._ahs ? hg._ahs->c_str() : nullptr;
                    hit_group_desc.IntersectionShaderImport = hg._is ? hg._is->c_str() : nullptr;
                    hit_group_desc.Type = hg._type;

                    D3D12_STATE_SUBOBJECT sub{};
                    sub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
                    sub.pDesc = &hit_group_desc;

                    _subobjects.push_back(sub);
                }
                // 3. local root signature
                for (auto &lrsb: _lrs_bindings)
                {
                    const size_t local_root_signature_subobject_index = _subobjects.size();
                    D3D12_STATE_SUBOBJECT sub{};
                    sub.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
                    sub.pDesc = &lrsb._root_signature;
                    _subobjects.push_back(sub);

                    if (!lrsb._export_shaders.empty())
                    {
                        auto &export_names = _association_exports.emplace_back();
                        export_names.reserve(lrsb._export_shaders.size());
                        for (const auto &export_shader: lrsb._export_shaders)
                            export_names.push_back(export_shader.c_str());

                        auto &association_desc = _association_descs.emplace_back();
                        association_desc.NumExports = static_cast<UINT>(export_names.size());
                        association_desc.pExports = export_names.data();
                        association_desc.pSubobjectToAssociate = &_subobjects[local_root_signature_subobject_index];

                        D3D12_STATE_SUBOBJECT association_sub{};
                        association_sub.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
                        association_sub.pDesc = &association_desc;
                        _subobjects.push_back(association_sub);
                    }
                }
                // 4. shader config
                for (auto &scb: _shader_config_bindings)
                {
                    const size_t shader_config_subobject_index = _subobjects.size();
                    D3D12_STATE_SUBOBJECT sub{};
                    sub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
                    sub.pDesc = &scb._config;
                    _subobjects.push_back(sub);

                    if (!scb._export_shaders.empty())
                    {
                        auto &export_names = _association_exports.emplace_back();
                        export_names.reserve(scb._export_shaders.size());
                        for (const auto &export_shader: scb._export_shaders)
                            export_names.push_back(export_shader.c_str());

                        auto &association_desc = _association_descs.emplace_back();
                        association_desc.NumExports = static_cast<UINT>(export_names.size());
                        association_desc.pExports = export_names.data();
                        association_desc.pSubobjectToAssociate = &_subobjects[shader_config_subobject_index];

                        D3D12_STATE_SUBOBJECT association_sub{};
                        association_sub.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
                        association_sub.pDesc = &association_desc;
                        _subobjects.push_back(association_sub);
                    }
                }

                if (_global_root_signature)
                {
                    D3D12_STATE_SUBOBJECT global_root_signature_sub{};
                    global_root_signature_sub.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
                    global_root_signature_sub.pDesc = &_global_root_signature;
                    _subobjects.push_back(global_root_signature_sub);
                }

                // 5. pipeline config
                _pipeline_config.MaxTraceRecursionDepth = _max_depth;
                D3D12_STATE_SUBOBJECT pipeline_config_sub{};
                pipeline_config_sub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
                pipeline_config_sub.pDesc = &_pipeline_config;
                _subobjects.push_back(pipeline_config_sub);

                D3D12_STATE_OBJECT_DESC desc{};
                desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
                desc.NumSubobjects = (UINT) _subobjects.size();
                desc.pSubobjects = _subobjects.data();

                ID3D12StateObject *result = nullptr;
                ThrowIfFailed(device->CreateStateObject(&desc, IID_PPV_ARGS(&result)));

                PrintStateObjectDesc(&desc);

                return result;
            }

        private:
            Vector<DXILLibrary> _dxil_libs;
            Vector<HitGroup> _hit_groups;
            Vector<LocalRootSignatureBinding> _lrs_bindings;
            Vector<ShaderConfigBinding> _shader_config_bindings;
            Vector<D3D12_HIT_GROUP_DESC> _hit_group_descs;
            Vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> _association_descs;
            Vector<Vector<LPCWSTR>> _association_exports;
            Vector<D3D12_STATE_SUBOBJECT> _subobjects;
            ID3D12RootSignature *_global_root_signature = nullptr;
            D3D12_RAYTRACING_PIPELINE_CONFIG _pipeline_config{};
            u32 _max_depth = 1;
        };
    }// namespace


    using Render::BindParams;
    using Render::EBindResDescType;
    using Render::Texture;
    using Render::Texture2D;
    using Render::Texture3D;
    using Render::ShaderBindResourceInfo;

    const WString c_hitGroupName0 = L"MyHitGroup0";
    const WString c_hitGroupName1 = L"MyHitGroup1";
    const WString c_raygenShaderName = L"MyRaygenShader";
    const WString c_closestHitShaderName0 = L"MyClosestHitShader0";
    const WString c_closestHitShaderName1 = L"MyClosestHitShader1";
    const WString c_missShaderName = L"MyMissShader";
    const WString c_shadowMissShaderName = L"MyShadowMissShader";

    D3DRayTracingShader::D3DRayTracingShader(const WString &sys_path) : RayTracingShader(sys_path)
    {
        Compile();
    }

    D3DRayTracingShader::~D3DRayTracingShader()
    {
    }

    void D3DRayTracingShader::DispatchRays(RHICommandBuffer* cmd,uint w, uint h, uint depth)
    {
        std::unique_lock lock(_state_mutex);
        AL_ASSERT(!_bind_state.empty());
        if (_bind_state.empty() || _state_object == nullptr)
            return;

        auto cur_state = _bind_state.front();
        _bind_state.pop();
        lock.unlock();

        auto d3dcmd = static_cast<D3DCommandBuffer*>(cmd)->NativeCmdList();
        d3dcmd->SetPipelineState1(_state_object.Get());
        d3dcmd->SetComputeRootSignature(_global_root_signature.Get());
        BindResources(cmd, cur_state);
        d3dcmd->DispatchRays(&GetDispatchDesc(w, h, depth));
    }

    void D3DRayTracingShader::BindResources(RHICommandBuffer *cmd, const BindState &state)
    {
        auto *d3d_cmd = static_cast<D3DCommandBuffer *>(cmd);
        for (u16 slot = 0; slot <= state._max_bind_slot; ++slot)
        {
            auto it = std::find_if(_bind_res_infos.begin(), _bind_res_infos.end(), [slot](const auto &entry)
                                   { return entry.second._bind_slot == slot; });
            if (it == _bind_res_infos.end())
                continue;

            auto &bind_info = it->second;
            auto *bind_res = state._bind_res[slot];
            if (bind_res == nullptr)
                continue;

            d3d_cmd->MarkUsedResource(bind_res);
            const auto &view_info = state._bind_params[slot];
            BindParams params;
            params._is_compute_pipeline = true;
            params._slot = slot;

            switch (bind_info._res_type)
            {
            case EBindResDescType::kTexture2D:
            {
                auto *tex = static_cast<Texture2D *>(bind_res);
                params._params._texture_binder._sub_res = view_info._sub_res;
                params._params._texture_binder._view_idx = view_info._view_index == static_cast<u16>(-1)
                                                           ? tex->CalculateViewIndex(Texture::ETextureViewType::kSRV, view_info._face, view_info._mipmap, 0)
                                                           : view_info._view_index;
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kTexture3D:
            {
                auto *tex = static_cast<Texture3D *>(bind_res);
                params._params._texture_binder._sub_res = view_info._sub_res;
                params._params._texture_binder._view_idx = view_info._view_index == static_cast<u16>(-1)
                                                           ? tex->CalculateViewIndex(Texture::ETextureViewType::kSRV, view_info._face, view_info._mipmap, view_info._slice)
                                                           : view_info._view_index;
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kUAVTexture2D:
            {
                auto *tex = static_cast<Texture2D *>(bind_res);
                params._is_random_access = true;
                params._params._texture_binder._sub_res = view_info._sub_res;
                params._params._texture_binder._view_idx = tex->CalculateViewIndex(Texture::ETextureViewType::kUAV, view_info._face, view_info._mipmap, 0);
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kRWTexture3D:
            {
                auto *tex = static_cast<Texture3D *>(bind_res);
                params._is_random_access = true;
                params._params._texture_binder._sub_res = view_info._sub_res;
                params._params._texture_binder._view_idx = tex->CalculateViewIndex(Texture::ETextureViewType::kUAV, view_info._face, view_info._mipmap, view_info._slice);
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kRWBuffer:
                params._is_random_access = true;
                bind_res->Bind(cmd, params);
                break;
            case EBindResDescType::kBuffer:
            case EBindResDescType::kConstBuffer:
            case EBindResDescType::kAccelerationStructure:
                bind_res->Bind(cmd, params);
                break;
            default:
                break;
            }
        }

        const auto bindless_srv_base = D3DDescriptorMgr::Get().GetBindlessSRVBaseGpuHandle();
        const auto bindless_uav_base = D3DDescriptorMgr::Get().GetBindlessUAVBaseGpuHandle();
        if (_has_bindless_texture2d)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(_bindless_texture_slot, bindless_srv_base);
        if (_has_bindless_buffer)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(_bindless_buffer_slot, bindless_srv_base);
        if (_has_bindless_rw_texture2d)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(_bindless_rw_texture_slot, bindless_uav_base);
        if (_has_bindless_rw_buffer)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(_bindless_rw_buffer_slot, bindless_uav_base);
    }

    const D3D12_DISPATCH_RAYS_DESC& D3DRayTracingShader::GetDispatchDesc(u32 w,u32 h, u32 depth)
    {
        //_dispatch_desc.RayGenerationShaderRecord.SizeInBytes = _ray_gen_stb->GetDesc().Width;
        //_dispatch_desc.RayGenerationShaderRecord.StartAddress = _ray_gen_stb->GetGPUVirtualAddress();
        //_dispatch_desc.MissShaderTable.SizeInBytes = _miss_stb->GetDesc().Width;
        //_dispatch_desc.MissShaderTable.StartAddress = _miss_stb->GetGPUVirtualAddress();
        //_dispatch_desc.HitGroupTable.SizeInBytes = _hit_group_stb->GetDesc().Width;
        //_dispatch_desc.HitGroupTable.StartAddress = _hit_group_stb->GetGPUVirtualAddress();
        _dispatch_desc.Width = w;
        _dispatch_desc.Height = h;
        _dispatch_desc.Depth = depth;
        return _dispatch_desc;
    }

    bool D3DRayTracingShader::RHICompileImpl(Render::ShaderVariantHash variant_hash, bool is_load_cache)
    {
        D3DShaderCompileDesc desc{};
        D3DShaderCompileOutput output;
        desc._filename = _src_file_path;
        desc._target = Render::RenderConstants::kLibModel_6_3;
        desc._is_load_cache = is_load_cache;
        if (!CreateFromFileDXC(desc, output) || output._byte_code == nullptr || output._library_reflection == nullptr)
        {
            LOG_ERROR("RayTracingShader: failed to compile raytracing shader library: {}", ToChar(desc._filename));
            _is_valid = false;
            return false;
        }

        _byte_code = output._byte_code;
        _all_dep_file_pathes.clear();
        _all_dep_file_pathes.insert(_src_file_path);
        _all_dep_file_pathes.insert(output._include_files.begin(), output._include_files.end());
        LoadReflectionInfo(output._library_reflection.Get());
        _dev = dynamic_cast<D3DContext&>(Render::GraphicsContext::Get()).GetDevice();
        GenerateRootSignature();
        RTPSOBuilder builder;

        auto& lib = builder.SetDXILibrary(_byte_code->GetBufferPointer(), _byte_code->GetBufferSize());
        lib.AddExport(c_raygenShaderName)
           .AddExport(c_closestHitShaderName0)
           .AddExport(c_closestHitShaderName1)
           .AddExport(c_missShaderName)
           .AddExport(c_shadowMissShaderName);

        builder.AddHitGroup(c_hitGroupName0, c_closestHitShaderName0)
            .AddHitGroup(c_hitGroupName1, c_closestHitShaderName1)
            .AddShaderConfig(kDxrHitAttributeSize, kDxrRayPayloadSize)
            .SetGlobalRootSignature(_global_root_signature.Get())
            .SetMaxRecursionDepth(kDxrMaxRecursionDepth);

        _state_object.Attach(builder.Build(_dev));
        _state_object->SetName(ToWChar(Name()).c_str());
        BuildShaderTables();
                // Since each shader table has only one shader record, the stride is same as the size.
        _dispatch_desc.HitGroupTable.StartAddress = _hit_group_stb->GetGPUVirtualAddress();
        _dispatch_desc.HitGroupTable.SizeInBytes = _hit_group_stb->GetDesc().Width;
        _dispatch_desc.HitGroupTable.StrideInBytes = _hit_group_shader_record_size;
        _dispatch_desc.MissShaderTable.StartAddress = _miss_stb->GetGPUVirtualAddress();
        _dispatch_desc.MissShaderTable.SizeInBytes = _miss_stb->GetDesc().Width;
        _dispatch_desc.MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        _dispatch_desc.RayGenerationShaderRecord.StartAddress = _ray_gen_stb->GetGPUVirtualAddress();
        _dispatch_desc.RayGenerationShaderRecord.SizeInBytes = _ray_gen_stb->GetDesc().Width;
        _is_valid = true;
        return true;
    }

    void D3DRayTracingShader::LoadReflectionInfo(ID3D12LibraryReflection *reflection)
    {
        _bind_res_infos.clear();
        ShaderReflectionUtils::AppendLibraryResources(reflection, _bind_res_infos);
    }

    void D3DRayTracingShader::BuildShaderTables()
    {
        const auto& dev = dynamic_cast<D3DContext&>(Render::GraphicsContext::Get()).GetDevice();

        void *rayGenShaderIdentifier;
        void *missShaderIdentifier;
        void *shadowMissShaderIdentifier;
        void *hitGroupShaderIdentifier0;
        void *hitGroupShaderIdentifier1;

        auto GetShaderIdentifiers = [&](auto *stateObjectProperties)
        {
            rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName.c_str());
            missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName.c_str());
            shadowMissShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_shadowMissShaderName.c_str());
            hitGroupShaderIdentifier0 = stateObjectProperties->GetShaderIdentifier(c_hitGroupName0.c_str());
            hitGroupShaderIdentifier1 = stateObjectProperties->GetShaderIdentifier(c_hitGroupName1.c_str());
        };

        // Get shader identifiers.
        UINT shaderIdentifierSize;
        {
            ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
            ThrowIfFailed(_state_object.As(&stateObjectProperties));
            GetShaderIdentifiers(stateObjectProperties.Get());
            shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        }

        // Ray gen shader table
        {
            UINT numShaderRecords = 1;
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable rayGenShaderTable(dev, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
            rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
            _ray_gen_stb = rayGenShaderTable.GetResource();
        }

        // Miss shader table
        {
            UINT numShaderRecords = 2;
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable missShaderTable(dev, numShaderRecords, shaderRecordSize, L"MissShaderTable");
            missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
            missShaderTable.push_back(ShaderRecord(shadowMissShaderIdentifier, shaderIdentifierSize));
            _miss_stb = missShaderTable.GetResource();
        }

        // Hit group shader table
        {
            UINT numShaderRecords = 2;
            UINT shaderRecordSize = shaderIdentifierSize;
            _hit_group_shader_record_size = shaderRecordSize;
            ShaderTable hitGroupShaderTable(dev, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier0, shaderIdentifierSize));
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier1, shaderIdentifierSize));
            _hit_group_stb = hitGroupShaderTable.GetResource();
        }
    }
    
    void D3DRayTracingShader::GenerateRootSignature()
    {
        auto static_samplers = CreateStaticSampler();
        ShaderReflectionUtils::GenerateRootSignature(_dev, _bind_res_infos, &_global_root_signature, false, static_samplers);

        _has_bindless_texture2d = false;
        _has_bindless_buffer = false;
        _has_bindless_rw_texture2d = false;
        _has_bindless_rw_buffer = false;
        _bindless_texture_slot = static_cast<u16>(-1);
        _bindless_buffer_slot = static_cast<u16>(-1);
        _bindless_rw_texture_slot = static_cast<u16>(-1);
        _bindless_rw_buffer_slot = static_cast<u16>(-1);
        if (auto it = _bind_res_infos.find("g_bindless_texture2d"); it != _bind_res_infos.end())
        {
            _has_bindless_texture2d = true;
            _bindless_texture_slot = it->second._bind_slot;
        }
        if (auto it = _bind_res_infos.find("g_bindless_buffer"); it != _bind_res_infos.end())
        {
            _has_bindless_buffer = true;
            _bindless_buffer_slot = it->second._bind_slot;
        }
        if (auto it = _bind_res_infos.find("g_bindless_rw_texture2d"); it != _bind_res_infos.end())
        {
            _has_bindless_rw_texture2d = true;
            _bindless_rw_texture_slot = it->second._bind_slot;
        }
        if (auto it = _bind_res_infos.find("g_bindless_rw_buffer"); it != _bind_res_infos.end())
        {
            _has_bindless_rw_buffer = true;
            _bindless_rw_buffer_slot = it->second._bind_slot;
        }

        for (auto &[name, bind_info] : _bind_res_infos)
        {
            if (bind_info._res_type == EBindResDescType::kConstBuffer &&
                (bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagInternal || bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagLocal))
            {
                _internal_cbuf_name = bind_info._name;
                break;
            }
        }
    }
}// namespace Ailu::RHI::DX12