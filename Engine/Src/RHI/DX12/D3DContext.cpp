#include "pch.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include <limits>
#include <dxgidebug.h>
#include "Framework/Common/Log.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/RenderingData.h"




namespace Ailu
{
	static void GetHardwareAdapter(IDXGIFactory6* pFactory, IDXGIAdapter4** ppAdapter)
	{
		IDXGIAdapter* pAdapter = nullptr;
		IDXGIAdapter4* pAdapter4 = nullptr;
		*ppAdapter = nullptr;
		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters(adapterIndex, &pAdapter); adapterIndex++)
		{
			DXGI_ADAPTER_DESC3 desc{};
			if (SUCCEEDED(pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter4))))
			{
				pAdapter4->GetDesc3(&desc);
				LOG_INFO(desc.Description);
			}
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				continue;
			}
			// Check to see if the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(pAdapter4, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
		*ppAdapter = pAdapter4;
	}

	static u8* GetPerRenderObjectCbufferPtr(u8* begin, u32 object_index)
	{
		return begin + RenderConstants::kPerFrameDataSize + RenderConstants::kPerMaterialDataSize * RenderConstants::kMaxMaterialDataCount + object_index * RenderConstants::kPeObjectDataSize;
	}


	D3DContext::D3DContext(WinWindow* window) : _window(window),_width(window->GetWidth()),_height(window->GetHeight())
	{
		m_aspectRatio = (float)_width / (float)_height;
	}

	D3DContext::~D3DContext()
	{
		Destroy();
		LOG_INFO("D3DContext Destroy");
	}

	void D3DContext::Init()
	{
		s_p_d3dcontext = this;
		LoadPipeline();
		LoadAssets();
#ifdef DEAR_IMGUI
		//init imgui
		{
			D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
			gpu_handle.ptr = m_cbvHeap->GetGPUDescriptorHandleForHeapStart().ptr + _cbv_desc_size * _cbv_desc_num;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
			cpu_handle.ptr = m_cbvHeap->GetCPUDescriptorHandleForHeapStart().ptr + _cbv_desc_size * _cbv_desc_num;
			auto ret = ImGui_ImplDX12_Init(m_device.Get(), RenderConstants::kFrameCount,
				RenderConstants::kColorRange == EColorRange::kLDR ? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat),
				m_cbvHeap.Get(), cpu_handle, gpu_handle);
		}
