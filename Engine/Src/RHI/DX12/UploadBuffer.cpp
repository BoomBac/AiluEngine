#include "RHI/DX12/UploadBuffer.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "pch.h"
using namespace Ailu::Render;

namespace Ailu::RHI::DX12
{
    namespace
    {
        inline void NameAndLogUploadResource(ID3D12Resource* resource, const String& name)
        {
            if (resource == nullptr)
                return;
            SetName(resource, ToWChar(name).c_str());
            //LOG_INFO("D3D12 resource created: name={}, ptr={}", name, static_cast<const void*>(resource));
        }
    }

#pragma region UploadBuffer
    UploadBuffer::UploadBuffer(const String &name, u64 page_size) : m_PageSize(page_size)
    {
        _name = name;
        _is_ready_for_rendering = true;
    }

    UploadBuffer::Allocation UploadBuffer::Allocate(size_t sizeInBytes, size_t alignment)
    {
        if (sizeInBytes > m_PageSize)
        {
            auto large_page = CreateLargePage(sizeInBytes, alignment);
            Allocation allocation{};
            AL_ASSERT(large_page->Allocate(sizeInBytes, alignment, allocation));
            return allocation;
        }

        for (;;)
        {
            auto current_page = m_CurrentPage.load(std::memory_order_acquire);
            if (!current_page || !current_page->HasSpace(sizeInBytes, alignment))
            {
                current_page = RequestPage();
                m_CurrentPage.store(current_page, std::memory_order_release);
            }

            Allocation allocation{};
            if (current_page->Allocate(sizeInBytes, alignment, allocation))
                return allocation;

            std::shared_ptr<Page> expected_page = current_page;
            m_CurrentPage.compare_exchange_strong(expected_page, std::shared_ptr<Page>{},
                                                  std::memory_order_acq_rel, std::memory_order_acquire);
        }
    }
    std::shared_ptr<UploadBuffer::Page> UploadBuffer::RequestPage()
    {
        std::lock_guard lock(m_PagePoolMutex);
        std::shared_ptr<Page> page;

        if (!m_AvailablePages.empty())
        {
            page = m_AvailablePages.front();
            m_AvailablePages.pop_front();
        }
        else
        {
            page = std::make_shared<Page>(_name, m_PageSize);
            m_PagePool.push_back(page);
        }

        return page;
    }
    std::shared_ptr<UploadBuffer::Page> UploadBuffer::CreateLargePage(size_t sizeInBytes, size_t alignment)
    {
        std::lock_guard lock(m_PagePoolMutex);
        const size_t page_size = AlignTo(sizeInBytes, alignment);
        auto page = std::make_shared<Page>(std::format("{}_Large_{}", _name, m_LargePagePool.size()), page_size);
        m_LargePagePool.push_back(page);
        return page;
    }
    void UploadBuffer::Reset()
    {
        std::lock_guard lock(m_PagePoolMutex);
        m_CurrentPage.store(std::shared_ptr<Page>{}, std::memory_order_release);
        // Reset all available pages.
        m_AvailablePages = m_PagePool;
        // 大页的GPU资源只能延迟一个周期释放：Reset() 虽然在该槽位fence等待之后调用，
        // 但调用后当前帧立刻会重新提交引用新大页的命令，直接析构会让仍在执行中的
        // ID3D12Resource 被 final-release（D3D12 OBJECT_DELETED_WHILE_STILL_IN_USE）。
        m_RetirePages.clear();
        m_RetirePages.swap(m_LargePagePool);

        for (auto page: m_AvailablePages)
        {
            // Reset the page for new allocations.
            page->Reset();
        }
    }
    void UploadBuffer::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams& params)
    {
        if (&params == nullptr)
            return;
        GpuResource::BindImpl(rhi_cmd, params);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        auto dxcmd = d3dcmd->NativeCmdList();
        if (params._is_compute_pipeline)
            dxcmd->SetComputeRootConstantBufferView(params._slot, params._params._ub_binder._gpu_ptr);
        else
            d3dcmd->SetGraphicsRootConstantBufferView(params._slot, params._params._ub_binder._gpu_ptr);
    }
    UploadBuffer::Page::Page(const String &name, size_t sizeInBytes)
        : m_PageSize(sizeInBytes), m_Offset(0), m_CPUPtr(nullptr), m_GPUPtr(D3D12_GPU_VIRTUAL_ADDRESS(0))
    {

        auto device = static_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto buffer = CD3DX12_RESOURCE_DESC::Buffer(m_PageSize);
        ThrowIfFailed(device->CreateCommittedResource(
                &heap_prop,
                D3D12_HEAP_FLAG_NONE,
                &buffer,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_d3d12Resource)));

        m_GPUPtr = m_d3d12Resource->GetGPUVirtualAddress();
        m_d3d12Resource->Map(0, nullptr, &m_CPUPtr);
        NameAndLogUploadResource(m_d3d12Resource.Get(), name);
    }
    UploadBuffer::Page::~Page()
    {
        m_d3d12Resource->Unmap(0, nullptr);
        m_CPUPtr = nullptr;
        m_GPUPtr = D3D12_GPU_VIRTUAL_ADDRESS(0);
    }
    bool UploadBuffer::Page::HasSpace(size_t sizeInBytes, size_t alignment) const
    {
        size_t alignedSize = AlignTo(sizeInBytes, alignment);
        size_t alignedOffset = AlignTo(m_Offset.load(std::memory_order_acquire), alignment);

        return alignedOffset + alignedSize <= m_PageSize;
    }
    bool UploadBuffer::Page::Allocate(size_t size_in_bytes, size_t alignment, Allocation &allocation)
    {
        if (!HasSpace(size_in_bytes, alignment))
        {
            return false;
        }

        size_t alignedSize = AlignTo(size_in_bytes, alignment);
        size_t alignedOffset = m_Offset.load(std::memory_order_relaxed);
        constexpr u32 kMaxCompareExchangeRetries = 64u;
        bool allocated = false;
        for (u32 retry = 0u; retry < kMaxCompareExchangeRetries; ++retry)
        {
            alignedOffset = AlignTo(alignedOffset, alignment);
            if (alignedOffset + alignedSize > m_PageSize)
                return false;
            if (m_Offset.compare_exchange_strong(alignedOffset, alignedOffset + alignedSize,
                                                 std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                allocated = true;
                break;
            }
        }

        if (!allocated)
            return false;

        allocation.CPU = static_cast<u8 *>(m_CPUPtr) + alignedOffset;
        allocation.GPU = m_GPUPtr + alignedOffset;
        allocation._size = alignedSize;
        allocation._offset = (u32)alignedOffset;
        allocation._page_res = m_d3d12Resource.Get();
        return true;
    }
    void UploadBuffer::Page::Reset()
    {
        m_Offset.store(0u, std::memory_order_release);
    }
#pragma endregion

#pragma region ReadbackBufferPool
    ReadbackBufferPool::ReadbackBufferPool(ID3D12Device *device) : _device(device) ,_total_used(0u){}

    ComPtr<ID3D12Resource> ReadbackBufferPool::Acquire(u64 size, u64 current_frame)
    {
        std::unique_lock lock(_mutex);
        size = AlignTo(size, size);
        for (auto &buf: _buffers)
        {
            if (buf._size >= size && (current_frame - buf._last_used_frame) > kMaxStaleFrameCount)
            {
                buf._last_used_frame = current_frame;
                return buf._resource;
            }
        }
        CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_READBACK);
        CD3DX12_RESOURCE_DESC res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ComPtr<ID3D12Resource> resource;
        _device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         IID_PPV_ARGS(resource.GetAddressOf()));
        NameAndLogUploadResource(resource.Get(), std::format("ReadbackBuffer_{}", _buffers.size()));
        _total_used += size;
        _buffers.push_back({resource, size, current_frame});
        return resource;
    }

    void ReadbackBufferPool::Tick(u64 current_frame)
    {
        if (_total_used > kMaxStaleBufferSize)
        {
            u64 old_size = _buffers.size(),old_data_size = _total_used;
            std::erase_if(_buffers, [this,current_frame](const Buffer &buf) {
                bool should_release = (current_frame - buf._last_used_frame) > kMaxStaleFrameCount;
                if (should_release)
                    _total_used -= buf._size;
                return should_release;
            });
            LOG_WARNING("ReadbackBufferPool::Tick: Release {} readback buffer with {} byte.", old_size - _buffers.size(),old_data_size - _total_used);
        }
    }
#pragma endregion
}// namespace Ailu
