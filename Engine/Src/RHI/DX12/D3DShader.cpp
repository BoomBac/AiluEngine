#include "pch.h"
//#include <atlcomcli.h>

#include "Framework/Common/Log.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Utils.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/D3DShaderCompiler.h"
#include "RHI/DX12/D3DShaderReflectionUtils.h"
#include "RHI/DX12/D3DRootSignaturePool.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/ShaderArtifactCache.h"
#include "Render/ShaderDependencyPath.h"

#define SHADER_DXC 1

namespace Ailu::RHI::DX12
{
    static D3D12_PRIMITIVE_TOPOLOGY ConvertTopologyToType(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
    {
        switch (type)
        {
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
                return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
                return D3D12_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_LINELIST;
            default:
                return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
        return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

    static EShaderDateType GetShaderDataType(DXGI_FORMAT dx_format)
    {
        switch (dx_format)
        {
            case DXGI_FORMAT_R32G32B32_FLOAT:
                return EShaderDateType::kFloat3;
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
                return EShaderDateType::kFloat4;
            case DXGI_FORMAT_R32G32_FLOAT:
                return EShaderDateType::kFloat2;
        }
        AL_ASSERT_MSG(false, "Unsupported DXGI_FORMAT to ShaderDataType!");
        //LOG_ERROR("Unsupported DXGI_FORMAT to ShaderDataType!");
        return EShaderDateType::kBool;
    }

    static const Vector<CD3DX12_STATIC_SAMPLER_DESC> &CreateStaticSampler()
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

    static ComPtr<ID3DBlob> CreateBlobFromBytes(const Vector<u8> &bytes)
    {
        if (bytes.empty())
            return nullptr;
        ComPtr<ID3DBlob> blob;
        if (FAILED(D3DCreateBlob(bytes.size(), blob.GetAddressOf())))
            return nullptr;
        std::memcpy(blob->GetBufferPointer(), bytes.data(), bytes.size());
        return blob;
    }

    static Vector<Render::ShaderMacro> ConvertShaderMacros(const Vector<D3D_SHADER_MACRO> &macros)
    {
        Vector<Render::ShaderMacro> result;
        for (const auto &macro : macros)
        {
            if (macro.Name == nullptr)
                continue;
            result.emplace_back(Render::ShaderMacro{macro.Name, macro.Definition == nullptr ? "1" : macro.Definition});
        }
        return result;
    }

    static Render::ShaderArtifactKey BuildShaderArtifactKey(Render::EShaderArtifactType type, const WString &source_path,
                                                             const String &pass_or_kernel_name, u16 index,
                                                             ShaderVariantHash variant_hash, const String &shader_model,
                                                             const Vector<D3D_SHADER_MACRO> &macros)
    {
        auto shader_macros = ConvertShaderMacros(macros);
        shader_macros.emplace_back(Render::ShaderMacro{"SHADER_DXC", "1"});
        const WString packaged_shader_interop = PathUtils::ResolveRelPath(L"../ShaderInterop.h", PathUtils::Parent(source_path)).wstring();
        if (FileManager::Exist(packaged_shader_interop))
            shader_macros.emplace_back(Render::ShaderMacro{"AL_PACKAGE_SHADER_INTEROP", "1"});
        Render::ShaderArtifactKey key;
        key._type = type;
        key._source_path = ToChar(PathUtils::FormatFilePath(source_path));
        key._pass_or_kernel_name = pass_or_kernel_name;
        key._pass_or_kernel_index = index;
        key._variant_hash = variant_hash;
        key._backend = "dx12";
        key._shader_artifact_version = Render::kShaderArtifactVersion;
        key._shader_abi_version = Render::kShaderAbiVersion;
        key._compiler_version = Render::kDxcCompilerVersion;
        key._compile_config_hash = Render::HashCompileConfig(key._backend, shader_model,
                                                              Render::kRuntimeShaderCompileMode,
                                                              shader_macros);
        return key;
    }

    static Render::ShaderResourceBindingArtifact MakeBindingArtifact(const String &name,
                                                                       const ShaderBindResourceInfo &info)
    {
        Render::ShaderResourceBindingArtifact binding;
        binding._name = name;
        binding._property_id = info._property_id;
        binding._resource_type = static_cast<u32>(info._res_type);
        binding._bind_slot = info._bind_slot;
        binding._register_space = info._register_space;
        binding._bind_flag = info._bind_flag;
        binding._array_size = info._array_size;
        binding._constant_buffer_size = info._cbuf_size;
        if (info._res_type & EBindResDescType::kCBufferAttribute)
        {
            binding._buffer_offset = ShaderBindResourceInfo::GetVariableOffset(info);
            binding._buffer_size = ShaderBindResourceInfo::GetVariableSize(info);
            binding._root_constant_buffer_name = info._p_root_cbuf == nullptr ? String{} : info._p_root_cbuf->_name;
        }
        else
        {
            binding._resource_slot = info._res_slot;
        }
        return binding;
    }

    static void AppendArtifactDependencies(const std::set<WString> &paths,
                                           Vector<Render::ShaderArtifactDependency> &dependencies)
    {
        std::set<WString> unique_paths;
        for (const auto &path : paths)
        {
            const WString normalized_path = Render::NormalizeShaderDependencyPath(path);
            if (normalized_path.empty() || !unique_paths.insert(normalized_path).second)
                continue;
            const auto &fingerprint = Render::ShaderFileFingerprintCache::Get().GetFingerprint(normalized_path);
            if (!fingerprint._exists)
                continue;
            const u64 content_hash = Render::ShaderFileFingerprintCache::Get().GetContentHash(normalized_path);
            dependencies.emplace_back(Render::ShaderArtifactDependency{normalized_path, fingerprint._file_size,
                                                                       fingerprint._last_write_time, content_hash});
        }
    }

    static void MoveSourceDependencyFirst(const WString &source_path,
                                          Vector<Render::ShaderArtifactDependency> &dependencies)
    {
        const WString normalized_source_path = Render::NormalizeShaderDependencyPath(source_path);
        const auto source_it = std::find_if(dependencies.begin(), dependencies.end(), [&](const auto &dependency)
        {
            return dependency._path == normalized_source_path;
        });
        if (source_it != dependencies.end() && source_it != dependencies.begin())
            std::iter_swap(dependencies.begin(), source_it);
    }

    static bool FillBindingInfo(const Render::ShaderResourceBindingArtifact &artifact,
                                ShaderBindResourceInfo &info)
    {
        info._res_type = static_cast<EBindResDescType>(artifact._resource_type);
        info._bind_slot = static_cast<u8>(artifact._bind_slot);
        info._name = artifact._name;
        info._property_id = ShaderPropertyRegistry::Get().Intern(info._name);
        info._register_space = artifact._register_space;
        info._bind_flag = artifact._bind_flag;
        info._array_size = static_cast<u8>(artifact._array_size);
        info._cbuf_size = artifact._constant_buffer_size;
        if (info._res_type & EBindResDescType::kCBufferAttribute)
        {
            info._cbuf_member_offset = (artifact._buffer_offset << 16u) | (artifact._buffer_size & 0xffffu);
        }
        else
        {
            info._res_slot = artifact._resource_slot;
        }
        return true;
    }

    static void HydrateBindingMap(const Vector<Render::ShaderResourceBindingArtifact> &bindings,
                                  ShaderReflectionResourceMap &bind_res_infos)
    {
        bind_res_infos.clear();
        for (const auto &binding : bindings)
        {
            ShaderBindResourceInfo info;
            FillBindingInfo(binding, info);
            bind_res_infos.emplace(binding._name, std::move(info));
        }
        for (const auto &binding : bindings)
        {
            if (binding._root_constant_buffer_name.empty())
                continue;
            auto binding_it = bind_res_infos.find(binding._name);
            auto root_it = bind_res_infos.find(binding._root_constant_buffer_name);
            if (binding_it != bind_res_infos.end() && root_it != bind_res_infos.end())
                binding_it->second._p_root_cbuf = &root_it->second;
        }
    }

    static void ParserBindResourceAddiInfo(HashMap<String, ShaderBindResourceInfo> &bind_res_infos, String line, bool is_in_cbuf_scope)
    {
        line = line.find(";") != line.npos ? line.substr(0, line.find_first_of(";") + 1) : line;
        if (su::BeginWith(line, "Texture2D"))
        {
            size_t name_begin = line.find_first_of("D") + 1;
            size_t name_end = line.find_first_of(":") - 1;
            String tex_name = line.substr(name_begin, name_end - name_begin);
        }
        else if (su::BeginWith(line, "TextureCube"))
        {
            size_t name_begin = line.find_last_of("e") + 1;
            size_t name_end = line.find_first_of(":") - 1;
            String tex_name = line.substr(name_begin, name_end - name_begin);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kCubeMap;
            }
        }
        else if (su::BeginWith(line, "TEXTURECUBE"))
        {
            size_t name_begin = line.find_first_of("(") + 1;
            size_t name_end = line.find_last_of(",");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kCubeMap;
            }
        }
        else if (su::BeginWith(line, "Texture3D"))
        {
            auto eol = line.find_first_of("<") == line.length() ? "D" : ">";
            size_t name_begin = line.find_first_of(eol) + 1;
            size_t name_end = line.find_first_of(";");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kTexture3D;
            }
        }
        else if (su::BeginWith(line, "TEXTURE3D"))
        {
            size_t name_begin = line.find_first_of("(") + 1;
            size_t name_end = line.find_first_of(")");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kTexture3D;
            }
        }
        else if (su::BeginWith(line, "RWTexture3D"))
        {
            auto eol = line.find_first_of("<") == line.length() ? "D" : ">";
            size_t name_begin = line.find_first_of(eol) + 1;
            size_t name_end = line.find_first_of(";");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kRWTexture3D;
            }
        }
        else if (su::BeginWith(line, "RWTEXTURE3D"))//RWTEXTURE3D(_OutNoise3D,float4)
        {
            size_t name_begin = line.find_first_of('(') + 1;
            size_t name_end = line.find_first_of(",");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kRWTexture3D;
            }
        }
        else if (su::BeginWith(line, "RWBuffer"))
        {
            auto eol = line.find_first_of("<") == line.length() ? "r" : ">";
            size_t name_begin = line.find_first_of(eol) + 1;
            size_t name_end = line.find_first_of(";");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kRWBuffer;
            }
        }
        else if (su::BeginWith(line, "uint"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._array_size = array_size;
                    if (type_name == "uint4" || type_name == "uint3" || type_name == "uint2")
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferUInts);
                    else
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferUInt);
                }
            }
        }
        else if (su::BeginWith(line, "int"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._array_size = array_size;
                    if (type_name == "int4" || type_name == "int3" || type_name == "int2")
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferInts);
                    else
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferInt);
                }
            }
        }
        else if (su::BeginWith(line, "float") && !su::BeginWith(line, "float4x4"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    if (type_name == "float")
                    {
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute |
                                                          (array_size > 1u ? EBindResDescType::kCBufferFloats :
                                                                             EBindResDescType::kCBufferFloat));
                    }
                    else
                    {
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute |
                                                          EBindResDescType::kCBufferFloats);
                    }
                    c._array_size = array_size;
                }
            }
        }
        else if (su::BeginWith(line, "bool"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferBool);
                }
            }
        }
        else if (su::BeginWith(line, "float4x4"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferMatrix);
                    c._array_size = array_size;
                }
            }
        }
        else if (line.find("[") != line.npos && is_in_cbuf_scope)//cbuf内结构体解析支持
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferFloats);
                    c._array_size = array_size;
                }
            }
        }
        else
        {
        };
    }

    static bool IsValidMacroName(const String &s)
    {
        if (s.empty()) return false;
        if (!(isalpha(s[0]) || s[0] == '_')) return false;
        for (char c: s)
            if (!(isalnum(c) || c == '_'))
                return false;
        return true;
    }


    static Vector<D3D_SHADER_MACRO> ConstructVariantMarcos(const std::set<String> &kw_seqs)
    {
        Vector<D3D_SHADER_MACRO> v;
        for (const auto &kw: Shader::GetPreDefinedMacros())
            v.emplace_back(D3D_SHADER_MACRO{kw.c_str(), "1"});
        for (auto &kw: kw_seqs)
        {
            if (kw != "_")
            {
                v.emplace_back(D3D_SHADER_MACRO{kw.c_str(), "1"});
            }
        }
        v.emplace_back(D3D_SHADER_MACRO{NULL, NULL});
        for (auto &m: v)
        {
            if (m.Name)
            {
                if (!IsValidMacroName(m.Name))
                {
                    LOG_ERROR("Invalid macro name in shader variant: {}", m.Name);
                }
            }
        }

        return v;
    }