#endif // DEAR_IMGUI
		Gizmo::Init();
		m_commandList->Close();
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
		WaitForGpu();
	}

	ComPtr<ID3D12DescriptorHeap> D3DContext::GetDescriptorHeap()
	{
		return m_cbvHeap;
	}

	std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> D3DContext::GetSRVDescriptorHandle()
	{
		static u32 global_texture_offset = 0u;
		AL_ASSERT(global_texture_offset > (RenderConstants::kMaxTextureCount - 1), "Can't alloc more texture");
		static u32 base = RenderConstants::kFrameCount + RenderConstants::kMaxMaterialDataCount * RenderConstants::kFrameCount +
			RenderConstants::kMaxRenderObjectCount * RenderConstants::kFrameCount + RenderConstants::kMaxPassDataCount * RenderConstants::kFrameCount;
		auto gpu_handle = m_cbvHeap->GetGPUDescriptorHandleForHeapStart();
		gpu_handle.ptr += _cbv_desc_size * (base + global_texture_offset);
		auto cpu_handle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu_handle.ptr += _cbv_desc_size * (base + global_texture_offset);
		++global_texture_offset;
		return std::make_tuple(cpu_handle, gpu_handle);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3DContext::GetRTVDescriptorHandle()
	{
		static u32 global_texture_offset = 0u;
		AL_ASSERT(global_texture_offset > (RenderConstants::kMaxRenderTextureCount - 1), "Can't alloc more texture");
		static u32 base = RenderConstants::kFrameCount;
		//auto gpu_handle = m_rtvHeap->GetGPUDescriptorHandleForHeapStart();
		//gpu_handle.ptr += _rtv_desc_size * (base + global_texture_offset);
		auto cpu_handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu_handle.ptr += _rtv_desc_size * (base + global_texture_offset);
		++global_texture_offset;
		return cpu_handle;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3DContext::GetDSVDescriptorHandle()
	{
		static u32 global_texture_offset = 0u;
		AL_ASSERT(global_texture_offset > (RenderConstants::kMaxTextureCount - 1), "Can't alloc more texture");
		static u32 base = RenderConstants::kFrameCount;
		//auto gpu_handle = m_dsvHeap->GetGPUDescriptorHandleForHeapStart();
		//gpu_handle.ptr += _dsv_desc_size * (base + global_texture_offset);
		auto cpu_handle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu_handle.ptr += _dsv_desc_size * (base + global_texture_offset);
		++global_texture_offset;
		return cpu_handle;
	}

	std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> D3DContext::GetUAVDescriptorHandle()
	{
		static u32 global_texture_offset = 0u;
		AL_ASSERT(global_texture_offset > (RenderConstants::kMaxUAVTextureCount - 1), "Can't alloc more uav texture");
		static u32 base = RenderConstants::kFrameCount + RenderConstants::kMaxMaterialDataCount * RenderConstants::kFrameCount +
			RenderConstants::kMaxRenderObjectCount * RenderConstants::kFrameCount + RenderConstants::kMaxPassDataCount * RenderConstants::kFrameCount + RenderConstants::kMaxTextureCount;
		auto gpu_handle = m_cbvHeap->GetGPUDescriptorHandleForHeapStart();
		gpu_handle.ptr += _cbv_desc_size * (base + global_texture_offset);
		auto cpu_handle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu_handle.ptr += _cbv_desc_size * (base + global_texture_offset);
		++global_texture_offset;
		return std::make_tuple(cpu_handle, gpu_handle);
	}

	u64 D3DContext::ExecuteCommandBuffer(Ref<CommandBuffer>& cmd)
	{    
		//for (auto& call : cmd->GetAllCommands())
		//{
		//	_all_commands.emplace_back(std::move(call));
		//}
		cmd->Close();
		ID3D12CommandList* ppCommandLists[] = { static_cast<D3DCommandBuffer*>(cmd.get())->GetCmdList() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
		++_fence_value;
		ThrowIfFailed(m_commandQueue->Signal(_p_cmd_buffer_fence.Get(), _fence_value));
		if (_cmd_target_fence_value.contains(cmd->GetID()))
			_cmd_target_fence_value[cmd->GetID()] = _fence_value;
		else
			_cmd_target_fence_value.insert(std::make_pair(cmd->GetID(), _fence_value));
		return _fence_value;
	}

	void D3DContext::BeginBackBuffer(CommandBuffer* cmd)
	{
		auto dxcmd = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
		auto bar_before = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		dxcmd->ResourceBarrier(1, &bar_before);
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, _rtv_desc_size);
		dxcmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		dxcmd->ClearRenderTargetView(rtvHandle, Colors::kBlack, 0, nullptr);
	}

	void D3DContext::EndBackBuffer(CommandBuffer* cmd)
	{
		auto dxcmd = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
		auto bar_after = CD3DX12_RESOURCE_BARRIER::Transition(_color_buffer[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		dxcmd->ResourceBarrier(1, &bar_after);
	}

	void D3DContext::DrawOverlay(CommandBuffer* cmd)
	{
		auto dxcmd = static_cast<D3DCommandBuffer*>(cmd)->GetCmdList();
		Gizmo::Submit(cmd);
#ifdef DEAR_IMGUI
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxcmd);
#endif // DEAR_IMGUI
	}

	void D3DContext::Destroy()
	{
		// Ensure that the GPU is no longer referencing resources that are about to be
// cleaned up by the destructor.
		WaitForGpu();
		CloseHandle(m_fenceEvent);
		//m_swapChain.Reset();
		//m_device.Reset();
		//for(int i = 0; i < RenderConstants::kFrameCount; ++i)
		//{
		//	_color_buffer[i].Reset();
		//	_depth_buffer[i].Reset();
		//	m_commandAllocators[i].Reset();
		//}
		//m_commandQueue.Reset();
		//m_commandList.Reset();
		//m_rtvHeap.Reset();
		//m_cbvHeap.Reset();
		//m_dsvHeap.Reset();
		// 在程序终止时调用 ReportLiveObjects() 函数
		IDXGIDebug1* pDebug = nullptr;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
		{
			pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
			pDebug->Release();
		}
	}

	void D3DContext::LoadPipeline()
	{
		UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
		// Enable the debug layer (requires the Graphics Tools "optional feature").
		// NOTE: Enabling the debug layer after device creation will invalidate the active device.
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();

				// Enable additional debug layers.
				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
		}
#endif
		ComPtr<IDXGIFactory6> factory;
		ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
		ComPtr<IDXGIAdapter4> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);
		ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&m_device)));
		// Describe and create the command queue.
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
		// Describe and create the swap chain.
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = RenderConstants::kFrameCount;
		swapChainDesc.Width = _width;
		swapChainDesc.Height = _height;
		swapChainDesc.Format = RenderConstants::kColorRange == EColorRange::kLDR? ConvertToDXGIFormat(RenderConstants::kLDRFormat) : ConvertToDXGIFormat(RenderConstants::kHDRFormat);
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		auto hwnd = static_cast<HWND>(_window->GetNativeWindowPtr());
		ComPtr<IDXGISwapChain1> swapChain;
		ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(),hwnd,&swapChainDesc,nullptr,nullptr,&swapChain));
		// This sample does not support fullscreen transitions.
		ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
		ThrowIfFailed(swapChain->SetFullscreenState(FALSE, nullptr));
		ThrowIfFailed(swapChain.As(&m_swapChain));

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		CreateDescriptorHeap();

		// Create frame resources.
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

			// Create a RTV for each frame.
			for (UINT n = 0; n < RenderConstants::kFrameCount; n++)
			{
				ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&_color_buffer[n])));
				m_device->CreateRenderTargetView(_color_buffer[n].Get(), nullptr, rtvHandle);
				rtvHandle.Offset(1, _rtv_desc_size);
				ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
			}
		}
		CreateDepthStencilTarget();
		// Create the command list.
		ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
	}


	void D3DContext::LoadAssets()
	{
		GraphicsPipelineStateMgr::BuildPSOCache();


		// Create synchronization objects.
		{
			//ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
			ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(_p_cmd_buffer_fence.GetAddressOf())));
			//m_fenceValues[m_frameIndex]++;
			m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (m_fenceEvent == nullptr)
			{
				ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
			}
			WaitForGpu();
		}
	}

	void D3DContext::Present()
	{
		while (!_resource_task.empty())
		{
			_resource_task.front()();
			_resource_task.pop();
		}
		ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
		ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));
		m_commandList->SetDescriptorHeaps(1, m_cbvHeap.GetAddressOf());
		ThrowIfFailed(m_commandList->Close());
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
#ifdef DEAR_IMGUI
		ImGuiIO& io = ImGui::GetIO();
		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault(nullptr, (void*)m_commandList.Get());
		}
