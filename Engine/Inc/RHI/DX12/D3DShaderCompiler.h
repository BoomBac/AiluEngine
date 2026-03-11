#pragma once

#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Render/RenderConstants.h"

using Microsoft::WRL::ComPtr;

namespace Ailu::RHI::DX12
{
    struct D3DShaderCompileDesc
    {
        std::wstring _filename;
        std::string _entry_point;
        std::string _target;
        Vector<D3D_SHADER_MACRO> _keywords;
        bool _is_load_cache = true;
        bool _skip_entry_point = false;
    };

    struct D3DShaderCompileOutput
    {
        ComPtr<ID3DBlob> _byte_code;
        ComPtr<ID3DBlob> _reflection_blob;
        ComPtr<ID3D12ShaderReflection> _shader_reflection;
        ComPtr<ID3D12LibraryReflection> _library_reflection;
        std::set<WString> _include_files;
    };

    bool ParseIncludeDependencies(const WString &source_file_path,
                                  std::set<WString> &include_files,
                                  const Vector<WString> &search_paths = {});

    bool CreateFromFileDXC(const D3DShaderCompileDesc &desc, D3DShaderCompileOutput &output);

    bool CreateFromFileFXC(const D3DShaderCompileDesc &desc, D3DShaderCompileOutput &output);

    bool CreateFromFileDXC(const std::wstring &filename, const std::string &entryPoint, const std::string &target, const Vector<D3D_SHADER_MACRO> &keywords, ComPtr<ID3DBlob> &p_blob,
                           ComPtr<ID3D12ShaderReflection> &shader_reflection,
                           std::set<WString> &include_files,
                           bool is_load_cache = true);

    bool CreateFromFileFXC(const std::wstring &filename, const std::string &entryPoint, const std::string &target, const Vector<D3D_SHADER_MACRO> &keywords, ComPtr<ID3DBlob> &p_blob,
                           ComPtr<ID3D12ShaderReflection> &shader_reflection,
                           std::set<WString> &include_files,
                           bool is_load_cache = true);
}