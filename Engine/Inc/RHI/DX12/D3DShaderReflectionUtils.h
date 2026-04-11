#pragma once

#include <d3d12shader.h>
#include <unordered_map>
#include <d3dx12.h>
#include <wrl/client.h>

#include "Render/CoreType.h"

namespace Ailu::RHI::DX12::ShaderReflectionUtils
{
    using Microsoft::WRL::ComPtr;

    using ShaderBindResourceMap = std::unordered_map<String, Render::ShaderBindResourceInfo>;

    std::pair<String, Render::ShaderBindResourceInfo> ParseBindResource(const D3D12_SHADER_INPUT_BIND_DESC &bind_desc);
    Render::ShaderBindResourceInfo ParseBindVariable(const D3D12_SHADER_VARIABLE_DESC &desc);

    void AppendShaderResources(ID3D12ShaderReflection *reflection,
                               ShaderBindResourceMap &bind_res_infos,
                               bool apply_cbuffer_bind_flags = true);

    void AppendFunctionResources(ID3D12FunctionReflection *reflection,
                                 ShaderBindResourceMap &bind_res_infos,
                                 bool apply_cbuffer_bind_flags = true);

    void AppendLibraryResources(ID3D12LibraryReflection *reflection,ShaderBindResourceMap &bind_res_infos,bool apply_cbuffer_bind_flags = true);
    
    //根据bind_res_infos创建根签名，并填充根参数的绑定槽位（_bind_slot）
    void GenerateRootSignature(ID3D12Device *device, ShaderBindResourceMap& bind_res_infos, ComPtr<ID3D12RootSignature> *root_signature, bool is_local_root_signature,Vector<CD3DX12_STATIC_SAMPLER_DESC> static_samplers = {});
}