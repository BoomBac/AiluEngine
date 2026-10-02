#include "pch.h"
#include "Render/Buffer.h"
#include "Render/FrameResource.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/UploadBuffer.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/Allocator.hpp"
#include <algorithm>
#include <thread>

namespace Ailu::Render
{
        GPUBuffer::~GPUBuffer()
        {
                if (_initial_data != nullptr)
                        AL_FREE(_initial_data);
        }

        void GPUBuffer::SetData(const u8 *data, u32 size)
        {
                SetDataInternal(data, size, false);
        }

        void GPUBuffer::SetPersistentData(const u8 *data, u32 size)
        {
                SetDataInternal(data, size, true);
        }

        void GPUBuffer::SetDataInternal(const u8 *data, u32 size, bool persistent)
        {
                AL_ASSERT(data != nullptr || size == 0u);
                AL_ASSERT(size <= _mem_size);
                _counter = _desc._element_size == 0u ? 0u : size / _desc._element_size;

                if (!IsPhysicalResourceCreated())
                {
                        if (_initial_data != nullptr)
                                AL_FREE(_initial_data);
                        _initial_data = nullptr;
                        _initial_data_size = size;
                        if (size != 0u)
                        {
                                _initial_data = reinterpret_cast<u8 *>(AL_ALLOC(u8, size));
                                memcpy(_initial_data, data, size);
                        }
                        _current_data_version = {};
                        _persistent_upload.reset();
                        return;
                }

                FrameUploadAllocation upload;
                if (persistent && size != 0u)
                {
                        auto owner = AdoptGpuResource(new RHI::DX12::UploadBuffer(
                                std::format("{}_persistent_upload", Name()), Math::AlignTo(size, 256u)));
                        const auto allocation = owner->Allocate(size, 16u);
                        upload._owner = owner.get();
                        upload._cpu_ptr = allocation.CPU;
                        upload._gpu_handle = allocation.GPU;
                        upload._native_page = {RendererAPI::ERenderAPI::kDirectX12, allocation._page_res};
                        upload._offset = allocation._offset;
                        upload._size = size;
                        _persistent_upload = std::move(owner);
                }
                else
                {
                        _persistent_upload.reset();
                        if (size != 0u)
                                upload = FrameResourceManager::Get().AllocFrameUpload(size, 16u);
                }
                if (size != 0u)
                        memcpy(upload._cpu_ptr, data, size);
                _current_data_version._upload = upload;
                _current_data_version._generation = ++_data_generation;
                _current_data_version._size = size;
        }