#endif // DEAR_IMGUI
		// Present the frame.
		ThrowIfFailed(m_swapChain->Present(1, 0));
		WaitForGpu();
		//CommandBufferPool::ReleaseAll();
	}

	const u64& D3DContext::GetFenceValue(const u32& cmd_index) const
	{
		static u64 u64_max = std::numeric_limits<u64>::max();
		if (!_cmd_target_fence_value.contains(cmd_index))
		{
			LOG_WARNING("GetFenceValue with invaild cmd index");
			return u64_max;
		}
		return _cmd_target_fence_value.at(cmd_index);
	}

	u64 D3DContext::GetCurFenceValue() const
	{
		return _p_cmd_buffer_fence->GetCompletedValue();
	}

	void D3DContext::SubmitRHIResourceBuildTask(RHIResourceTask task)
	{
		_resource_task.push(task);
	}


	void D3DContext::WaitForGpu()
	{
		FlushCommandQueue(m_commandQueue.Get(), _p_cmd_buffer_fence.Get(), _fence_value);
	}

	void D3DContext::FlushCommandQueue(ID3D12CommandQueue* cmd_queue, ID3D12Fence* fence, u64& fence_value)
	{
		++fence_value;
		_timer.Mark();
		ThrowIfFailed(cmd_queue->Signal(fence,fence_value));
		if (fence->GetCompletedValue() < fence_value)
		{
			ThrowIfFailed(fence->SetEventOnCompletion(fence_value, m_fenceEvent));
			WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
		}
		RenderingStates::s_gpu_latency = _timer.GetElapsedSinceLastMark();
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	void D3DContext::InitCBVSRVUAVDescHeap()
	{
		auto device = D3DContext::GetInstance()->GetDevice();
		//constbuffer desc heap
		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
		_cbv_desc_num = (1u + RenderConstants::kMaxMaterialDataCount + RenderConstants::kMaxRenderObjectCount + RenderConstants::kMaxPassDataCount) * RenderConstants::kFrameCount +
			RenderConstants::kMaxTextureCount + RenderConstants::kMaxUAVTextureCount;
		cbvHeapDesc.NumDescriptors = _cbv_desc_num + 1;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		ThrowIfFailed(device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
		_cbv_desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	void D3DContext::CreateDepthStencilTarget()
	{
		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC depthDesc = {};
		depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthDesc.Width = _width;
		depthDesc.Height = _height;
		//depthDesc.Format = DXGI_FORMAT_D32_FLOAT; // 选择深度缓冲格式
		depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; // 选择深度缓冲格式
		depthDesc.MipLevels = 1;
		depthDesc.DepthOrArraySize = 1;
		depthDesc.SampleDesc.Count = 1; // 非多重采样
		depthDesc.SampleDesc.Quality = 0;
		depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		D3D12_CLEAR_VALUE depthClearValue = {};
		depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthClearValue.DepthStencil.Depth = 1.0f;
		depthClearValue.DepthStencil.Stencil = 0;

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		//dsvDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; // 与深度缓冲资源的格式匹配
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 与深度缓冲资源的格式匹配
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
		for (size_t i = 0; i < RenderConstants::kFrameCount; i++)
		{
			ThrowIfFailed(m_device->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&depthDesc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&depthClearValue,
				IID_PPV_ARGS(&_depth_buffer[i])
			));
			m_device->CreateDepthStencilView(_depth_buffer[i].Get(), &dsvDesc, dsv_handle);
			dsv_handle.Offset(1,_dsv_desc_size);
		}
	}

	void D3DContext::CreateDescriptorHeap()
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = RenderConstants::kFrameCount + RenderConstants::kMaxRenderTextureCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
		_rtv_desc_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = RenderConstants::kFrameCount + RenderConstants::kMaxRenderTextureCount / 2u;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(m_dsvHeap.GetAddressOf())));
		_dsv_desc_size = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		InitCBVSRVUAVDescHeap();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE D3DContext::GetCBVGPUDescHandle(u32 index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle{};
		handle.ptr = m_cbvHeap->GetGPUDescriptorHandleForHeapStart().ptr + _cbv_desc_size * index;
		return handle;
	}
}