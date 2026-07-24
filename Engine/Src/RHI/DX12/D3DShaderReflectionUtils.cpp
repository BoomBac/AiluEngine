#include "pch.h"
#include "Framework/Common/Log.h"
#include "RHI/DX12/D3DShaderReflectionUtils.h"
#include "RHI/DX12/DescriptorManager.h"

namespace Ailu::RHI::DX12::ShaderReflectionUtils
{
    namespace
    {
        constexpr u32 kMaxRootParameterCount = 32u;

        template<typename TGetBindDescFn>
        void AppendBoundResources(UINT bound_resource_count,
                                  TGetBindDescFn &&get_bind_desc,
                                  ShaderBindResourceMap &bind_res_infos)
        {
            for (UINT index = 0; index < bound_resource_count; ++index)
            {
                D3D12_SHADER_INPUT_BIND_DESC bind_desc{};
                get_bind_desc(index, &bind_desc);

                if (bind_desc.BindPoint == UINT_MAX || bind_desc.Space == UINT_MAX)
                {
                    LOG_WARNING("Skipping shader resource reflection entry '{}' with invalid register binding t/u/b/s{} space{}.", bind_desc.Name ? bind_desc.Name : "<unnamed>", bind_desc.BindPoint, bind_desc.Space);
                    continue;
                }

                bind_res_infos.insert(ParseBindResource(bind_desc));
            }
        }

        template<typename TGetConstantBufferFn>
        void AppendConstantBuffers(UINT constant_buffer_count,
                                   TGetConstantBufferFn &&get_constant_buffer,
                                   ShaderBindResourceMap &bind_res_infos,
                                   bool apply_cbuffer_bind_flags)
        {
            for (UINT cbuffer_index = 0; cbuffer_index < constant_buffer_count; ++cbuffer_index)
            {
                auto *cbuf = get_constant_buffer(cbuffer_index);
                if (cbuf == nullptr)
                    continue;

                D3D12_SHADER_BUFFER_DESC cbuf_desc{};
                cbuf->GetDesc(&cbuf_desc);
                if (cbuf_desc.Type != D3D_CT_CBUFFER)
                {
                    // 忽略非 cbuffer（比如 RESOURCE_BIND_INFO）
                    continue;
                }
                auto root_cbuf_it = bind_res_infos.find(cbuf_desc.Name);
                if (root_cbuf_it == bind_res_infos.end())
                    continue;

                if (apply_cbuffer_bind_flags)
                    root_cbuf_it->second._bind_flag = Render::ShaderBindResourceInfo::GetBindResourceFlag(cbuf_desc.Name);

                for (UINT variable_index = 0; variable_index < cbuf_desc.Variables; ++variable_index)
                {
                    auto *variable = cbuf->GetVariableByIndex(variable_index);
                    D3D12_SHADER_VARIABLE_DESC variable_desc{};
                    variable->GetDesc(&variable_desc);

                    auto info = ParseBindVariable(variable_desc);
                    info._bind_flag = root_cbuf_it->second._bind_flag;
                    info._p_root_cbuf = &root_cbuf_it->second;
                    info._p_root_cbuf->_cbuf_size += variable_desc.Size;
                    bind_res_infos.insert(std::make_pair(variable_desc.Name, info));
                }
            }
        }
    }

