#ifndef __UOLOAD_BUFFER__
#define __UOLOAD_BUFFER_
#pragma once
#include "GlobalMarco.h"
#include <deque>
#include <memory>
#include <mutex>
#include <wrl.h>

#include "Ext/d3dx12.h"
#include "Render/GpuResource.h"

namespace Ailu::RHI::DX12
{
    using Microsoft::WRL::ComPtr;

#define _2MB 2097152u
    class UploadBuffer : public Render::GpuResource
    {
    public:
        // Use to upload data to the GPU
        struct Allocation
        {
            void *CPU;
            D3D12_GPU_VIRTUAL_ADDRESS GPU;
            u64 _size;
            u32 _offset;
            ID3D12Resource *_page_res;
            void SetData(const void *data, u64 data_size)
            {
                memcpy(CPU, data, data_size);
            }
        };
        explicit UploadBuffer(const String &name, u64 page_size = _2MB);
        u64 GetPageSize() const { return m_PageSize; }
        Allocation Allocate(size_t sizeInBytes, size_t alignment);
        void Reset();
        void BindImpl(Render::RHICommandBuffer *rhi_cmd, const Render::BindParams& params) final;

    private:
        struct Page
        {
        public:
            Page(const String &name, u64 byte_size);
            ~Page();
            bool HasSpace(u64 byte_size, u64 alignment) const;
            Allocation Allocate(size_t sizeInBytes, size_t alignment);
            void Reset();

        private:
            ComPtr<ID3D12Resource> m_d3d12Resource;
            void *m_CPUPtr;
            D3D12_GPU_VIRTUAL_ADDRESS m_GPUPtr;
            size_t m_PageSize;
            size_t m_Offset;
        };
        using PagePool = std::deque<std::shared_ptr<Page>>;
        // Request a page from the pool of available pages
        // or create a new page if there are no available pages.
        std::shared_ptr<Page> RequestPage();
        PagePool m_PagePool;
        PagePool m_AvailablePages;
        std::shared_ptr<Page> m_CurrentPage;
        // The size of each page of memory.
        size_t m_PageSize;
    };

    class ReadbackBufferPool
    {
    public:
        ReadbackBufferPool(ID3D12Device *device);
        ComPtr<ID3D12Resource> Acquire(u64 size, u64 current_frame);
        void Tick(u64 current_frame);

    private:
        //16 mb
        static constexpr u64 kMaxStaleBufferSize = 1024 * 1024 * 16;
        struct Buffer
        {
            ComPtr<ID3D12Resource> _resource;
            u64 _size;
            u64 _last_used_frame;
        };
        ID3D12Device *_device;
        Vector<Buffer> _buffers;
        u64 _total_used;
        std::mutex _mutex;
    };
}// namespace Ailu::RHI::DX12
#endif