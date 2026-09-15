#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <mutex>
#include <unordered_map>

#include "Framework/Core/CoreMinimal.h"

namespace Ailu::RHI::DX12
{
    using Microsoft::WRL::ComPtr;

    class D3DRootSignaturePool
    {
    public:
        static D3DRootSignaturePool &Get();

        ComPtr<ID3D12RootSignature> GetOrCreate(u64 root_signature_hash, const Vector<u8> &serialized_blob);
        void Clear();

    private:
        std::mutex _mutex;
        HashMap<u64, ComPtr<ID3D12RootSignature>> _root_signatures;
    };
}
