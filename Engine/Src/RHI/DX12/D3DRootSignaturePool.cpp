#include "pch.h"

#include "Framework/Common/Log.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DRootSignaturePool.h"

namespace Ailu::RHI::DX12
{
    D3DRootSignaturePool &D3DRootSignaturePool::Get()
    {
        static D3DRootSignaturePool s_pool;
        return s_pool;
    }

    ComPtr<ID3D12RootSignature> D3DRootSignaturePool::GetOrCreate(u64 root_signature_hash,
                                                                   const Vector<u8> &serialized_blob)
    {
        std::lock_guard lock(_mutex);
        if (auto it = _root_signatures.find(root_signature_hash); it != _root_signatures.end())
            return it->second;
        if (serialized_blob.empty())
            return nullptr;

        auto device = static_cast<D3DContext &>(Render::GraphicsContext::Get()).GetDevice();
        if (device == nullptr)
            return nullptr;
        ComPtr<ID3D12RootSignature> root_signature;
        const HRESULT hr = device->CreateRootSignature(0, serialized_blob.data(), serialized_blob.size(),
                                                        IID_PPV_ARGS(root_signature.GetAddressOf()));
        if (FAILED(hr))
        {
            LOG_ERROR("D3DRootSignaturePool: CreateRootSignature failed, hr = 0x{:08X}", static_cast<u32>(hr));
            return nullptr;
        }
        _root_signatures.emplace(root_signature_hash, root_signature);
        return root_signature;
    }

    void D3DRootSignaturePool::Clear()
    {
        std::lock_guard lock(_mutex);
        _root_signatures.clear();
    }
}
