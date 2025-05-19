//
// Created by 22292 on 2024/10/27.
//

#ifndef AILU_GPURESOURCEMANAGER_H
#define AILU_GPURESOURCEMANAGER_H
#include "GlobalMarco.h"
#include <d3dx12.h>
#include "Page.h"
using Microsoft::WRL::ComPtr;

namespace Ailu::RHI::DX12
{

    class GPUResourcePage : public Page
    {
        friend class GpuResourceManager;
    public:
        DISALLOW_COPY_AND_ASSIGN(GPUResourcePage)
        GPUResourcePage(u16 id, D3D12_HEAP_TYPE type, u32 size);
        GPUResourcePage(GPUResourcePage &&other) noexcept;
    private:
        inline static u32 s_global_id = 0u;
        ComPtr<ID3D12Resource> _res;
        D3D12_HEAP_TYPE _type;
        u8* _ptr_cpu;
        D3D12_GPU_VIRTUAL_ADDRESS _ptr_gpu;
    };

    class GpuResourceManager
    {
    public:
        struct Allocation
        {
            Allocation() = default;
            Allocation(u16 page_id, u64 offset, u64 size): _page_id(page_id), _offset(offset), _size(size) {}
            u16 _page_id = (u16)-1;
            u64 _offset = 0u;
            u64 _size = 0u;
            void *_cpu_ptr = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS _gpu_ptr = 0u;
        };
    public:
        inline static u64 kPerPageSize = 2097152u; //2MB
        static void Init();
        static void Shutdown();
        static GpuResourceManager* Get();
    public:
        Allocation Allocate(u32 size = 1024u,D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_UPLOAD);
        void Free(Allocation&& handle);
        u32 ReleaseSpace();
    private:
        std::multimap<u32,u16>::iterator AddNewPage(D3D12_HEAP_TYPE type,u32 size = kPerPageSize);
    private:
        Vector<GPUResourcePage> _pages;
        Map<D3D12_HEAP_TYPE ,std::multimap<u32,u16>> _page_free_space_lut;

    };
}

#endif//AILU_GPURESOURCEMANAGER_H
