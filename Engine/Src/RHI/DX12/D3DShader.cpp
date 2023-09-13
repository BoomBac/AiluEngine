#include "pch.h"
#include <d3dcompiler.h>
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "Framework/Common/Utils.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	static uint32_t GetPerFramePropertyOffset(std::string_view name)
	{
		if (name == "_MatrixV")
			return 0;
		else if (name == "_MatrixVP")
			return 64;
		else if (name == "_Color")
			return D3DConstants::kPerFrameDataSize;
	}
	D3DShader::D3DShader(const std::string_view file_name, EShaderType type)
	{		
		if (!_b_init_buffer)
		{
			m_cbvHeap = D3DContext::GetInstance()->GetDescriptorHeap();
			_desc_size = D3DContext::GetInstance()->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			_p_cbuffer = D3DContext::GetInstance()->GetCBufferPtr();
			_b_init_buffer = true;
		}
#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif         
		if (type == EShaderType::kVertex)
		{
			ThrowIfFailed(D3DCompileFromFile(ToWChar(file_name.data()), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", compileFlags, 0, _p_blob.GetAddressOf(), nullptr));
		}
		else if (type == EShaderType::kPixel)
		{
			ThrowIfFailed(D3DCompileFromFile(ToWChar(file_name.data()), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", compileFlags, 0, _p_blob.GetAddressOf(), nullptr));
		}

		{
			D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

			// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			auto device = D3DContext::GetInstance()->GetDevice();
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
			{
				featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			}

			CD3DX12_DESCRIPTOR_RANGE1 ranges[3]{};
			CD3DX12_ROOT_PARAMETER1 rootParameters[3]{};

			ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
			ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
			ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_VERTEX);

			// Allow input layout and deny uneccessary access to certain pipeline stages.
			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(_p_sig.GetAddressOf())));
			s_active_sig = _p_sig;
		}
	}
	D3DShader::~D3DShader()
	{
	}

	void* D3DShader::GetByteCode()
	{
		return reinterpret_cast<void*>(_p_blob.Get());
	}

	uint8_t* D3DShader::GetCBufferPtr(uint32_t index)
	{
		if (index > D3DConstants::kMaxMaterialDataCount)
		{
			AL_ASSERT(true, "Material num more than MaxMaterialDataCount!");
			return nullptr;
		}
		return _p_cbuffer + D3DConstants::kPerFrameDataSize + index * D3DConstants::kPerMaterialDataSize;
	}

	void D3DShader::Bind()
	{
		auto cmdlist = D3DContext::GetInstance()->GetCmdList();
		cmdlist->SetGraphicsRootSignature(_p_sig.Get());
		D3D12_GPU_DESCRIPTOR_HANDLE matHandle;
		matHandle.ptr = m_cbvHeap->GetGPUDescriptorHandleForHeapStart().ptr + _desc_size;	
		cmdlist->SetGraphicsRootDescriptorTable(1, matHandle);
	}

	void D3DShader::Bind(uint32_t index)
	{
		if (index > D3DConstants::kMaxMaterialDataCount)
		{
			AL_ASSERT(true, "Material num more than MaxMaterialDataCount!");
			return;
		}
		auto cmdlist = D3DContext::GetInstance()->GetCmdList();
		cmdlist->SetGraphicsRootSignature(_p_sig.Get());
		D3D12_GPU_DESCRIPTOR_HANDLE matHandle;
		matHandle.ptr = m_cbvHeap->GetGPUDescriptorHandleForHeapStart().ptr + _desc_size + _desc_size * index;
		cmdlist->SetGraphicsRootDescriptorTable(1, matHandle);
	}

	void D3DShader::SetGlobalVector(const std::string_view name, const Vector4f& vector)
	{
		memcpy(_p_cbuffer + GetPerFramePropertyOffset(name), &vector, sizeof(vector));
	}

	void D3DShader::SetGlobalVector(const std::string_view name, const Vector3f& vector)
	{
	}

	void D3DShader::SetGlobalVector(const std::string_view name, const Vector2f& vector)
	{
	}

	void D3DShader::SetGlobalMatrix(const std::string_view name, const Matrix4x4f& mat)
	{
		memcpy(_p_cbuffer + GetPerFramePropertyOffset(name), &mat, sizeof(mat));
	}
	void D3DShader::SetGlobalMatrix(const std::string_view name, const Matrix3x3f& mat)
	{
	}

	ComPtr<ID3D12RootSignature> D3DShader::GetSignature() const
	{
		return _p_sig;
	}
	ComPtr<ID3D12RootSignature> D3DShader::GetCurrentActiveSignature()
	{
		return s_active_sig;
	}
}