    std::pair<String, Render::ShaderBindResourceInfo> ParseBindResource(const D3D12_SHADER_INPUT_BIND_DESC &bind_desc)
    {
        std::pair<String, Render::ShaderBindResourceInfo> ret;
        const auto res_type = bind_desc.Type;
        if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER)
        {
            ret = std::make_pair(bind_desc.Name, Render::ShaderBindResourceInfo{Render::EBindResDescType::kConstBuffer, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE)
        {
            auto info = Render::ShaderBindResourceInfo{Render::EBindResDescType::kTexture2D, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name};
            info._register_space = static_cast<u16>(bind_desc.Space);
            ret = std::make_pair(bind_desc.Name, info);
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER)
        {
            ret = std::make_pair(bind_desc.Name, Render::ShaderBindResourceInfo{Render::EBindResDescType::kSampler, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_STRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_BYTEADDRESS)
        {
            auto info = Render::ShaderBindResourceInfo{Render::EBindResDescType::kBuffer, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name};
            info._register_space = static_cast<u16>(bind_desc.Space);
            ret = std::make_pair(bind_desc.Name, info);
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWSTRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_APPEND_STRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_CONSUME_STRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWBYTEADDRESS)
        {
            auto info = Render::ShaderBindResourceInfo{Render::EBindResDescType::kRWBuffer, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name};
            info._register_space = static_cast<u16>(bind_desc.Space);
            ret = std::make_pair(bind_desc.Name, info);
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWTYPED)
        {
            ret = std::make_pair(bind_desc.Name, Render::ShaderBindResourceInfo{Render::EBindResDescType::kUAVTexture2D, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_RTACCELERATIONSTRUCTURE)
        {
            ret = std::make_pair(bind_desc.Name, Render::ShaderBindResourceInfo{Render::EBindResDescType::kAccelerationStructure, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else
        {
            AL_ASSERT(false);
        }
        ret.second._register_space = bind_desc.Space;
        return ret;
    }

    Render::ShaderBindResourceInfo ParseBindVariable(const D3D12_SHADER_VARIABLE_DESC &desc)
    {
        const auto offset = static_cast<u16>(desc.StartOffset);
        const auto size = static_cast<u16>(desc.Size);
        u32 variable_info = 0u;
        variable_info |= offset;
        variable_info <<= 16;
        variable_info |= size;

        auto value_type = Render::EBindResDescType::kCBufferAttribute;
        if (size == 4)
            value_type = static_cast<Render::EBindResDescType>(Render::EBindResDescType::kCBufferFloat | value_type);
        else if (size == 16 || size == 12 || size == 8)
            value_type = static_cast<Render::EBindResDescType>(Render::EBindResDescType::kCBufferFloats | value_type);
        else if (size == 64)
            value_type = static_cast<Render::EBindResDescType>(Render::EBindResDescType::kCBufferMatrix | value_type);

        return Render::ShaderBindResourceInfo{value_type, variable_info, 255u, desc.Name};
    }

    void AppendShaderResources(ID3D12ShaderReflection *reflection,
                               ShaderBindResourceMap &bind_res_infos,
                               bool apply_cbuffer_bind_flags)
    {
        if (reflection == nullptr)
            return;

        D3D12_SHADER_DESC desc{};
        reflection->GetDesc(&desc);
        AppendBoundResources(desc.BoundResources,
                             [reflection](UINT index, D3D12_SHADER_INPUT_BIND_DESC *bind_desc)
                             {
                                 reflection->GetResourceBindingDesc(index, bind_desc);
                             },
                             bind_res_infos);
        AppendConstantBuffers(desc.ConstantBuffers,
                              [reflection](UINT index)
                              {
                                  return reflection->GetConstantBufferByIndex(index);
                              },
                              bind_res_infos,
                              apply_cbuffer_bind_flags);
    }

    void AppendFunctionResources(ID3D12FunctionReflection *reflection,
                                 ShaderBindResourceMap &bind_res_infos,
                                 bool apply_cbuffer_bind_flags)
    {
        if (reflection == nullptr)
            return;

        D3D12_FUNCTION_DESC desc{};
        reflection->GetDesc(&desc);
        AppendBoundResources(desc.BoundResources,
                             [reflection](UINT index, D3D12_SHADER_INPUT_BIND_DESC *bind_desc)
                             {
                                 reflection->GetResourceBindingDesc(index, bind_desc);
                             },
                             bind_res_infos);
        for (auto& res : bind_res_infos)
        {
            if (res.second._register_space == 1)
                res.second._bind_flag |= Render::ShaderBindResourceInfo::kBindFlagLocal;
        }
        AppendConstantBuffers(desc.ConstantBuffers,
                              [reflection](UINT index)
                              {
                                  return reflection->GetConstantBufferByIndex(index);
                              },
                              bind_res_infos,
                              apply_cbuffer_bind_flags);
    }

    void AppendLibraryResources(ID3D12LibraryReflection *reflection,
                                ShaderBindResourceMap &bind_res_infos,
                                bool apply_cbuffer_bind_flags)
    {
        if (reflection == nullptr)
            return;

        D3D12_LIBRARY_DESC desc{};
        reflection->GetDesc(&desc);
        for (UINT function_index = 0; function_index < desc.FunctionCount; ++function_index)
            AppendFunctionResources(reflection->GetFunctionByIndex(function_index), bind_res_infos, apply_cbuffer_bind_flags);
    }

    void GenerateRootSignature(ID3D12Device *device, ShaderBindResourceMap& bind_res_infos, ComPtr<ID3D12RootSignature> *root_signature, bool is_local_root_signature,Vector<CD3DX12_STATIC_SAMPLER_DESC> static_samplers)
    {
            auto create_root_signature = [](ID3D12Device *device, bool is_local, u32 root_parameter_count, const CD3DX12_ROOT_PARAMETER1 *root_parameters, ComPtr<ID3D12RootSignature> *rootSig,
            u32 static_sampler_count = 0, const CD3DX12_STATIC_SAMPLER_DESC* static_samplers = nullptr){
            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            rootSignatureDesc.Init_1_1(root_parameter_count, root_parameters, static_sampler_count, static_samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            rootSignatureDesc.Desc_1_1.Flags = is_local ? D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE : D3D12_ROOT_SIGNATURE_FLAG_NONE;
            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;

            HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
            if (FAILED(hr))
            {
                LOG_ERROR("Failed to serialize root signature: {}", error ? static_cast<const char *>(error->GetBufferPointer()) : "Unknown error");
                throw std::runtime_error("Failed to serialize root signature");
            }

            hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(rootSig->GetAddressOf()));
            if (FAILED(hr))
            {
                LOG_ERROR("Failed to create root signature, hr = 0x{:08X}", static_cast<u32>(hr));
                throw std::runtime_error("Failed to create root signature");
            }

        };

        CD3DX12_DESCRIPTOR_RANGE1 ranges[kMaxRootParameterCount]{};
        CD3DX12_ROOT_PARAMETER1 rootParameters[kMaxRootParameterCount]{};
        u32 root_param_index = 0;
        auto assert_capacity = [](u32 index)
        {
            AL_ASSERT_MSG(index < kMaxRootParameterCount, "GenerateRootSignature root parameter count exceeds 32");
        };

        auto append_bindless_table = [&](const char* resource_name, D3D12_DESCRIPTOR_RANGE_TYPE range_type, u32 capacity)
        {
            auto bindless_it = std::find_if(bind_res_infos.begin(), bind_res_infos.end(), [resource_name](auto it) -> bool
            {
                return it.second._name == resource_name;
            });
            if (bindless_it == bind_res_infos.end())
                return;

            assert_capacity(root_param_index);
            ranges[root_param_index].Init(range_type,
                                          capacity,
                                          bindless_it->second._res_slot,
                                          bindless_it->second._register_space,
                                          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
            rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
            bindless_it->second._bind_slot = root_param_index++;
        };

        append_bindless_table("g_bindless_texture2d", D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GPUVisibleDescriptorAllocator::kBindlessSRVCapacity);
        append_bindless_table("g_bindless_buffer", D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GPUVisibleDescriptorAllocator::kBindlessSRVCapacity);
        append_bindless_table("g_bindless_rw_texture2d", D3D12_DESCRIPTOR_RANGE_TYPE_UAV, GPUVisibleDescriptorAllocator::kBindlessUAVCapacity);
        append_bindless_table("g_bindless_rw_buffer", D3D12_DESCRIPTOR_RANGE_TYPE_UAV, GPUVisibleDescriptorAllocator::kBindlessUAVCapacity);
        for (auto it = bind_res_infos.begin(); it != bind_res_infos.end(); it++)
        {
            auto &desc = it->second;
            if (desc._name == "g_bindless_texture2d" || desc._name == "g_bindless_buffer" || desc._name == "g_bindless_rw_texture2d" || desc._name == "g_bindless_rw_buffer")
                continue;
            switch (desc._res_type)
            {
                case Render::EBindResDescType::kTexture2D:
                case Render::EBindResDescType::kTexture3D:
                case Render::EBindResDescType::kBuffer:
                case Render::EBindResDescType::kAccelerationStructure:
                {
                    assert_capacity(root_param_index);
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index++;
                }
                break;
                case Render::EBindResDescType::kUAVTexture2D:
                case Render::EBindResDescType::kRWTexture3D:
                case Render::EBindResDescType::kRWBuffer:
                {
                    assert_capacity(root_param_index);
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index++;
                }
                break;
                case Render::EBindResDescType::kConstBuffer:
                {
                    assert_capacity(root_param_index);
                    rootParameters[root_param_index].InitAsConstantBufferView(desc._res_slot, desc._register_space);
                    desc._bind_slot = root_param_index++;
                }
            default:
                break;
            }
        }
        
        if (static_samplers.size())
            create_root_signature(device, is_local_root_signature, root_param_index, rootParameters, root_signature, static_cast<u32>(static_samplers.size()), static_samplers.data());
        else
            create_root_signature(device, is_local_root_signature, root_param_index, rootParameters, root_signature);
    }
}