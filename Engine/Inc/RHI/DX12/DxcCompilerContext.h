#pragma once

#include <dxcapi.h>
#include <wrl/client.h>

namespace Ailu::RHI::DX12
{
    using Microsoft::WRL::ComPtr;

    class DxcCompilerContext
    {
    public:
        DxcCompilerContext();

        IDxcUtils *Utils() const { return _utils.Get(); }
        IDxcCompiler3 *Compiler() const { return _compiler.Get(); }
        bool IsValid() const { return _utils != nullptr && _compiler != nullptr; }

    private:
        ComPtr<IDxcUtils> _utils;
        ComPtr<IDxcCompiler3> _compiler;
    };

    DxcCompilerContext &GetThreadDxcCompilerContext();
}
