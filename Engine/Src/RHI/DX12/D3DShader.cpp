#include "pch.h"
#include <d3dcompiler.h>
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "Framework/Common/Utils.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
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
					cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + i * D3DConstants::kPerFrameDataSize + j * D3DConstants::kPerMaterialDataSize;
					cbv_desc.SizeInBytes = D3DConstants::kPerMaterialDataSize;
					device->CreateConstantBufferView(&cbv_desc, cbvHandle2);
				}
				for (uint32_t k = 0; k < D3DConstants::kMaxRenderObjectCount; k++)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle2;
					cbvHandle2.ptr = cbvHandle.ptr + (D3DConstants::kMaxMaterialDataCount + 1) * _desc_size;
					cbv_desc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + D3DConstants::kPerFrameDataSize * i + 
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
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;
#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif         
		ThrowIfFailed(D3DCompileFromFile(ToWChar(file_name.data()), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(ToWChar(file_name.data()), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));		
	}
	D3DShader::~D3DShader()
	{
	}
	void D3DShader::Bind()
	{
	}
	void D3DShader::SetGlobalVector(const std::string_view name, const Vector4f& vector)
	{
	}
	void D3DShader::SetGlobalVector(const std::string_view name, const Vector3f& vector)
	{
	}
	void D3DShader::SetGlobalVector(const std::string_view name, const Vector2f& vector)
	{
	}
	void D3DShader::SetGlobalMatrix(const std::string_view name, const Matrix4x4f& mat)
	{
	}
	void D3DShader::SetGlobalMatrix(const std::string_view name, const Matrix3x3f& mat)
	{
	}
}