#pragma endregion

#pragma region D3DShader
    D3DShader::D3DShader(const WString &sys_path) : Shader(sys_path)
    {
        Compile();
    }
    D3DShader::~D3DShader()
    {
    }

    void D3DShader::GenerateInternalPSO(u16 pass_index, ShaderVariantHash variant_hash,
                                        Vector<u8> *serialized_signature)
    {
        AL_ASSERT(pass_index < _passes.size());
        AL_ASSERT(_passes[pass_index]._variants.contains(variant_hash));
        AL_ASSERT(pass_index < _pass_elements.size());
        AL_ASSERT(_pass_elements[pass_index]._variants.contains(variant_hash));
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        auto device = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice();
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) { featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; }
        CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
        auto& rootParameters = _pass_elements[pass_index]._variants[variant_hash]._root_parameters;
        rootParameters.resize(32);
        int cbuf_mask = 0, texture_count = 0;
        auto &pass_variant = _passes[pass_index]._variants[variant_hash];
        auto &variant_bind_res_info = pass_variant._bind_res_infos;
        for (auto it = variant_bind_res_info.begin(); it != variant_bind_res_info.end(); it++)
        {
            auto &desc = it->second;//if (desc._res_type == EBindResDescType::kCBufferAttribute) continue;
            if (desc._res_type == EBindResDescType::kConstBuffer)
            {
                if (desc._name == RenderConstants::kCBufNamePrimitiveDraw) cbuf_mask |= 0x01;
                else if (desc._name == RenderConstants::kCBufNamePerObject) cbuf_mask |= 0x10;
                else if (desc._name == RenderConstants::kCBufNamePerMaterial)
                    cbuf_mask |= 0x02;
                else if (desc._name == RenderConstants::kCBufNamePerCamera)
                    cbuf_mask |= 0x04;
                else if (desc._name == RenderConstants::kCBufNamePerScene)
                    cbuf_mask |= 0x08;
            }
        }
        u8 root_param_index = 0;
        if (cbuf_mask & 0x01)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePrimitiveDraw]._bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstants(4u, 0u);
        }
        else if (cbuf_mask & 0x10)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerObject]._bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(0u);
        }
        if (cbuf_mask & 0x02)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerMaterial]._bind_slot = root_param_index;
            pass_variant._per_mat_buf_bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(1u);
        }
        if (cbuf_mask & 0x04)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerCamera]._bind_slot = root_param_index;
            pass_variant._per_pass_buf_bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(3u);
        }
        if (cbuf_mask & 0x08)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerScene]._bind_slot = root_param_index;
            pass_variant._per_frame_buf_bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(2u);
        }
        for (auto it = variant_bind_res_info.begin(); it != variant_bind_res_info.end(); it++)
        {
            auto &desc = it->second;
            switch (desc._res_type)
            {
                case EBindResDescType::kTexture2D:
                case EBindResDescType::kUAVTexture2D:
                case EBindResDescType::kTexture3D:
                case EBindResDescType::kCubeMap:
                {
                    ++texture_count;
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index;
                    ++root_param_index;
                }
                break;
                case EBindResDescType::kRWBuffer:
                {
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index;
                    ++root_param_index;
                }
                break;
                case EBindResDescType::kBuffer:
                {
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index;
                    ++root_param_index;
                }
                break;
                default:
                    break;
            }
        }
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
        if (_pass_elements[pass_index]._variants[variant_hash]._p_gblob == nullptr) rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        auto samplers = CreateStaticSampler();
        rootSignatureDesc.Init_1_1(root_param_index, rootParameters.data(), static_cast<UINT>(samplers.size()), samplers.data(), rootSignatureFlags);
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        auto hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if (error) { LOG_ERROR("Root signature error: {}", (const char *) error->GetBufferPointer()); }
        ThrowIfFailed(hr);
        Vector<u8> signature_bytes(signature->GetBufferSize());
        std::memcpy(signature_bytes.data(), signature->GetBufferPointer(), signature_bytes.size());
        if (serialized_signature != nullptr)
            *serialized_signature = signature_bytes;
        const u64 signature_hash = Render::HashArtifactBytes(signature_bytes);
        _pass_elements[pass_index]._variants[variant_hash]._p_sig =
            D3DRootSignaturePool::Get().GetOrCreate(signature_hash, signature_bytes);
        ThrowIfFailed(_pass_elements[pass_index]._variants[variant_hash]._p_sig != nullptr ? S_OK : E_FAIL);
    }

    bool D3DShader::RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash, Render::EShaderCompilePolicy policy)
    {
        const bool is_load_cache = policy == Render::EShaderCompilePolicy::kUseArtifact;
        if (pass_index >= _passes.size() || !_passes[pass_index]._variants.contains(variant_hash))
            return false;
        if (!_is_pass_elements_init.load())
        {
            Reset();
            _is_pass_elements_init.store(true);
        }

        auto &pass = _passes[pass_index];
        const auto keyword_defines = ConstructVariantMarcos(pass._variants[variant_hash]._active_keywords);
        const auto key = BuildShaderArtifactKey(Render::EShaderArtifactType::kGraphics, _src_file_path, pass._name,
                                                pass_index, variant_hash, "vs6.1|ps6.1|gs6.1", keyword_defines);
        auto &artifact_cache = Render::ShaderArtifactCache::Get();
        Render::GraphicsShaderVariantArtifact artifact;
        Render::EShaderArtifactInvalidReason reason = Render::EShaderArtifactInvalidReason::kNone;
        if (is_load_cache && artifact_cache.Load(key, artifact, &reason) && HydrateGraphicsVariant(pass_index, variant_hash, artifact))
        {
            LOG_INFO("ShaderVariant READY shader={} pass={} variant={} source=artifact reason={}", _name, pass_index,
                     variant_hash, Render::ShaderArtifactInvalidReasonToString(reason));
            return true;
        }
        if (is_load_cache && artifact_cache.WaitForCompile(key) &&
            artifact_cache.Load(key, artifact, &reason) && HydrateGraphicsVariant(pass_index, variant_hash, artifact))
        {
            LOG_INFO("ShaderVariant READY shader={} pass={} variant={} source=artifact reason={}", _name, pass_index,
                     variant_hash, Render::ShaderArtifactInvalidReasonToString(reason));
            return true;
        }

        artifact_cache.BeginCompile(key);
        bool succeed = false;
        bool used_artifact = false;
        try
        {
            if (is_load_cache && artifact_cache.Load(key, artifact, &reason) && HydrateGraphicsVariant(pass_index, variant_hash, artifact))
            {
                succeed = true;
                used_artifact = true;
            }
            else
            {
                ComPtr<ID3DBlob> tmp_p_vblob;
                ComPtr<ID3DBlob> tmp_p_pblob;
                ComPtr<ID3DBlob> tmp_p_gblob;
                ComPtr<ID3D12ShaderReflection> tmp_p_vreflect, tmp_p_preflect, tmp_p_greflect;
                std::set<WString> dependency_files = pass._source_files;
                bool compile_succeed = true;
#ifdef SHADER_DXC
                compile_succeed &= CreateFromFileDXC(pass._vert_src_file, pass._vert_entry, RenderConstants::kVSModel_6_1,
                                                      keyword_defines, tmp_p_vblob, tmp_p_vreflect, dependency_files, false);
                compile_succeed &= CreateFromFileDXC(pass._pixel_src_file, pass._pixel_entry, RenderConstants::kPSModel_6_1,
                                                      keyword_defines, tmp_p_pblob, tmp_p_preflect, dependency_files, false);
                if (!pass._geometry_entry.empty())
                    compile_succeed &= CreateFromFileDXC(pass._geom_src_file, pass._geometry_entry, RenderConstants::kGSModel_6_1,
                                                          keyword_defines, tmp_p_gblob, tmp_p_greflect, dependency_files, false);
#else
                compile_succeed &= CreateFromFileFXC(pass._vert_src_file, pass._vert_entry, RenderConstants::kVSModel_5_0,
                                                      keyword_defines, tmp_p_vblob, tmp_p_vreflect, dependency_files, false);
                compile_succeed &= CreateFromFileFXC(pass._pixel_src_file, pass._pixel_entry, RenderConstants::kPSModel_5_0,
                                                      keyword_defines, tmp_p_pblob, tmp_p_preflect, dependency_files, false);
                if (!pass._geometry_entry.empty())
                    compile_succeed &= CreateFromFileFXC(pass._geom_src_file, pass._geometry_entry, RenderConstants::kGSModel_5_0,
                                                          keyword_defines, tmp_p_gblob, tmp_p_greflect, dependency_files, false);
#endif
                if (compile_succeed && tmp_p_vreflect != nullptr && tmp_p_preflect != nullptr)
                {
                    auto &element = _pass_elements[pass_index]._variants[variant_hash];
                    element = D3DShaderElement::D3DVariantElement();
                    element._p_vblob = tmp_p_vblob;
                    element._p_pblob = tmp_p_pblob;
                    element._p_gblob = tmp_p_gblob;
                    element._keyword_defines = keyword_defines;
                    pass._source_files = Render::NormalizeShaderDependencyPaths(dependency_files);
                    LoadShaderReflection(pass_index, variant_hash, tmp_p_vreflect.Get(), tmp_p_preflect.Get());
                    Render::GraphicsShaderVariantArtifact new_artifact;
                    GenerateInternalPSO(pass_index, variant_hash, &new_artifact._root_signature_blob);
                    BuildBindingLayout(pass_index, variant_hash);
                    BuildGraphicsArtifact(pass_index, variant_hash, keyword_defines, key, new_artifact);
                    new_artifact._header._type = Render::EShaderArtifactType::kGraphics;
                    new_artifact._header._artifact_key = key.Hash();
                    new_artifact._root_signature_hash = Render::HashArtifactBytes(new_artifact._root_signature_blob);
                    artifact_cache.Store(key, new_artifact);
                    ++artifact_cache.Stats()._compiled_variant_count;
                    succeed = true;
                }
            }
        }
        catch (const std::exception &error)
        {
            LOG_ERROR("Compile shader {} failed: {}", _name, error.what());
            succeed = false;
        }
        artifact_cache.EndCompile(key);
        LOG_INFO("ShaderVariant READY shader={} pass={} variant={} source={} reason={}", _name, pass_index, variant_hash,
                 used_artifact ? "artifact" : "compile", Render::ShaderArtifactInvalidReasonToString(reason));
        return succeed;
    }

    bool D3DShader::HydrateGraphicsVariant(u16 pass_index, ShaderVariantHash variant_hash,
                                           const Render::GraphicsShaderVariantArtifact &artifact)
    {
        if (artifact._root_signature_blob.empty() || artifact._vertex_shader._byte_code.empty() ||
            artifact._pixel_shader._byte_code.empty())
            return false;
        auto &pass = _passes[pass_index];
        auto &pass_variant = _passes[pass_index]._variants[variant_hash];
        auto &element = _pass_elements[pass_index]._variants[variant_hash];
        element = D3DShaderElement::D3DVariantElement();
        element._p_vblob = CreateBlobFromBytes(artifact._vertex_shader._byte_code);
        element._p_pblob = CreateBlobFromBytes(artifact._pixel_shader._byte_code);
        element._p_gblob = CreateBlobFromBytes(artifact._geometry_shader._byte_code);
        element._p_sig = D3DRootSignaturePool::Get().GetOrCreate(artifact._root_signature_hash,
                                                                  artifact._root_signature_blob);
        if (element._p_vblob == nullptr || element._p_pblob == nullptr || element._p_sig == nullptr)
            return false;
        pass._source_files.clear();
        for (const auto &dependency : artifact._dependencies)
            pass._source_files.insert(Render::NormalizeShaderDependencyPath(dependency._path));
        pass_variant._vertex_input_num = static_cast<u8>(artifact._vertex_input_layout.size());
        pass_variant._pipeline_input_layout = VertexBufferLayout(artifact._vertex_input_layout);
        for (u32 index = 0u; index < artifact._vertex_input_layout.size() && index < RenderConstants::kMaxVertexAttrNum; ++index)
        {
            const auto &layout = artifact._vertex_input_layout[index];
            element._vertex_input_layout[index] = D3D12_INPUT_ELEMENT_DESC{
                RenderConstants::GetVertexSemanticName(layout._semantic),
                RenderConstants::GetVertexSemanticIndex(layout._semantic),
                D3DConvertUtils::GetGXGIFormatByShaderDataType(layout.Type), index, layout.Offset,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
        }
        HydrateBindingMap(artifact._bindings, pass_variant._bind_res_infos);
        pass_variant._per_mat_buf_bind_slot = artifact._per_material_slot;
        pass_variant._per_pass_buf_bind_slot = artifact._per_camera_slot;
        pass_variant._per_frame_buf_bind_slot = artifact._per_scene_slot;
        element._root_parameters.clear();
        BuildBindingLayout(pass_index, variant_hash);
        return true;
    }

    void D3DShader::BuildGraphicsArtifact(u16 pass_index, ShaderVariantHash variant_hash,
                                          const Vector<D3D_SHADER_MACRO> &keyword_defines,
                                          const Render::ShaderArtifactKey &key,
                                          Render::GraphicsShaderVariantArtifact &artifact) const
    {
        (void) keyword_defines;
        const auto &pass = _passes[pass_index];
        const auto &pass_variant = pass._variants.at(variant_hash);
        const auto &element = _pass_elements[pass_index]._variants.at(variant_hash);
        artifact._header._type = Render::EShaderArtifactType::kGraphics;
        artifact._header._artifact_key = key.Hash();
        artifact._vertex_shader._byte_code.assign(static_cast<const u8 *>(element._p_vblob->GetBufferPointer()),
                                                  static_cast<const u8 *>(element._p_vblob->GetBufferPointer()) + element._p_vblob->GetBufferSize());
        artifact._pixel_shader._byte_code.assign(static_cast<const u8 *>(element._p_pblob->GetBufferPointer()),
                                                 static_cast<const u8 *>(element._p_pblob->GetBufferPointer()) + element._p_pblob->GetBufferSize());
        if (element._p_gblob != nullptr)
            artifact._geometry_shader._byte_code.assign(static_cast<const u8 *>(element._p_gblob->GetBufferPointer()),
                                                        static_cast<const u8 *>(element._p_gblob->GetBufferPointer()) + element._p_gblob->GetBufferSize());
        artifact._vertex_input_layout.assign(pass_variant._pipeline_input_layout.GetBufferDesc().begin(),
                                             pass_variant._pipeline_input_layout.GetBufferDesc().end());
        for (const auto &[name, info] : pass_variant._bind_res_infos)
            artifact._bindings.emplace_back(MakeBindingArtifact(name, info));
        artifact._per_material_slot = pass_variant._per_mat_buf_bind_slot;
        artifact._per_camera_slot = pass_variant._per_pass_buf_bind_slot;
        artifact._per_scene_slot = pass_variant._per_frame_buf_bind_slot;
        std::set<WString> dependency_files = pass._source_files;
        dependency_files.insert(_src_file_path);
        dependency_files.insert(pass._vert_src_file);
        dependency_files.insert(pass._pixel_src_file);
        if (!pass._geom_src_file.empty())
            dependency_files.insert(pass._geom_src_file);
        AppendArtifactDependencies(dependency_files, artifact._dependencies);
        MoveSourceDependencyFirst(_src_file_path, artifact._dependencies);
    }

    void D3DShader::LoadAdditionalShaderReflection(const WString &sys_path, u16 pass_index, ShaderVariantHash variant_hash)
    {
        using namespace std;
        namespace su = StringUtils;
        namespace fs = std::filesystem;
        ifstream src(sys_path, ios::in);
        string line;
        vector<string> lines;
        List<WString> cur_file_head_files{};
        WString parent_path = su::SubStrRange(_src_file_path, 0, _src_file_path.find_last_of(L"/"));
        bool is_in_cbuf_scope = false;//为了支持cbuffer中对于结构体数组的解析
        while (getline(src, line))
        {
            line = su::Trim(line);
            if (su::BeginWith(line, "//"))
            {
                lines.emplace_back(line);
                continue;
            }
            if (su::BeginWith(line, "CBUFFER_START"))
                is_in_cbuf_scope = true;
            if (su::BeginWith(line, "CBUFFER_END") && is_in_cbuf_scope)
                is_in_cbuf_scope = false;
            ParserBindResourceAddiInfo(_passes[pass_index]._variants[variant_hash]._bind_res_infos, line, is_in_cbuf_scope);
            lines.emplace_back(line);
        }
        src.close();
        fs::path src_path(sys_path);
        fs::path pwd = src_path.parent_path();
        for (auto &head_file: cur_file_head_files)
        {
            fs::path temp = pwd;
            temp.append(head_file);
            LoadAdditionalShaderReflection(temp.wstring(), pass_index, variant_hash);
        }
    }

    std::pair<D3D12_INPUT_ELEMENT_DESC *, u8> D3DShader::GetVertexInputLayout(u16 pass_index, ShaderVariantHash variant_hash)
    {
        return std::make_pair(_pass_elements[pass_index]._variants[variant_hash]._vertex_input_layout, _passes[pass_index]._variants[variant_hash]._vertex_input_num);
    }

    void D3DShader::Reset()
    {
        //_pass_elements.clear();
        _pass_elements.resize(_passes.size());
        //for (int i = 0; i < _passes.size(); i++)
        //{
        //	_pass_elements[i] = D3DShaderElement();
        //	//if (_pass_elements[i]._p_vblob != nullptr) _pass_elements[i]._p_vblob.Reset();
        //	//if (_pass_elements[i]._p_pblob != nullptr) _pass_elements[i]._p_pblob.Reset();
        //	//if (_pass_elements[i]._p_v_reflection != nullptr) _pass_elements[i]._p_v_reflection.Reset();
        //	//if (_pass_elements[i]._p_p_reflection != nullptr) _pass_elements[i]._p_p_reflection.Reset();
        //	//memset(_pass_elements[i]._vertex_input_layout, 0, sizeof(D3D12_INPUT_ELEMENT_DESC) * RenderConstants::kMaxVertexAttrNum);
        //	//_passes[i]._vertex_input_num = 0u;
        //	_passes[i]._pipeline_topology = ETopology::kTriangle;
        //}
    }
    void D3DShader::LoadShaderReflection(u16 pass_index, ShaderVariantHash variant_hash, ID3D12ShaderReflection *ref_vs, ID3D12ShaderReflection *ref_ps)
    {
        AL_ASSERT(pass_index < _passes.size());
        auto &pass = _passes[pass_index];
        AL_ASSERT(pass._variants.contains(variant_hash));
        auto &pass_variant = pass._variants[variant_hash];
        D3D12_SHADER_DESC desc{};
        //parser vs reflecton
        {
            ref_vs->GetDesc(&desc);
            if (desc.InputParameters > 10)
            {
                AL_ASSERT_MSG(false, "LayoutDesc count must less than 10");
                return;
            }
            Vector<VertexBufferLayoutDesc> vb_input_desc{};
            for (u32 i = 0u; i < desc.InputParameters; i++)
            {
                D3D12_SIGNATURE_PARAMETER_DESC input_desc{};
                ref_vs->GetInputParameterDesc(i, &input_desc);
                const EVertexSemantic semantic = Render::RenderConstants::GetVertexSemantic(input_desc.SemanticName,
                                                                                               static_cast<u8>(input_desc.SemanticIndex));
                EShaderDateType data_type = D3DConvertUtils::GetShaderDataType(semantic, input_desc.Mask);
                if (data_type != EShaderDateType::kNone)
                {
                    const u32 layout_index = static_cast<u32>(vb_input_desc.size());
                    _pass_elements[pass_index]._variants[variant_hash]._vertex_input_layout[layout_index] =
                        D3D12_INPUT_ELEMENT_DESC{Render::RenderConstants::GetVertexSemanticName(semantic),
                                                  Render::RenderConstants::GetVertexSemanticIndex(semantic),
                                                  D3DConvertUtils::GetGXGIFormatByShaderDataType(data_type), i, 0,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
                    vb_input_desc.emplace_back(semantic, data_type, input_desc.Register);
                }
                else if (semantic != EVertexSemantic::kVertexIndex && semantic != EVertexSemantic::kInstanceID)
                {
                    LOG_WARNING("LoadShaderReflection {} skip input element: {}", _name, input_desc.SemanticName);
                }
            }
            pass_variant._vertex_input_num = (u8) vb_input_desc.size();
            pass_variant._pipeline_input_layout = VertexBufferLayout(vb_input_desc);
            pass_variant._bind_res_infos.clear();
            ShaderReflectionUtils::AppendShaderResources(ref_vs, pass_variant._bind_res_infos);
        }
        //parser ps reflecton
        {
            ref_ps->GetDesc(&desc);
            ShaderReflectionUtils::AppendShaderResources(ref_ps, pass_variant._bind_res_infos);
        }
        //parser additon info
        LoadAdditionalShaderReflection(_src_file_path, pass_index, variant_hash);
        //for (auto &p: pass._source_files)
        //    LoadAdditionalShaderReflection(p, pass_index, variant_hash);
    }

    void D3DShader::Bind(RHICommandBuffer *cmd, u16 pass_index, ShaderVariantHash variant_hash)
    {
        Shader::Bind(cmd, pass_index, variant_hash);
    }

    void *D3DShader::GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash)
    {
        switch (type)
        {
            case EShaderType::kVertex:
                return reinterpret_cast<void *>(_pass_elements[pass_index]._variants[variant_hash]._p_vblob.Get());
            case EShaderType::kPixel:
                return reinterpret_cast<void *>(_pass_elements[pass_index]._variants[variant_hash]._p_pblob.Get());
            case EShaderType::kGeometry:
                return reinterpret_cast<void *>(_pass_elements[pass_index]._variants[variant_hash]._p_gblob.Get());
        }
        return nullptr;
    }

    ID3D12RootSignature *D3DShader::GetSignature(u16 pass_index, ShaderVariantHash variant_hash)
    {
        return _pass_elements[pass_index]._variants[variant_hash]._p_sig.Get();
    }

    Vector<CD3DX12_ROOT_PARAMETER1> &D3DShader::GetRootParameters(u16 pass_index, ShaderVariantHash variant_hash)
    {
        return _pass_elements[pass_index]._variants[variant_hash]._root_parameters;
    }

#pragma endregion

#pragma region D3DComputeShader
    //-------------------------------------------------------------------------------D3DComputeShader---------------------------------------------------------------------------
    D3DComputeShader::D3DComputeShader(const WString &sys_path) : ComputeShader(sys_path)
    {
        Compile();
    }

    void D3DComputeShader::Bind(RHICommandBuffer *cmd, Render::ComputeShaderKernelId kernel)
    {
        if (!_is_valid || !IsKernelValid(kernel))
        {
            LOG_WARNING("ComputeShader or kernel id is not valid!");
            return;
        }
        const auto kernel_index = ResolveKernelIndex(kernel);
        ComputeShader::Bind(cmd, kernel);
        AL_ASSERT(!_bind_state.empty());
        std::unique_lock lock(_state_mutex);
        auto &cur_state = _bind_state.front();
        if (_variant_state[kernel_index][cur_state._variant_hash] != EShaderVariantState::kReady)
            return;
        auto command_buffer = static_cast<D3DCommandBuffer *>(cmd);
        auto d3dcmd = command_buffer->NativeCmdList();
        auto &d3d_ele = _elements[kernel_index]._variants[cur_state._variant_hash];
        auto &cs_ele = _kernels[kernel_index]._variants[cur_state._variant_hash];
        command_buffer->SetComputePipelineState(d3d_ele._pso.Get());
        d3dcmd->SetComputeRootSignature(d3d_ele._p_sig.Get());
        for (u16 i = 0; i <= cur_state._max_bind_slot; i++)
        {
            auto it = std::find_if(cs_ele._bind_res_infos.begin(), cs_ele._bind_res_infos.end(), [&](const auto &it) -> bool
                                   { return it.second._bind_slot == i; });
            if (it != cs_ele._bind_res_infos.end())
            {
                auto &bind_info = it->second;
                auto &view_info = cur_state._bind_params[bind_info._bind_slot];
                GpuResource *bind_res = cur_state._bind_res[bind_info._bind_slot];
                if (bind_res == nullptr)
                    continue;
                if (bind_info._res_type == EBindResDescType::kTexture2D ||
                    bind_info._res_type == EBindResDescType::kCubeMap)
                {
                    auto tex = static_cast<Texture *>(bind_res);
                    u16 view_index = view_info._view_index;
                    if (view_index == static_cast<u16>(-1))
                    {
                        view_index = bind_info._res_type == EBindResDescType::kCubeMap &&
                                     view_info._face == static_cast<u16>(ECubemapFace::kUnknown)
                                         ? Texture::kMainSRVIndex
                                         : tex->CalculateViewIndex(Texture::ETextureViewType::kSRV,
                                                                    static_cast<ECubemapFace>(view_info._face),
                                                                    view_info._mipmap, 0u);
                    }
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = view_index;
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kUAVTexture2D)
                {
                    auto tex = static_cast<Texture *>(bind_res);
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = tex->CalculateViewIndex(Texture::ETextureViewType::kUAV, static_cast<ECubemapFace>(view_info._face), view_info._mipmap, 0);
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kTexture3D)
                {
                    auto tex = static_cast<Texture3D *>(bind_res);
                    u16 view_index = view_info._view_index == (u16) -1 ? tex->CalculateViewIndex(Texture::ETextureViewType::kSRV, static_cast<ECubemapFace>(view_info._face), view_info._mipmap, view_info._slice) : view_info._view_index;
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = view_index;
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kRWTexture3D)
                {
                    auto tex = static_cast<Texture3D *>(bind_res);
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = tex->CalculateViewIndex(Texture::ETextureViewType::kUAV, static_cast<ECubemapFace>(view_info._face), view_info._mipmap, view_info._slice);
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kRWBuffer)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    params._is_random_access = true;
                    bind_res->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kBuffer)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    bind_res->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kConstBuffer)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    bind_res->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kAccelerationStructure)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    bind_res->Bind(cmd, params);
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
        }
        const auto bindless_srv_base = D3DDescriptorMgr::Get().GetBindlessSRVBaseGpuHandle();
        const auto bindless_uav_base = D3DDescriptorMgr::Get().GetBindlessUAVBaseGpuHandle();
        if (d3d_ele._has_bindless_texture2d)
            d3dcmd->SetComputeRootDescriptorTable(d3d_ele._bindless_texture_slot, bindless_srv_base);
        if (d3d_ele._has_bindless_buffer)
            d3dcmd->SetComputeRootDescriptorTable(d3d_ele._bindless_buffer_slot, bindless_srv_base);
        if (d3d_ele._has_bindless_rw_texture2d)
            d3dcmd->SetComputeRootDescriptorTable(d3d_ele._bindless_rw_texture_slot, bindless_uav_base);
        if (d3d_ele._has_bindless_rw_buffer)
            d3dcmd->SetComputeRootDescriptorTable(d3d_ele._bindless_rw_buffer_slot, bindless_uav_base);
        _bind_state.pop();
        //d3dcmd->Dispatch(thread_group_x, thread_group_y, thread_group_z);
    }

    void D3DComputeShader::Bind(RHICommandBuffer *cmd, Render::ComputeShaderKernelId kernel,
                                const Render::ComputeDispatchSnapshot &snapshot)
    {
        if (!_is_valid || !IsKernelValid(kernel) || !snapshot._is_ready)
            return;
        const auto kernel_index = ResolveKernelIndex(kernel);
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd);
        auto native_cmd = d3dcmd->NativeCmdList();
        auto &d3d_ele = _elements[kernel_index]._variants[snapshot._variant_hash];
        auto &cs_ele = _kernels[kernel_index]._variants[snapshot._variant_hash];
        d3dcmd->SetComputePipelineState(d3d_ele._pso.Get());
        native_cmd->SetComputeRootSignature(d3d_ele._p_sig.Get());
        for (u16 i = 0u; i < snapshot._entry_count; ++i)
        {
            const auto &entry = snapshot._entries[i];
            auto *resource = GpuResourceRegistry::Get().Resolve(entry._resource);
            if (resource == nullptr && entry._resource_type != EBindResDescType::kConstBufferRaw)
                continue;
            BindParams params;
            params._is_compute_pipeline = true;
            params._slot = entry._slot;
            params._is_random_access = entry._resource_type == EBindResDescType::kRWBuffer;
            if (entry._resource_type == EBindResDescType::kConstBufferRaw)
            {
                native_cmd->SetComputeRootConstantBufferView(entry._slot, entry._addi_info._gpu_handle);
            }
            else if (entry._resource_type == EBindResDescType::kTexture2D ||
                     entry._resource_type == EBindResDescType::kCubeMap)
            {
                auto tex = static_cast<Texture *>(resource);
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
            }
            else if (entry._resource_type == EBindResDescType::kUAVTexture2D)
            {
                auto tex = static_cast<Texture *>(resource);
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
            }
            else if (entry._resource_type == EBindResDescType::kTexture3D)
            {
                auto tex = static_cast<Texture3D *>(resource);
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
            }
            else if (entry._resource_type == EBindResDescType::kRWTexture3D)
            {
                auto tex = static_cast<Texture3D *>(resource);
                params._params._texture_binder._sub_res = entry._sub_res;
                params._params._texture_binder._view_idx = entry._view_index;
                tex->Bind(cmd, params);
            }
            else if (entry._resource_type == EBindResDescType::kRWBuffer || entry._resource_type == EBindResDescType::kBuffer
                     || entry._resource_type == EBindResDescType::kConstBuffer ||
                     entry._resource_type == EBindResDescType::kAccelerationStructure)
            {
                resource->Bind(cmd, params);
            }
            else
            {
                LOG_WARNING("D3DComputeShader skipped unsupported snapshot binding type {} at slot {}",
                            static_cast<u32>(entry._resource_type), entry._slot);
            }
        }
        const auto bindless_srv_base = D3DDescriptorMgr::Get().GetBindlessSRVBaseGpuHandle();
        const auto bindless_uav_base = D3DDescriptorMgr::Get().GetBindlessUAVBaseGpuHandle();
        if (d3d_ele._has_bindless_texture2d)
            native_cmd->SetComputeRootDescriptorTable(d3d_ele._bindless_texture_slot, bindless_srv_base);
        if (d3d_ele._has_bindless_buffer)
            native_cmd->SetComputeRootDescriptorTable(d3d_ele._bindless_buffer_slot, bindless_srv_base);
        if (d3d_ele._has_bindless_rw_texture2d)
            native_cmd->SetComputeRootDescriptorTable(d3d_ele._bindless_rw_texture_slot, bindless_uav_base);
        if (d3d_ele._has_bindless_rw_buffer)
            native_cmd->SetComputeRootDescriptorTable(d3d_ele._bindless_rw_buffer_slot, bindless_uav_base);
    }


    void D3DComputeShader::LoadReflectionInfo(ID3D12ShaderReflection *p_reflect, u16 kernel_index, ShaderVariantHash variant_hash)
    {
        auto &cs_ele = _kernels[kernel_index]._variants[variant_hash];
        cs_ele._temp_bind_res_infos.clear();
        p_reflect->GetThreadGroupSize(&_kernels[kernel_index]._thread_num.x, &_kernels[kernel_index]._thread_num.y, &_kernels[kernel_index]._thread_num.z);
        ShaderReflectionUtils::AppendShaderResources(p_reflect, cs_ele._temp_bind_res_infos);
    }
    //https://rtarun9.github.io/blogs/shader_reflection/#reflecting-input-parameters
    void D3DComputeShader::LoadAdditionalShaderReflection(const WString &sys_path, u16 kernel_index, ShaderVariantHash variant_hash)
    {
        using namespace std;
        namespace su = StringUtils;
        namespace fs = std::filesystem;
        ifstream src(sys_path, ios::in);
        string line;
        vector<string> lines;
        List<WString> cur_file_head_files{};
        WString parent_path = su::SubStrRange(_src_file_path, 0, _src_file_path.find_last_of(L"/"));
        auto &cur_kernel = _kernels[kernel_index]._variants[variant_hash];
        bool is_in_cbuf_scope = false;
        while (getline(src, line))
        {
            line = su::Trim(line);
            if (su::BeginWith(line, "//"))
            {
                lines.emplace_back(line);
                continue;
            }
            if (su::BeginWith(line, "CBUFFER_START"))
                is_in_cbuf_scope = true;
            if (su::BeginWith(line, "CBUFFER_END") && is_in_cbuf_scope)
                is_in_cbuf_scope = false;
            ParserBindResourceAddiInfo(_kernels[kernel_index]._variants[variant_hash]._temp_bind_res_infos, line, is_in_cbuf_scope);
            lines.emplace_back(line);
        }
        src.close();
        fs::path src_path(sys_path);
        fs::path pwd = src_path.parent_path();
        for (auto &head_file: cur_file_head_files)
        {
            fs::path temp = pwd;
            temp.append(head_file);
            LoadAdditionalShaderReflection(temp.wstring(), kernel_index, variant_hash);
        }
    }

    bool D3DComputeShader::RHICompileImpl(u16 kernel_index, ShaderVariantHash variant_hash,
                                           Render::EShaderCompilePolicy policy)
    {
        const bool is_load_cache = policy == Render::EShaderCompilePolicy::kUseArtifact;
        if (kernel_index >= _kernels.size())
            return false;
        {
            std::lock_guard<std::mutex> lock(_ele_lock);
            if (kernel_index >= _elements.size())
                _elements.resize(kernel_index + 1);
        }
        auto &cur_variant = _kernels[kernel_index]._variants[variant_hash];
        const bool is_inline_ray_tracing = cur_variant._active_keywords.contains("AL_RAY_QUERY_DXR_INLINE");
        if (is_inline_ray_tracing && !GraphicsContext::Get().IsInlineRayTracingSupported())
        {
            LOG_INFO("Skip unsupported inline DXR compute variant shader={} kernel={} variant={}",
                     ToChar(_src_file_path), kernel_index, variant_hash);
            return true;
        }
        const auto macros = ConstructVariantMarcos(cur_variant._active_keywords);
        const String shader_model = is_inline_ray_tracing ? RenderConstants::kCSModel_6_5
                                                          : RenderConstants::kCSModel_6_1;
        const String artifact_shader_model = is_inline_ray_tracing ? "cs6.5" : "cs6.1";
        const auto key = BuildShaderArtifactKey(Render::EShaderArtifactType::kCompute, _src_file_path,
                                                _kernels[kernel_index]._name, kernel_index, variant_hash,
                                                artifact_shader_model, macros);
        auto &artifact_cache = Render::ShaderArtifactCache::Get();
        Render::ComputeShaderVariantArtifact artifact;
        Render::EShaderArtifactInvalidReason reason = Render::EShaderArtifactInvalidReason::kNone;
        if (is_load_cache && artifact_cache.Load(key, artifact, &reason) && HydrateComputeVariant(kernel_index, variant_hash, artifact))
        {
            LOG_INFO("ComputeShaderVariant READY shader={} kernel={} variant={} source=artifact reason={}",
                     ToChar(_src_file_path), kernel_index, variant_hash,
                     Render::ShaderArtifactInvalidReasonToString(reason));
            return true;
        }
        if (is_load_cache && artifact_cache.WaitForCompile(key) &&
            artifact_cache.Load(key, artifact, &reason) && HydrateComputeVariant(kernel_index, variant_hash, artifact))
        {
            LOG_INFO("ComputeShaderVariant READY shader={} kernel={} variant={} source=artifact reason={}",
                     ToChar(_src_file_path), kernel_index, variant_hash,
                     Render::ShaderArtifactInvalidReasonToString(reason));
            return true;
        }

        artifact_cache.BeginCompile(key);
        bool succeed = false;
        bool used_artifact = false;
        try
        {
            if (is_load_cache && artifact_cache.Load(key, artifact, &reason) && HydrateComputeVariant(kernel_index, variant_hash, artifact))
            {
                succeed = true;
                used_artifact = true;
            }
            else
            {
                ComPtr<ID3DBlob> tmp_blob;
                ComPtr<ID3D12ShaderReflection> tmp_reflection;
                std::set<WString> dependency_files = cur_variant._all_dep_file_pathes;
                bool compile_succeed = false;
#ifdef SHADER_DXC
                compile_succeed = CreateFromFileDXC(_src_file_path, _kernels[kernel_index]._name,
                                                    shader_model, macros, tmp_blob, tmp_reflection,
                                                    dependency_files, false);
#else
                if (!is_inline_ray_tracing)
                    compile_succeed = CreateFromFileFXC(_src_file_path, _kernels[kernel_index]._name,
                                                        RenderConstants::kCSModel_5_0, macros, tmp_blob, tmp_reflection,
                                                        dependency_files, false);
#endif
                if (compile_succeed && tmp_reflection != nullptr)
                {
                    cur_variant._all_dep_file_pathes = Render::NormalizeShaderDependencyPaths(dependency_files);
                    _elements[kernel_index]._variants[variant_hash] = D3DVariantElement();
                    _elements[kernel_index]._variants[variant_hash]._p_blob = tmp_blob;
                    LoadReflectionInfo(tmp_reflection.Get(), kernel_index, variant_hash);
                    LoadAdditionalShaderReflection(_src_file_path, kernel_index, variant_hash);
                    Render::ComputeShaderVariantArtifact new_artifact;
                    GenerateInternalPSO(kernel_index, variant_hash, &new_artifact._root_signature_blob);
                    cur_variant._binding_layout = Render::BuildShaderBindingLayout(
                        cur_variant._bind_res_infos, static_cast<u32>(variant_hash));
                    BuildComputeArtifact(kernel_index, variant_hash, key, new_artifact);
                    artifact_cache.Store(key, new_artifact);
                    ++artifact_cache.Stats()._compiled_variant_count;
                    succeed = _is_valid;
                }
            }
        }
        catch (const std::exception &error)
        {
            LOG_ERROR("Compile compute shader {} failed: {}", ToChar(_src_file_path), error.what());
        }
        artifact_cache.EndCompile(key);
        LOG_INFO("ComputeShaderVariant READY shader={} kernel={} variant={} source={} reason={}",
                 ToChar(_src_file_path), kernel_index, variant_hash, used_artifact ? "artifact" : "compile",
                 Render::ShaderArtifactInvalidReasonToString(reason));
        return succeed;
    }

    bool D3DComputeShader::HydrateComputeVariant(u16 kernel_index, ShaderVariantHash variant_hash,
                                                const Render::ComputeShaderVariantArtifact &artifact)
    {
        if (artifact._compute_shader._byte_code.empty() || artifact._root_signature_blob.empty())
            return false;
        auto &kernel_variant = _kernels[kernel_index]._variants[variant_hash];
        auto &d3d_variant = _elements[kernel_index]._variants[variant_hash];
        d3d_variant = D3DVariantElement();
        d3d_variant._p_blob = CreateBlobFromBytes(artifact._compute_shader._byte_code);
        d3d_variant._p_sig = D3DRootSignaturePool::Get().GetOrCreate(artifact._root_signature_hash,
                                                                      artifact._root_signature_blob);
        if (d3d_variant._p_blob == nullptr || d3d_variant._p_sig == nullptr)
            return false;
        _kernels[kernel_index]._thread_num = artifact._thread_group_size;
        HydrateBindingMap(artifact._bindings, kernel_variant._bind_res_infos);
        kernel_variant._binding_layout = Render::BuildShaderBindingLayout(
            kernel_variant._bind_res_infos, static_cast<u32>(variant_hash));
        kernel_variant._temp_bind_res_infos.clear();
        kernel_variant._all_dep_file_pathes.clear();
        for (const auto &dependency : artifact._dependencies)
            kernel_variant._all_dep_file_pathes.insert(Render::NormalizeShaderDependencyPath(dependency._path));
        for (const auto &binding : artifact._bindings)
        {
            if (binding._name == "g_bindless_texture2d")
            {
                d3d_variant._has_bindless_texture2d = true;
                d3d_variant._bindless_texture_slot = binding._bind_slot;
            }
            else if (binding._name == "g_bindless_buffer")
            {
                d3d_variant._has_bindless_buffer = true;
                d3d_variant._bindless_buffer_slot = binding._bind_slot;
            }
            else if (binding._name == "g_bindless_rw_texture2d")
            {
                d3d_variant._has_bindless_rw_texture2d = true;
                d3d_variant._bindless_rw_texture_slot = binding._bind_slot;
            }
            else if (binding._name == "g_bindless_rw_buffer")
            {
                d3d_variant._has_bindless_rw_buffer = true;
                d3d_variant._bindless_rw_buffer_slot = binding._bind_slot;
            }
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = d3d_variant._p_sig.Get();
        pso_desc.CS = {d3d_variant._p_blob->GetBufferPointer(), d3d_variant._p_blob->GetBufferSize()};
        const auto device = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice();
        if (device == nullptr || FAILED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&d3d_variant._pso))))
            return false;
        _is_valid = true;
        return true;
    }

    void D3DComputeShader::BuildComputeArtifact(u16 kernel_index, ShaderVariantHash variant_hash,
                                                const Render::ShaderArtifactKey &key,
                                                Render::ComputeShaderVariantArtifact &artifact) const
    {
        const auto &kernel_variant = _kernels[kernel_index]._variants.at(variant_hash);
        const auto &element = _elements[kernel_index]._variants.at(variant_hash);
        artifact._header._type = Render::EShaderArtifactType::kCompute;
        artifact._header._artifact_key = key.Hash();
        artifact._compute_shader._byte_code.assign(static_cast<const u8 *>(element._p_blob->GetBufferPointer()),
                                                   static_cast<const u8 *>(element._p_blob->GetBufferPointer()) + element._p_blob->GetBufferSize());
        artifact._thread_group_size = _kernels[kernel_index]._thread_num;
        for (const auto &[name, info] : kernel_variant._bind_res_infos)
            artifact._bindings.emplace_back(MakeBindingArtifact(name, info));
        std::set<WString> dependency_files = kernel_variant._all_dep_file_pathes;
        dependency_files.insert(_src_file_path);
        AppendArtifactDependencies(dependency_files, artifact._dependencies);
        MoveSourceDependencyFirst(_src_file_path, artifact._dependencies);
        artifact._root_signature_hash = Render::HashArtifactBytes(artifact._root_signature_blob);
    }

    void D3DComputeShader::GenerateInternalPSO(u16 kernel_index, ShaderVariantHash variant_hash,
                                                Vector<u8> *serialized_signature)
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        auto device = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice();
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
        CD3DX12_ROOT_PARAMETER1 rootParameters[32]{};
        int cbuf_mask = 0, texture_count = 0;
        u8 root_param_index = 0;
        auto &cs_ele = _kernels[kernel_index]._variants[variant_hash];
        auto &d3d_ele = _elements[kernel_index]._variants[variant_hash];
        auto append_bindless_table = [&](const char* resource_name, D3D12_DESCRIPTOR_RANGE_TYPE range_type, u32 capacity, bool& has_bindless, u16& bindless_slot)
        {
            auto bindless_it = std::find_if(cs_ele._temp_bind_res_infos.begin(), cs_ele._temp_bind_res_infos.end(), [resource_name](auto it) -> bool
            {
                return it.second._name == resource_name;
            });
            if (bindless_it == cs_ele._temp_bind_res_infos.end())
                return;

            rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
            ranges[root_param_index].Init(range_type,
                                          capacity,
                                          bindless_it->second._res_slot,
                                          bindless_it->second._register_space,
                                          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
            bindless_it->second._bind_slot = root_param_index;
            has_bindless = true;
            bindless_slot = root_param_index;
            ++root_param_index;
        };
        d3d_ele._has_bindless_texture2d = false;
        d3d_ele._has_bindless_buffer = false;
        d3d_ele._has_bindless_rw_texture2d = false;
        d3d_ele._has_bindless_rw_buffer = false;
        d3d_ele._bindless_texture_slot = static_cast<u16>(-1);
        d3d_ele._bindless_buffer_slot = static_cast<u16>(-1);
        d3d_ele._bindless_rw_texture_slot = static_cast<u16>(-1);
        d3d_ele._bindless_rw_buffer_slot = static_cast<u16>(-1);
        append_bindless_table("g_bindless_texture2d", D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GPUVisibleDescriptorAllocator::kBindlessSRVCapacity, d3d_ele._has_bindless_texture2d, d3d_ele._bindless_texture_slot);
        append_bindless_table("g_bindless_buffer", D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GPUVisibleDescriptorAllocator::kBindlessSRVCapacity, d3d_ele._has_bindless_buffer, d3d_ele._bindless_buffer_slot);
        append_bindless_table("g_bindless_rw_texture2d", D3D12_DESCRIPTOR_RANGE_TYPE_UAV, GPUVisibleDescriptorAllocator::kBindlessUAVCapacity, d3d_ele._has_bindless_rw_texture2d, d3d_ele._bindless_rw_texture_slot);
        append_bindless_table("g_bindless_rw_buffer", D3D12_DESCRIPTOR_RANGE_TYPE_UAV, GPUVisibleDescriptorAllocator::kBindlessUAVCapacity, d3d_ele._has_bindless_rw_buffer, d3d_ele._bindless_rw_buffer_slot);
        for (auto it = cs_ele._temp_bind_res_infos.begin(); it != cs_ele._temp_bind_res_infos.end(); it++)
        {
            auto &desc = it->second;
            if (desc._name == "g_bindless_texture2d" || desc._name == "g_bindless_buffer" || desc._name == "g_bindless_rw_texture2d" || desc._name == "g_bindless_rw_buffer")
                continue;
            if (desc._res_type == EBindResDescType::kTexture2D ||
                desc._res_type == EBindResDescType::kCubeMap ||
                desc._res_type == EBindResDescType::kTexture3D)
            {
                ++texture_count;
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kBuffer ||
                     desc._res_type == EBindResDescType::kAccelerationStructure)
            {
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kUAVTexture2D || desc._res_type == EBindResDescType::kRWTexture3D)
            {
                ++texture_count;
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kRWBuffer)
            {
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kConstBuffer)
            {
                rootParameters[root_param_index].InitAsConstantBufferView(desc._res_slot, desc._register_space);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
        }
        auto &sig = d3d_ele._p_sig;
        auto &pso = d3d_ele._pso;

        auto &samplers = CreateStaticSampler();
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(root_param_index, rootParameters, static_cast<u32>(samplers.size()), samplers.data(), rootSignatureFlags);
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        if (SUCCEEDED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error)))
        {
            Vector<u8> signature_bytes(signature->GetBufferSize());
            std::memcpy(signature_bytes.data(), signature->GetBufferPointer(), signature_bytes.size());
            if (serialized_signature != nullptr)
                *serialized_signature = signature_bytes;
            const u64 signature_hash = Render::HashArtifactBytes(signature_bytes);
            sig = D3DRootSignaturePool::Get().GetOrCreate(signature_hash, signature_bytes);
            if (sig == nullptr)
            {
                _is_valid = false;
                return;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
            pso_desc.pRootSignature = sig.Get();
            pso_desc.CS = {d3d_ele._p_blob->GetBufferPointer(), d3d_ele._p_blob->GetBufferSize()};
            pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
            if (SUCCEEDED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
            {
                std::unordered_map<String, ShaderBindResourceInfo> cbuffer_bind_info;
                u32 cbuffer_size = 0;
                u16 cbuffer_count = 0;
                auto cb_iter = std::find_if(cs_ele._temp_bind_res_infos.begin(), cs_ele._temp_bind_res_infos.end(), [this](auto it) -> bool
                                            {
					auto& desc = it.second;
					return desc._res_type == EBindResDescType::kConstBuffer && desc._name != RenderConstants::kCBufNamePerScene & desc._name != RenderConstants::kCBufNamePerCamera; });
                if (cb_iter != cs_ele._temp_bind_res_infos.end())
                {
                    _internal_cbuf_name = cb_iter->second._name;
                }
                for (auto it = cs_ele._temp_bind_res_infos.begin(); it != cs_ele._temp_bind_res_infos.end(); it++)
                {
                    //临时方案，kCBufNameSceneState外部创建
                    if (it->second._res_type & EBindResDescType::kCBufferAttribute && it->second._p_root_cbuf->_name != RenderConstants::kCBufNamePerScene && it->second._p_root_cbuf->_name != RenderConstants::kCBufNamePerCamera)
                    {
                        cbuffer_bind_info.insert(std::make_pair(it->first, it->second));
                        if (it->second._res_type & EBindResDescType::kCBufferAttribute)
                            cbuffer_size += ShaderBindResourceInfo::GetVariableSize(it->second);
                    }
                }
                AL_ASSERT_MSG(cbuffer_size <= ComputeShader::kCBufferSize, "ComputeBuffer size must be less than 1024");
                cbuffer_size = AlignTo(cbuffer_size,256);
                //这里暂时只支持一个cbuffer，以后按需修改
                auto cbuf_it = std::find_if(cs_ele._temp_bind_res_infos.begin(), cs_ele._temp_bind_res_infos.end(), [this](auto it)
                                            {
					auto& desc = it.second;
					return desc._res_type == EBindResDescType::kConstBuffer; });
                if (cbuf_it != cs_ele._temp_bind_res_infos.end())
                {
                    for (auto it = cbuffer_bind_info.begin(); it != cbuffer_bind_info.end(); it++)
                    {
                        auto old_variable_it = cs_ele._bind_res_infos.find(it->first);
                        if (old_variable_it != cs_ele._bind_res_infos.end())
                        {
                            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), _cache_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(old_variable_it->second),
                                   ShaderBindResourceInfo::GetVariableSize(it->second));
                        }
                    }
                }
                cs_ele._bind_res_infos = std::move(cs_ele._temp_bind_res_infos);
                _is_valid = true;
            }
            else
                _is_valid = false;
        }
        else
        {
            _is_valid = false;
            LOG_ERROR(L"Create compute shader {} failed when generate internal pso!", _src_file_path);
        }
    }
#pragma endregion
    //-------------------------------------------------------------------------------D3DComputeShader---------------------------------------------------------------------------
}// namespace Ailu::RHI::DX12
