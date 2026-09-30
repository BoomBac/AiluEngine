#include "RHI/DX12/RayTracing/D3DRayTracingShader.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/dxrhelper.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/RenderConstants.h"
#include "RHI/DX12/D3DShaderCompiler.h"
#include "RHI/DX12/D3DShaderReflectionUtils.h"
#include "Render/Buffer.h"
#include "Render/RayTracing/RayTracingScene.h"
#include "Render/Texture.h"
#include "Render/ShaderArtifactCache.h"
#include "Render/ShaderDependencyPath.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/FileManager.h"
#include "pch.h"

namespace Ailu::RHI::DX12
{
    namespace
    {
        bool LoadRayTracingLibrary(const D3DShaderCompileDesc &desc, Render::ShaderVariantHash variant_hash,
                                   D3DShaderCompileOutput &output)
        {
            Render::ShaderArtifactKey key;
            key._type = Render::EShaderArtifactType::kRayTracing;
            key._source_path = ToChar(Render::NormalizeShaderDependencyPath(desc._filename));
            key._variant_hash = variant_hash;
            key._backend = "dx12";
            Vector<Render::ShaderMacro> macros;
            for (const auto &macro : desc._keywords)
                macros.push_back({macro.Name, macro.Definition == nullptr ? "1" : macro.Definition});
            macros.push_back({"SHADER_DXC", "1"});
            macros.push_back({"AL_SHADER_INTEROP_CBUFFER_AS_STRUCT", "1"});
            const auto packaged_interop = PathUtils::ResolveRelPath(
                L"../ShaderInterop.h", PathUtils::Parent(desc._filename)).wstring();
            if (FileManager::Exist(packaged_interop))
                macros.push_back({"AL_PACKAGE_SHADER_INTEROP", "1"});
            key._compile_config_hash = Render::HashCompileConfig(key._backend, desc._target, desc._compile_mode, macros);
            auto &cache = Render::ShaderArtifactCache::Get();
            Render::RayTracingShaderVariantArtifact artifact;
            auto reason = Render::EShaderArtifactInvalidReason::kNone;
            auto load_artifact = [&]()
            {
                if (!desc._is_load_cache || !cache.Load(key, artifact, &reason))
                    return false;
                if (artifact._library._byte_code.empty() || artifact._reflection_blob.empty() ||
                    artifact._dependencies.empty())
                {
                    reason = Render::EShaderArtifactInvalidReason::kCorrupted;
                    return false;
                }
                ComPtr<IDxcUtils> utils;
                ComPtr<ID3DBlob> byte_code;
                ComPtr<ID3D12LibraryReflection> reflection;
                DxcBuffer buffer{};
                buffer.Ptr = artifact._reflection_blob.data();
                buffer.Size = artifact._reflection_blob.size();
                if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
                    FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection))) ||
                    FAILED(D3DCreateBlob(artifact._library._byte_code.size(), &byte_code)))
                {
                    reason = Render::EShaderArtifactInvalidReason::kCorrupted;
                    return false;
                }
                memcpy(byte_code->GetBufferPointer(), artifact._library._byte_code.data(),
                       artifact._library._byte_code.size());
                reason = Render::EShaderArtifactInvalidReason::kNone;
                output._byte_code = byte_code;
                output._library_reflection = reflection;
                output._include_files.clear();
                for (const auto &dependency : artifact._dependencies)
                    output._include_files.insert(Render::NormalizeShaderDependencyPath(dependency._path));
                return true;
            };
            bool used_artifact = load_artifact();
            bool succeeded = used_artifact;
            if (!used_artifact)
            {
                cache.BeginCompile(key);
                try
                {
                    used_artifact = load_artifact();
                    succeeded = used_artifact;
                    if (!used_artifact && CompileDxcStage(desc, output) && output._byte_code != nullptr &&
                        output._library_reflection != nullptr && output._reflection_blob != nullptr)
                    {
                        artifact = {};
                        const auto *byte_code = static_cast<const u8 *>(output._byte_code->GetBufferPointer());
                        artifact._library._byte_code.assign(byte_code, byte_code + output._byte_code->GetBufferSize());
                        const auto *reflection = static_cast<const u8 *>(output._reflection_blob->GetBufferPointer());
                        artifact._reflection_blob.assign(
                            reflection, reflection + output._reflection_blob->GetBufferSize());
                        auto dependencies = Render::NormalizeShaderDependencyPaths(output._include_files);
                        const auto source_path = Render::NormalizeShaderDependencyPath(desc._filename);
                        dependencies.erase(source_path);
                        auto append_dependency = [&](const WString &path)
                        {
                            const auto fingerprint = Render::ShaderFileFingerprintCache::Get().GetFingerprint(path);
                            const auto content_hash = Render::ShaderFileFingerprintCache::Get().GetContentHash(path);
                            artifact._dependencies.push_back(
                                {path, fingerprint._file_size, fingerprint._last_write_time, content_hash});
                        };
                        append_dependency(source_path);
                        for (const auto &path : dependencies)
                            append_dependency(path);
                        if (!cache.Store(key, artifact))
                            LOG_WARNING("RayTracing shader artifact store failed shader={} variant={}",
                                        key._source_path, variant_hash);
                        ++cache.Stats()._compiled_variant_count;
                        succeeded = true;
                    }
                }
                catch (...)
                {
                    cache.EndCompile(key);
                    throw;
                }
                cache.EndCompile(key);
            }
            if (succeeded)
                LOG_INFO("RayTracingShaderLibrary shader={} variant={} source={} reason={}", key._source_path,
                         variant_hash, used_artifact ? "artifact" : "compile",
                         Render::ShaderArtifactInvalidReasonToString(reason));
            return succeeded;
        }

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

    void D3DRayTracingShader::DispatchRays(RHICommandBuffer *cmd, uint w, uint h, uint depth,
                                           const Render::DispatchBindingSnapshot &snapshot)
    {
        const auto it = _dxr_variants.find(snapshot._variant_hash);
        if (it == _dxr_variants.end() || it->second._state_object == nullptr || !snapshot._is_ready)
            return;
        const auto &variant = it->second;
        auto d3dcmd = static_cast<D3DCommandBuffer*>(cmd)->NativeCmdList();
        d3dcmd->SetPipelineState1(variant._state_object.Get());
        d3dcmd->SetComputeRootSignature(variant._global_root_signature.Get());
        if (!BindResources(cmd, snapshot, variant))
            throw HrException(E_INVALIDARG);
        auto dispatch_desc = variant._dispatch_desc;
        dispatch_desc.Width = w;
        dispatch_desc.Height = h;
        dispatch_desc.Depth = depth;
        d3dcmd->DispatchRays(&dispatch_desc);
    }

    bool D3DRayTracingShader::BindResources(RHICommandBuffer *cmd, const Render::DispatchBindingSnapshot &snapshot,
                                            const DxrVariant &variant)
    {
        auto *d3d_cmd = static_cast<D3DCommandBuffer *>(cmd);
        bool scene_bound = false;
        for (u16 index = 0u; index < snapshot._entry_count; ++index)
        {
            const auto &entry = snapshot._entries[index];
            const u16 slot = entry._slot;
            auto *bind_res = Render::GpuResourceRegistry::Get().Resolve(entry._resource);
            if (bind_res == nullptr)
                continue;
            if (entry._resource_type == EBindResDescType::kAccelerationStructure &&
                bind_res == static_cast<Render::GpuResource *>(_scene))
                scene_bound = true;
            BindParams params;
            params._is_compute_pipeline = true;
            params._slot = slot;

            if (entry._resource_type == EBindResDescType::kConstBufferRaw)
            {
                if (!D3DCommandBuffer::IsRootViewAddressValid(entry._addi_info._gpu_handle))
                {
                    LOG_ERROR("D3DRayTracingShader rejected raw constant buffer on slot {} address {:#x}",
                              slot, entry._addi_info._gpu_handle);
                    continue;
                }
                d3d_cmd->NativeCmdList()->SetComputeRootConstantBufferView(slot, entry._addi_info._gpu_handle);
                continue;
            }
            switch (entry._resource_type)
            {
            case EBindResDescType::kTexture2D:
            case EBindResDescType::kCubeMap:
            {
                auto *tex = static_cast<Texture *>(bind_res);
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kTexture3D:
            {
                auto *tex = static_cast<Texture3D *>(bind_res);
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kUAVTexture2D:
            {
                auto *tex = static_cast<Texture2D *>(bind_res);
                params._is_random_access = true;
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
                break;
            }
            case EBindResDescType::kRWTexture3D:
            {
                auto *tex = static_cast<Texture3D *>(bind_res);
                params._is_random_access = true;
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
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

        if (!scene_bound)
        {
            i32 scene_root_slot = -1;
            if (const auto layout_it = _variants.find(snapshot._variant_hash);
                layout_it != _variants.end() && layout_it->second._binding_layout != nullptr)
            {
                const auto *scene_binding = layout_it->second._binding_layout->Find(
                    Render::ShaderPropertyRegistry::Get().Intern("Scene"));
                if (scene_binding != nullptr)
                    scene_root_slot = scene_binding->_bind_slot;
            }
            LOG_ERROR("DXR DispatchRays aborted: Scene TLAS is missing from dispatch snapshot; "
                      "scene={}, expected_root_slot={}, snapshot_entries={}",
                      static_cast<const void *>(_scene), scene_root_slot, snapshot._entry_count);
            return false;
        }

        const auto bindless_srv_base = D3DDescriptorMgr::Get().GetBindlessSRVBaseGpuHandle();
        const auto bindless_uav_base = D3DDescriptorMgr::Get().GetBindlessUAVBaseGpuHandle();
        if (variant._has_bindless_texture2d || variant._has_bindless_buffer ||
            variant._has_bindless_rw_texture2d || variant._has_bindless_rw_buffer)
            d3d_cmd->SetDescriptorHeap(D3DDescriptorMgr::Get().GetBindHeap(), 1024u);
        if (variant._has_bindless_texture2d)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(variant._bindless_texture_slot, bindless_srv_base);
        if (variant._has_bindless_buffer)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(variant._bindless_buffer_slot, bindless_srv_base);
        if (variant._has_bindless_rw_texture2d)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(variant._bindless_rw_texture_slot,
                                                                   bindless_uav_base);
        if (variant._has_bindless_rw_buffer)
            d3d_cmd->NativeCmdList()->SetComputeRootDescriptorTable(variant._bindless_rw_buffer_slot,
                                                                   bindless_uav_base);
        return true;
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

    bool D3DRayTracingShader::RHICompileImpl(Render::ShaderVariantHash variant_hash,
                                             Render::EShaderCompilePolicy policy)
    {
        D3DShaderCompileDesc desc{};
        D3DShaderCompileOutput output;
        desc._filename = _src_file_path;
        desc._target = Render::RenderConstants::kLibModel_6_3;
        desc._is_load_cache = policy == Render::EShaderCompilePolicy::kUseArtifact;
        const auto variant_it = _variants.find(variant_hash);
        if (variant_it == _variants.end())
            return false;
        for (const auto &keyword : variant_it->second._active_keywords)
            desc._keywords.emplace_back(D3D_SHADER_MACRO{keyword.c_str(), "1"});
        if (!LoadRayTracingLibrary(desc, variant_hash, output))
        {
            LOG_ERROR("RayTracingShader: failed to compile raytracing shader library: {} variant={}",
                      ToChar(desc._filename), variant_hash);
            return false;
        }

        _byte_code = output._byte_code;
        for (const auto &include_file : output._include_files)
            RegisterDependencyFile(include_file);
        LoadReflectionInfo(output._library_reflection.Get());
        _dev = dynamic_cast<D3DContext&>(Render::GraphicsContext::Get()).GetDevice();
        GenerateRootSignature();
        _binding_layout = Render::BuildShaderBindingLayout(_bind_res_infos, _binding_layout_version++);
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
        auto &variant = _dxr_variants[variant_hash];
        variant._byte_code = _byte_code;
        variant._state_object = _state_object;
        variant._global_root_signature = _global_root_signature;
        variant._ray_gen_stb = _ray_gen_stb;
        variant._miss_stb = _miss_stb;
        variant._hit_group_stb = _hit_group_stb;
        variant._dispatch_desc = _dispatch_desc;
        variant._has_bindless_texture2d = _has_bindless_texture2d;
        variant._has_bindless_buffer = _has_bindless_buffer;
        variant._has_bindless_rw_texture2d = _has_bindless_rw_texture2d;
        variant._has_bindless_rw_buffer = _has_bindless_rw_buffer;
        variant._bindless_texture_slot = _bindless_texture_slot;
        variant._bindless_buffer_slot = _bindless_buffer_slot;
        variant._bindless_rw_texture_slot = _bindless_rw_texture_slot;
        variant._bindless_rw_buffer_slot = _bindless_rw_buffer_slot;
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
