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
			auto device = D3DContext::GetInstance()->GetDevice();
			//constbuffer desc heap
			D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
			cbvHeapDesc.NumDescriptors = (1u + D3DConstants::kMaxMaterialDataCount + D3DConstants::kMaxRenderObjectCount) * D3DConstants::kFrameCount;
			cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
			_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			//constbuffer
			auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(D3DConstants::kPerFrameTotalSize * D3DConstants::kFrameCount);
			ThrowIfFailed(device->CreateCommittedResource(&heap_prop,D3D12_HEAP_FLAG_NONE,&res_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&m_constantBuffer)));
			// Describe and create a constant buffer view.
			for (uint32_t i = 0; i < D3DConstants::kFrameCount; i++)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle;
				cbvHandle.ptr = m_cbvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * (1 + D3DConstants::kMaxMaterialDataCount + D3DConstants::kMaxRenderObjectCount) * _desc_size;
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
				cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i * D3DConstants::kPerFrameTotalSize;
				cbv_desc.SizeInBytes = D3DConstants::kPerFrameDataSize;
				device->CreateConstantBufferView(&cbv_desc, cbvHandle);

				for (uint32_t j = 0; j < D3DConstants::kMaxMaterialDataCount; j++)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
					cbvHandle2.ptr = cbvHandle.ptr + (j + 1) * _desc_size;
					cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i *  D3DConstants::kPerFrameTotalSize + D3DConstants::kPerFrameDataSize + j * D3DConstants::kPerMaterialDataSize;
					cbv_desc.SizeInBytes = D3DConstants::kPerMaterialDataSize;
					device->CreateConstantBufferView(&cbv_desc, cbvHandle2);
				}
				for (uint32_t k = 0; k < D3DConstants::kMaxRenderObjectCount; k++)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
					cbvHandle2.ptr = cbvHandle.ptr + (D3DConstants::kMaxMaterialDataCount + 1) * _desc_size;
					cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + D3DConstants::kPerFrameTotalSize * i + D3DConstants::kPerFrameDataSize + 
						D3DConstants::kMaxMaterialDataCount * D3DConstants::kPerMaterialDataSize + k * D3DConstants::kPeObjectDataSize;
					cbv_desc.SizeInBytes = D3DConstants::kPeObjectDataSize;
					device->CreateConstantBufferView(&cbv_desc, cbvHandle2);
				}
			}
			// Map and initialize the constant buffer. We don't unmap this until the
			// app closes. Keeping things mapped for the lifetime of the resource is okay.
			CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
			ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&_p_cbuffer)));
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
			ThrowIfFailed(D3DCompileFromFile(ToWChar(file_name.data()), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, _p_blob.GetAddressOf(), nullptr));
		}
		else if (type == EShaderType::kPixel)
		{
			ThrowIfFailed(D3DCompileFromFile(ToWChar(file_name.data()), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, _p_blob.GetAddressOf(), nullptr));
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

			CD3DX12_DESCRIPTOR_RANGE1 ranges[2]{};
			CD3DX12_ROOT_PARAMETER1 rootParameters[2]{};

			ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);

			ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

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
		return _p_cbuffer + D3DConstants::kPerFrameDataSize + index * D3DConstants::kPerMaterialDataSize;
	}

	void D3DShader::Bind()
	{
		auto cmdlist = D3DContext::GetInstance()->GetCmdList();
		cmdlist->SetGraphicsRootSignature(_p_sig.Get());
		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
		cmdlist->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		cmdlist->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());
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
		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
		cmdlist->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		cmdlist->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());
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
}