        #pragma region GPUBuffer
	Ref<GPUBuffer> GPUBuffer::Create(BufferDesc desc, const String &name)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::ERenderAPI::kNone:
				AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
			case RendererAPI::ERenderAPI::kDirectX12:
			{
                                auto buf = AdoptGpuResource(new RHI::DX12::D3DGPUBuffer(desc));
				buf->Name(name);
				GraphicsContext::Get().CreateResource(buf.get());
				return buf;
			}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	Ref<GPUBuffer> GPUBuffer::CreateSync(BufferDesc desc, const String &name)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::ERenderAPI::kNone:
				AL_ASSERT_MSG(false, "None render api used!");
				return nullptr;
			case RendererAPI::ERenderAPI::kDirectX12:
			{
                                auto buf = AdoptGpuResource(new RHI::DX12::D3DGPUBuffer(desc));
				buf->Name(name);
				GraphicsContext::Get().CreateResourceSync(buf.get());
				return buf;
			}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}

	Ref<GPUBuffer> GPUBuffer::Create(EGPUBufferTarget target, u32 element_size, u32 element_num, const String &name)
	{
		BufferDesc desc;
		desc._element_num = element_num;
		desc._element_size = element_size;
		desc._size = element_num * element_size;
		desc._target = target;
		return Create(desc,name);
	}
	#pragma endregion

	#pragma region VertexBuffer
	Ref<VertexBuffer> VertexBuffer::Create(VertexBufferLayout layout, const String& name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto buf = new RHI::DX12::D3DVertexBuffer(layout);
            buf->Name(name);
			return AdoptGpuResource(buf);
		}
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	VertexBuffer::VertexBuffer(VertexBufferLayout layout)
	{
		_buffer_layout = std::move(layout);
		u16 stream_count = _buffer_layout.GetStreamCount();
		_stream_data.resize(RenderConstants::kMaxVertexAttrNum);
		_gpu_stream_buffers.resize(RenderConstants::kMaxVertexAttrNum);
		_bindless_srv_indices.resize(RenderConstants::kMaxVertexAttrNum, -1);
		_res_type = EGpuResType::kVertexBuffer;
	}
	void VertexBuffer::SetData(u8 *data, u32 size, u8 stream_index, u32 offset)
	{
		if (!IsReady())
			return;
		AL_ASSERT(stream_index < _stream_data.size());
		AL_ASSERT(offset + size <= _stream_data[stream_index]._size);
		memcpy(_stream_data[stream_index]._data, data + offset, size);
		_vertices_count = size / _buffer_layout[stream_index].Size;
	}
	void VertexBuffer::SetStream(u8 *data, u32 size, u8 stream_index, bool is_dynamic)
	{
		if (_buffer_layout.GetStride(stream_index) == 0)
		{
			AL_ASSERT_MSG(false, "Try to set a null stream!");
			return;
		}
		_vertices_count = size / _buffer_layout.GetStride(stream_index);
		_stream_data[stream_index] = {data, size, is_dynamic};
		_mem_size += size;
	}
	void VertexBuffer::SetGpuStream(GPUBuffer *buffer, u32 size, u8 stream_index)
	{
		if (buffer == nullptr || _buffer_layout.GetStride(stream_index) == 0u)
		{
			AL_ASSERT_MSG(false, "Try to set an invalid GPU vertex stream!");
			return;
		}
		if (stream_index >= _gpu_stream_buffers.size())
			_gpu_stream_buffers.resize(stream_index + 1u);
		if (_gpu_stream_buffers[stream_index].get() != buffer || _stream_data[stream_index]._size != size)
			++_view_version;
		_gpu_stream_buffers[stream_index] = std::dynamic_pointer_cast<GPUBuffer>(buffer->SharedFromThis());
		_stream_data[stream_index] = {nullptr, size, false};
		_vertices_count = size / _buffer_layout.GetStride(stream_index);
	}
	const VertexBuffer::ResolvedVertexLayout& VertexBuffer::ResolveLayout(const VertexBufferLayout &layout)
	{
		auto hash = layout.Hash();
		AL_ASSERT(hash < 64);
		auto &resolved = _resolved_layouts[hash];
		if (resolved._state.load(std::memory_order_acquire) == EResolvedVertexLayoutState::kReady)
			return resolved;

		auto expected_state = EResolvedVertexLayoutState::kEmpty;
		if (!resolved._state.compare_exchange_strong(expected_state, EResolvedVertexLayoutState::kBuilding,
		                                            std::memory_order_acq_rel, std::memory_order_acquire))
		{
			while (resolved._state.load(std::memory_order_acquire) != EResolvedVertexLayoutState::kReady)
				std::this_thread::yield();
			return resolved;
		}

		std::array<ResolvedVertexBinding, 30> bindings{};
		u8 binding_count = 0u;

		for (const auto &layout_ele : layout)
		{
			const auto it = _buffer_layout_indexer.find(layout_ele._semantic);
			if (it == _buffer_layout_indexer.end())
			{
				LOG_WARNING("Invalid vertex layout element {}{}", RenderConstants::GetVertexSemanticName(layout_ele._semantic),
							RenderConstants::GetVertexSemanticIndex(layout_ele._semantic));
				continue;
			}
			AL_ASSERT(binding_count < bindings.size());
			auto &binding = bindings[binding_count++];
			binding._slot = layout_ele.Stream;
			binding._stream_index = it->second;
		}

		std::sort(bindings.begin(), bindings.begin() + binding_count,
		          [](const ResolvedVertexBinding &lhs, const ResolvedVertexBinding &rhs) { return lhs._slot < rhs._slot; });
		u8 first_slot = 0u;
		bool is_slot_contiguous = false;
		if (binding_count != 0u)
		{
			first_slot = bindings[0]._slot;
			is_slot_contiguous = true;
			for (u8 i = 1u; i < binding_count; ++i)
			{
				if (bindings[i]._slot != first_slot + i)
				{
					is_slot_contiguous = false;
					break;
				}
			}
		}
		resolved._bindings = bindings;
		resolved._binding_count = binding_count;
		resolved._first_slot = first_slot;
		resolved._is_slot_contiguous = is_slot_contiguous;
		resolved._state.store(EResolvedVertexLayoutState::kReady, std::memory_order_release);
		return resolved;
	}

	#pragma endregion

	#pragma region IndexBuffer
	Ref<IndexBuffer> IndexBuffer::Create(u32* indices, u32 count, const String& name,bool is_dynamic )
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT_MSG(false, "None render api used!");
			return nullptr;
        case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto buf = new RHI::DX12::D3DIndexBuffer(indices, count, is_dynamic);
			buf->Name(name);
			return AdoptGpuResource(buf);
        }
		}
		AL_ASSERT_MSG(false, "Unsupported render api!");
		return nullptr;
	}
	IndexBuffer::IndexBuffer(u32 *indices, u32 count, bool is_dynamic)
	: _data((u8*)indices),_count(count), _capacity(count), _is_dynamic(is_dynamic)
	{
		_mem_size = sizeof(u32) * count;
		_res_type = EGpuResType::kIndexBuffer;
	}
	void IndexBuffer::SetData(u8* data,u32 size)
	{
		if (_data == nullptr || data == nullptr)
		{
			LOG_WARNING("Try to set a null data to index buffer!");
			return;
		}
		u32 max_size = _capacity * sizeof(u32);
		if (size > max_size)
		{
			size = max_size;
			LOG_WARNING("Try to set a larger data to index buffer! ({} > {})", size, max_size);
		}
		memcpy(_data, data, size);
		_count = size / sizeof(u32);
	}
	#pragma endregion

	#pragma region ConstantBuffer
        Ref<ConstantBuffer> ConstantBuffer::Create(u32 size,const String& name)
        {
                switch (Renderer::GetAPI())
                {
                case RendererAPI::ERenderAPI::kNone:
                        AL_ASSERT_MSG(false, "None render api used!");
                        return nullptr;
                case RendererAPI::ERenderAPI::kDirectX12:
                {
                        auto buffer = AdoptGpuResource(new RHI::DX12::D3DConstantBuffer(size));
                        buffer->Name(name);
                        return buffer;
                }
                }
                AL_ASSERT_MSG(false, "Unsupported render api!");
                return nullptr;
        }
	void ConstantBuffer::Release(ConstantBuffer* ptr)
	{
		if (ptr != nullptr)
			GpuResourceRegistry::Get().Release(ptr->Handle());
	}
	#pragma endregion

	#pragma region ConstBufferPool
	ConstBufferPool *s_ConstBufferPool = nullptr;
	void ConstBufferPool::Init()
	{
		if (!s_ConstBufferPool)
			s_ConstBufferPool = AL_NEW(ConstBufferPool);
	}
	void ConstBufferPool::ShutDown()
	{
        if (s_ConstBufferPool)
        {
            s_ConstBufferPool->_buffer_pool.clear();
            AL_DELETE(s_ConstBufferPool);
		}
	}
	ConstantBuffer *ConstBufferPool::Acquire(u32 size)
	{
		size = (u32)AlignTo(size, 256u);
		u64 cur_frame = GraphicsContext::Get().GetFrameCount();
		for (auto it = s_ConstBufferPool->_buffer_pool.lower_bound(size); it != s_ConstBufferPool->_buffer_pool.end(); it++)
		{
			auto &[buffer, frame_count] = it->second;
			if (cur_frame - frame_count > 2)
			{
				//buffer->Reset();
				frame_count = cur_frame;
				return buffer.get();
			}
		}
		auto buffer = ConstantBuffer::Create(size);
		s_ConstBufferPool->_buffer_pool.emplace(size, ConstbufferNode{buffer, cur_frame});
		if (s_ConstBufferPool->_buffer_pool.size()> 100u)
			LOG_INFO("[ConstBufferPool::Acquire]: Create a new constant buffer pool: {}", s_ConstBufferPool->_buffer_pool.size());
		return buffer.get();
	}
	#pragma endregion
}// namespace Ailu::Render
