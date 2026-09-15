#include "pch.h"

#include "RHI/DX12/DxcCompilerContext.h"

namespace Ailu::RHI::DX12
{
    DxcCompilerContext::DxcCompilerContext()
    {
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&_utils));
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&_compiler));
    }

    DxcCompilerContext &GetThreadDxcCompilerContext()
    {
        thread_local DxcCompilerContext s_context;
        return s_context;
    }
}
