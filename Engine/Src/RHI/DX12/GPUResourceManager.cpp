//
// Created by 22292 on 2024/10/27.
//
#include "RHI/DX12/GPUResourceManager.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include <Framework/Common/Application.h>
#include <Framework/Common/Log.h>

using namespace Ailu::Render;

namespace Ailu::RHI::DX12
{
    GPUResourcePage::GPUResourcePage(u16 id, D3D12_HEAP_TYPE type, u32 size) : Page(id,size),_type(type)
    {
        auto heap_prop = CD3DX12_HEAP_PROPERTIES(type);
        if (type == D3D12_HEAP_TYPE_UPLOAD)
        {
            auto& d3d_context = static_cast<D3DContext&>(GraphicsContext::Get());
            size = ALIGN_TO_256(size);
            _size = size;
            auto res_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            ThrowIfFailed(d3d_context.GetDevice()->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &res_desc,
                                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(_res.GetAddressOf())));
            _res->Map(0, nullptr, reinterpret_cast<void **>(&_ptr_cpu));
            _ptr_gpu = _res->GetGPUVirtualAddress();
            _res->SetName(std::format(L"UploadPage_{}",s_global_id++).c_str());
        }
        else
        {
            LOG_WARNING("GPUResourcePage::GPUResourcePage: type is not D3D12_HEAP_TYPE_UPLOAD");
        }
    }
    GPUResourcePage::GPUResourcePage(GPUResourcePage &&other) noexcept : Page(std::move(other))
    {
       other._res.Swap( _res );
       _ptr_cpu = other._ptr_cpu;
       _ptr_gpu = other._ptr_gpu;
       _type = other._type;
       other._ptr_gpu = 0u;
       other._ptr_cpu = nullptr;
       other._res = nullptr;
    }
    //-----------------------------------------------------------------------------------------------------------------------------------------------
    static GpuResourceManager* s_GpuResourceManager = nullptr;
    void GpuResourceManager::Init()
    {
        s_GpuResourceManager = new GpuResourceManager();
    }
    void GpuResourceManager::Shutdown()
    {
        DESTORY_PTR(s_GpuResourceManager);
    }
    GpuResourceManager *GpuResourceManager::Get()
    {
        return s_GpuResourceManager;
    }
    //-------------------------------------------------------------------------------------------------------------------------------------------------
    GpuResourceManager::Allocation GpuResourceManager::Allocate(u32 size, D3D12_HEAP_TYPE type)
    {
        static bool s_first_alloc = true;
        if (s_first_alloc)
        {
            //防止vec扩容后，原有的allocation中存储的page指针失效
            _pages.reserve(10);
            s_first_alloc = false;
        }
        if (!_page_free_space_lut.contains(type))
        {
            _page_free_space_lut[type] = std::multimap<u32, u16>{};
        }
        auto page_it = _page_free_space_lut[type].lower_bound(size);
        if (page_it == _page_free_space_lut[type].end())
        {
            page_it = AddNewPage(type);
        }
        auto& page = _pages[page_it->second];
        i64 offset = page.Allocate(size);
        if (offset != -1)
        {
            _page_free_space_lut[type].erase(page_it);
            _page_free_space_lut[type].emplace(std::make_pair((u32)page.AvailableSize(),page.PageID()));
            if (type == D3D12_HEAP_TYPE_UPLOAD)
            {
                Allocation allocation(page._id, offset, size);
                allocation._cpu_ptr = reinterpret_cast<u8 *>(page._ptr_cpu) + offset;
                allocation._gpu_ptr = page._ptr_gpu + offset;
                return allocation;
            }
        }
        else
        {
            LOG_ERROR("GpuResourceManager::Allocate: allocate failed");
        }
        return Allocation(-1,0u,0u);
    }
    void GpuResourceManager::Free(GpuResourceManager::Allocation &&handle)
    {
        AL_ASSERT(handle._page_id < _pages.size());
        _pages[handle._page_id].Free(handle._offset, handle._size,Application::Application::Get().GetFrameCount());
    }
    u32 GpuResourceManager::ReleaseSpace()
    {
        _page_free_space_lut.clear();
        u32 release_num = 0u;
        for (auto& page : _pages)
        {
            release_num += page.ReleaseAllStaleBlock(Application::Application::Get().GetFrameCount());
            _page_free_space_lut[page._type].emplace(std::make_pair((u32)page.AvailableSize(), page.PageID()));
        }
        return release_num;
    }
    std::multimap<u32, u16>::iterator GpuResourceManager::AddNewPage(D3D12_HEAP_TYPE type, u32 size)
    {
        GPUResourcePage new_page(static_cast<u16>(_pages.size()), type, size);
        _pages.emplace_back(std::move(new_page));
        return _page_free_space_lut[type].emplace(std::make_pair(size, static_cast<u16>(_pages.size() - 1)));
    }

}