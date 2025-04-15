#include "RHI/DX12/D3DTexture.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/dxhelper.h"
#include "pch.h"

namespace Ailu
{
    static DXGI_FORMAT GetCompatibleSRVFormat(DXGI_FORMAT dx_format)
    {
        if (dx_format == DXGI_FORMAT_D24_UNORM_S8_UINT)
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        else if (dx_format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        else if (dx_format == DXGI_FORMAT_D32_FLOAT)
            return DXGI_FORMAT_R32_FLOAT;
        else if (dx_format == DXGI_FORMAT_R16_FLOAT)
            return DXGI_FORMAT_R16_FLOAT;
        else
            return dx_format;
    }
    static u16 GetTextureSizePower2Info(const std::tuple<u16,u16>& size)
    {
        auto [w, h] = size;
        if (w % 2 == 0 && h % 2 == 0)
            return 0;
        else if ( w % 2 != 0 && h % 2 == 0)
            return 1;
        else if (w % 2 == 0 && h % 2 != 0)
            return 2;
        else
            return 3;
    }
    #pragma region D3DTexture2D
    //----------------------------------------------------------------------------D3DTexture2DNew-----------------------------------------------------------------------
    D3DTexture2D::D3DTexture2D(const Texture2DInitializer& initializer) : Texture2D(initializer)
    {
    }

    D3DTexture2D::~D3DTexture2D()
    {
        Release();
    }

    void D3DTexture2D::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        auto d3d_ctx = dynamic_cast<D3DContext*>(ctx);
        auto p_device = d3d_ctx->GetDevice();
        auto p_cmdlist = static_cast<D3DCommandBuffer *>(rhi_cmd)->NativeCmdList();
        //ComPtr<ID3D12Resource> pTextureGPU;

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.MipLevels = _mipmap_count;
        textureDesc.Format = ConvertToDXGIFormat(_pixel_format);
        textureDesc.Width = _width;
        textureDesc.Height = _height;
        textureDesc.Flags = _is_random_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        bool is_data_filed = false;
        for (const auto &fill: _is_data_filled)
        {
            if (fill)
            {
                is_data_filed = true;
                break;
            }
        }
        CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
        _state_guard = D3DResourceStateGuard(is_data_filed ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON);
        //_state_guard = D3DResourceStateGuard(D3D12_RESOURCE_STATE_COPY_DEST);
        ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, _state_guard.CurState(), nullptr, IID_PPV_ARGS(_p_d3dres.GetAddressOf())));
        if (is_data_filed)
        {
            ComPtr<ID3D12Resource> pTextureUpload;
            const u32 subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
            const UINT64 uploadBufferSize = GetRequiredIntermediateSize(_p_d3dres.Get(), 0, subresourceCount);
            heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
            auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
            ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                            nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));

            Vector<D3D12_SUBRESOURCE_DATA> subres_datas = {};
            for (size_t i = 0; i < _mipmap_count; i++)
            {
                u16 cur_mip_width = _width >> i;
                u16 cur_mip_height = _height >> i;
                D3D12_SUBRESOURCE_DATA subdata;
                subdata.pData = _pixel_data[i];
                subdata.RowPitch = _pixel_size * cur_mip_width;// pixel size/byte  * width
                subdata.SlicePitch = subdata.RowPitch * cur_mip_height;
                subres_datas.emplace_back(subdata);
            }
            UpdateSubresources(p_cmdlist, _p_d3dres.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, subres_datas.data());
            _state_guard.MakesureResourceState(p_cmdlist, _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            d3d_ctx->TrackResource(pTextureUpload);
        }
        {
            D3DTextureViewInfo view_info(ETextureViewType::kSRV, false);
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = _mipmap_count;
            srv_desc.Texture2D.MostDetailedMip = 0;
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[kMainSRVIndex] = std::move(view_info);
        }
        CreateView(ETextureViewType::kSRV, 0);
        if (_is_random_access)
            CreateView(ETextureViewType::kUAV, 0);
        _p_d3dres->SetName(ToWStr(_name).c_str());
        _is_ready_for_rendering = true;
        Texture2D::CreateView();
    }

    void D3DTexture2D::Release()
    {
        Texture2D::Release();
        if (g_pGfxContext)
            g_pGfxContext->WaitForFence(_fence_value);
        _p_d3dres.Reset();
        _views.clear();
    }

    void D3DTexture2D::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        if (auto bind_param = dynamic_cast<BindParamsTexture*>(params); bind_param != nullptr)
        {
            auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
            if (_views.contains(bind_param->_view_idx))
            {
                GpuResource::BindImpl(rhi_cmd, params);
                auto view_type = _views[bind_param->_view_idx]._view_type;
                if (view_type == ETextureViewType::kSRV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, bind_param->_sub_res);
                    if (params->_is_compute_pipeline)
                        _views[bind_param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, bind_param->_slot);
                    else
                        _views[bind_param->_view_idx]._gpu_alloc.CommitDescriptorsForDraw(d3dcmd, bind_param->_slot);
                }
                else if (view_type == ETextureViewType::kUAV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, bind_param->_sub_res);
                    _views[bind_param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, bind_param->_slot);
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
        }
    }

    void D3DTexture2D::CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice)
    {
        if (!_p_d3dres)
        {
            LOG_ERROR("Can't create view before it been applied![Texture2d: {}]", _name);
            return;
        }
        if (view_type == kDSV || view_type == kRTV)
        {
            LOG_WARNING("Try to create dsv/rtv on a raw texture2d,call is on render texture instead!");
            return;
        }
        u16 view_index = CalculateViewIndex(view_type, mipmap, array_slice);
        if (_views.contains(view_index) || view_index == kMainSRVIndex)
            return;
        D3DTextureViewInfo view_info(view_type, false);
        auto p_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
        auto [cpu_handle, gpu_handle] = alloc.At(0);
        if (view_type == ETextureViewType::kSRV)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            srv_desc.Texture2D.MostDetailedMip = mipmap;
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, cpu_handle);
        }
        else if (view_type == kUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC slice_uav_desc{};
            slice_uav_desc.Format = ConvertToDXGIFormat(_pixel_format);
            slice_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            slice_uav_desc.Texture2D.MipSlice = 0;
            p_device->CreateUnorderedAccessView(_p_d3dres.Get(), nullptr, &slice_uav_desc, cpu_handle);
        }
        else
        {
            AL_ASSERT(false);
        }
        view_info._gpu_handle = gpu_handle;
        view_info._gpu_alloc = std::move(alloc);
        _views[view_index] = std::move(view_info);
    }

    TextureHandle D3DTexture2D::GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice) const
    {
        u16 idx = CalculateViewIndex(view_type, mipmap, array_slice);
        if (_views.contains(idx))
        {
            return D3DDescriptorMgr::Get().GetBindGpuHandle(_views.at(idx)._gpu_alloc).ptr;
        }
        else
            return 0;
    }

    void D3DTexture2D::ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice)
    {
        u16 idx = CalculateViewIndex(view_type, mipmap, array_slice);
        _views.erase(idx);
    }

    void D3DTexture2D::Name(const String &new_name)
    {
        _name = new_name;
        if (_p_d3dres)
            _p_d3dres->SetName(ToWStr(new_name).c_str());
    }

    void D3DTexture2D::GenerateMipmap()
    {
        auto mipmap_gen = g_pResourceMgr->GetRef<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
        auto kernel = mipmap_gen->FindKernel("MipmapGen2D");
        auto cmd = CommandBufferPool::Get("MipmapGen2D");
        mipmap_gen->SetInt("SrcMipLevel", 0);
        mipmap_gen->SetInt("NumMipLevels", 4);
        mipmap_gen->SetBool("IsSRGB", false);
        auto [mip1w, mip1h] = CalculateMipSize(_width, _height, 1);
        mipmap_gen->SetInt("SrcDimension", GetTextureSizePower2Info(CalculateMipSize(_width, _height, 0)));
        mipmap_gen->SetVector("TexelSize", Vector4f(1.0f / (float) mip1w, 1.0f / (float) mip1h, 0.0f, 0.0f));
        mipmap_gen->SetTexture("SrcMip", this,  ECubemapFace::kUnknown, 0);
        mipmap_gen->SetTexture("OutMip1", this, ECubemapFace::kUnknown, 1);
        mipmap_gen->SetTexture("OutMip2", this, ECubemapFace::kUnknown, 2);
        mipmap_gen->SetTexture("OutMip3", this, ECubemapFace::kUnknown, 3);
        mipmap_gen->SetTexture("OutMip4", this, ECubemapFace::kUnknown, 4);
        //static_cast<D3DComputeShader*>(_p_mipmapgen_cs0.get())->BindImpl(cmd, 32, 32, 1);
        //保证线程数和第一级输出的mipmap像素数一一对应
        cmd->Dispatch(mipmap_gen.get(), kernel, mip1w / 8, mip1h / 8, 1);
        if (_mipmap_count > 5)
        {
            auto [mip5w, mip5h] = CalculateMipSize(_width, _height, 5);
            mipmap_gen->SetInt("SrcMipLevel", 4);
            mipmap_gen->SetInt("NumMipLevels", std::min<u16>(_mipmap_count - 5, 4));
            mipmap_gen->SetInt("SrcDimension", GetTextureSizePower2Info(CalculateMipSize(_width, _height, 4)));
            mipmap_gen->SetBool("IsSRGB", false);
            mipmap_gen->SetVector("TexelSize", Vector4f(1.0f / (float) mip5w, 1.0f / (float) mip5h, 0.0f, 0.0f));
            mipmap_gen->SetTexture("SrcMip", this,  ECubemapFace::kUnknown, 4);
            mipmap_gen->SetTexture("OutMip1", this, ECubemapFace::kUnknown, 5);
            mipmap_gen->SetTexture("OutMip2", this, ECubemapFace::kUnknown, 6);
            mipmap_gen->SetTexture("OutMip3", this, ECubemapFace::kUnknown, 7);
            mipmap_gen->SetTexture("OutMip4", this, ECubemapFace::kUnknown, 8);
            cmd->Dispatch(mipmap_gen.get(), kernel, std::max(mip5w / 8,1), std::max(mip5h / 8,1), 1);
        }
        if (_mipmap_count > 9)
        {
            auto [mip9w, mip9h] = CalculateMipSize(_width, _height, 9);
            mipmap_gen->SetInt("SrcMipLevel", 8);
            mipmap_gen->SetInt("NumMipLevels", std::min<u16>(_mipmap_count - 9, 4));
            mipmap_gen->SetInt("SrcDimension", GetTextureSizePower2Info(CalculateMipSize(_width, _height, 8)));
            mipmap_gen->SetBool("IsSRGB", false);
            mipmap_gen->SetVector("TexelSize", Vector4f(1.0f / (float) mip9w, 1.0f / (float) mip9h, 0.0f, 0.0f));
            mipmap_gen->SetTexture("SrcMip", this, ECubemapFace::kUnknown, 8);
            mipmap_gen->SetTexture("OutMip1", this, ECubemapFace::kUnknown, 9);
            mipmap_gen->SetTexture("OutMip2", this, ECubemapFace::kUnknown, 10);
            mipmap_gen->SetTexture("OutMip3", this, ECubemapFace::kUnknown, 11);
            mipmap_gen->SetTexture("OutMip4", this, ECubemapFace::kUnknown, 12);
            cmd->Dispatch(mipmap_gen.get(), kernel, std::max(mip9w / 8,1), std::max(mip9h / 8,1) ,1);
        }
        cmd->StateTransition(this,D3DConvertUtils::ToALResState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    #pragma endregion
    //----------------------------------------------------------------------------D3DTexture2DNew-----------------------------------------------------------------------
    
    #pragma region D3DCubeMap
    //----------------------------------------------------------------------------D3DCubeMap----------------------------------------------------------------------------
    D3DCubeMap::D3DCubeMap(u16 width, bool mipmap_chain, ETextureFormat::ETextureFormat format, bool linear, bool random_access)
        : CubeMap(width, mipmap_chain, format, linear, random_access)
    {
    }

    D3DCubeMap::~D3DCubeMap()
    {
        g_pGfxContext->WaitForFence(_fence_value);
    }

    void D3DCubeMap::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        auto d3d_ctx = dynamic_cast<D3DContext*>(ctx);
        auto p_device = d3d_ctx->GetDevice();
        auto p_cmdlist = dynamic_cast<D3DCommandBuffer *>(rhi_cmd)->NativeCmdList();
        //ComPtr<ID3D12Resource> pTextureGPU;
        ComPtr<ID3D12Resource> pTextureUpload;
        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.MipLevels = _mipmap_count;
        textureDesc.Format = ConvertToDXGIFormat(_pixel_format);
        textureDesc.Width = _width;
        textureDesc.Height = _width;
        textureDesc.Flags = _is_random_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 6;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
        _state_guard = D3DResourceStateGuard(_is_random_access ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_DEST);
        ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, _state_guard.CurState(),
                                                        nullptr, IID_PPV_ARGS(_p_d3dres.GetAddressOf())));

        const u32 subresourceCount = textureDesc.DepthOrArraySize * textureDesc.MipLevels;
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(_p_d3dres.Get(), 0, subresourceCount);
        heap_prop.Type = D3D12_HEAP_TYPE_UPLOAD;
        auto upload_buf_desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &upload_buf_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(pTextureUpload.GetAddressOf())));
        Vector<D3D12_SUBRESOURCE_DATA> cubemap_datas;
        for (int j = 0; j < 6; ++j)
        {
            for (int i = 0; i < textureDesc.MipLevels; ++i)
            {
                u16 cur_mip_width = _width >> i;
                D3D12_SUBRESOURCE_DATA subdata;
                subdata.pData = _pixel_data[j * textureDesc.MipLevels + i];
                subdata.RowPitch = _pixel_size * cur_mip_width;// pixel size/byte  * width
                subdata.SlicePitch = subdata.RowPitch * cur_mip_width;
                cubemap_datas.emplace_back(subdata);
            }
        }

        UpdateSubresources(p_cmdlist, _p_d3dres.Get(), pTextureUpload.Get(), 0, 0, subresourceCount, cubemap_datas.data());
        _state_guard.MakesureResourceState(p_cmdlist, _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d3d_ctx->TrackResource(pTextureUpload);
        //Main srv
        {
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            D3DTextureViewInfo view_info(ETextureViewType::kSRV, false);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
            view_desc.Format = GetCompatibleSRVFormat(textureDesc.Format);
            view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (_dimension == ETextureDimension::kCube)
            {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                view_desc.TextureCube.MipLevels = _mipmap_count;
                view_desc.TextureCube.MostDetailedMip = 0;
            }
            else
                AL_ASSERT(false);
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &view_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[kMainSRVIndex] = std::move(view_info);
        }
        for (u16 face = 1; face <= 6; face++)
        {
            if (_is_random_access)
                CreateView(ETextureViewType::kUAV, (ECubemapFace::ECubemapFace) face, 0);
            else
                CreateView(ETextureViewType::kSRV, (ECubemapFace::ECubemapFace) face, 0);
        }
        _p_d3dres->SetName(ToWStr(_name).c_str());
        CubeMap::CreateView();
    }

    void D3DCubeMap::CreateView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice)
    {
        if (!_p_d3dres)
        {
            LOG_ERROR("Can't create view before it been applied![Cubemap: {}]", _name);
            return;
        }
        if (view_type == kDSV || view_type == kRTV)
        {
            LOG_WARNING("Try to create dsv/rtv on a raw cubemap,call is on render texture instead!");
            return;
        }
        u16 view_index = CalculateViewIndex(view_type, mipmap, array_slice);
        if (_views.contains(view_index))
            return;
        D3DTextureViewInfo view_info(view_type, false);
        auto p_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
        auto [cpu_handle, gpu_handle] = alloc.At(0);
        if (view_type == ETextureViewType::kSRV)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv_desc.Texture2DArray.ArraySize = 1;
            srv_desc.Texture2DArray.FirstArraySlice = ((u16) face - 1) * array_slice;
            srv_desc.Texture2DArray.MipLevels = 1;
            srv_desc.Texture2DArray.MostDetailedMip = mipmap;

            p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, cpu_handle);
        }
        else if (view_type == kUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC slice_uav_desc{};
            slice_uav_desc.Format = ConvertToDXGIFormat(_pixel_format);
            slice_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            slice_uav_desc.Texture2DArray.ArraySize = 1;
            slice_uav_desc.Texture2DArray.FirstArraySlice = ((u16) face - 1) * array_slice;
            slice_uav_desc.Texture2DArray.MipSlice = mipmap;
            p_device->CreateUnorderedAccessView(_p_d3dres.Get(), nullptr, &slice_uav_desc, cpu_handle);
        }
        else
        {
            AL_ASSERT(false);
        }
        view_info._gpu_handle = gpu_handle;
        view_info._gpu_alloc = std::move(alloc);
        _views[view_index] = std::move(view_info);
    }

    TextureHandle D3DCubeMap::GetView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice) const
    {
        u16 idx = CalculateViewIndex(view_type, mipmap, array_slice);
        if (_views.contains(idx))
        {
            return D3DDescriptorMgr::Get().GetBindGpuHandle(_views.at(idx)._gpu_alloc).ptr;
        }
        else
            return 0;
    }

    void D3DCubeMap::ReleaseView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice)
    {
        u16 idx = CalculateViewIndex(view_type, face, mipmap, array_slice);
        _views.erase(idx);
    }

    void D3DCubeMap::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        if (auto param = dynamic_cast<BindParamsTexture*>(params);param != nullptr)
        {
            if (_views.contains(param->_view_idx))
            {
                GpuResource::BindImpl(rhi_cmd, params);
                auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
                auto view_type = _views[param->_view_idx]._view_type;
                if (view_type == ETextureViewType::kSRV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, param->_sub_res);
                    if (param->_is_compute_pipeline)
                        _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, param->_slot);
                    else
                        _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDraw(d3dcmd, param->_slot);
                }
                else if (view_type == ETextureViewType::kUAV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, param->_sub_res);
                    _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, param->_slot);
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
        }
    }
    #pragma endregion
    //----------------------------------------------------------------------------D3DCubeMap-----------------------------------------------------------------------------

    #pragma region D3DTexture3D
    //----------------------------------------------------------------------------D3D3DTexture-----------------------------------------------------------------------------
    D3DTexture3D::D3DTexture3D(const Texture3DInitializer &initializer) : Texture3D(initializer)
    {
        if (_p_mipmapgen_cs0 == nullptr)
        {
            _p_mipmapgen_cs0 = g_pResourceMgr->GetRef<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
            _p_mipmapgen_cs1 = g_pResourceMgr->GetRef<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
        }
    }
    D3DTexture3D::~D3DTexture3D()
    {
        g_pGfxContext->WaitForFence(_fence_value);
    }
    void D3DTexture3D::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params)
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        auto d3d_ctx = dynamic_cast<D3DContext*>(ctx);
        auto p_device = d3d_ctx->GetDevice();
        auto p_cmdlist = dynamic_cast<D3DCommandBuffer*>(rhi_cmd)->NativeCmdList();
        //ComPtr<ID3D12Resource> pTextureGPU;

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.MipLevels = _mipmap_count;
        textureDesc.Format = ConvertToDXGIFormat(_pixel_format);
        textureDesc.Width = _width;
        textureDesc.Height = _height;
        textureDesc.DepthOrArraySize = _depth;
        textureDesc.Flags = _is_random_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        bool is_data_filed = false;
        if (_is_data_filled[0])
        {
            for (u16 i = 1; i < _mipmap_count; i++)
            {
                if (!_is_data_filled[i])
                {
                    // auto [x, y, z] = Texture::CalculateMipSize(_width, _height, _depth, i);
                    // u32 size = x * y * z * _pixel_size;
                    // _pixel_data[i] = new u8[size];
                    // memset(_pixel_data[i], 0, size);
                    is_data_filed = true;
                    _is_data_filled[i] = true;
                }
            }
        }
        CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
        _state_guard = D3DResourceStateGuard(is_data_filed ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON);
        //_state_guard = D3DResourceStateGuard(D3D12_RESOURCE_STATE_COPY_DEST);
        ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &textureDesc, _state_guard.CurState(), nullptr, IID_PPV_ARGS(_p_d3dres.GetAddressOf())));
        if (is_data_filed)
        {
            // 2. 创建上传缓冲区
            UINT64 uploadBufferSize;
            p_device->GetCopyableFootprints(&textureDesc, 0, _mipmap_count, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

            ComPtr<ID3D12Resource> uploadBuffer;
            heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

            CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

            if (FAILED(p_device->CreateCommittedResource(
                        &heap_prop,
                        D3D12_HEAP_FLAG_NONE,
                        &uploadBufferDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&uploadBuffer))))
            {
                throw std::runtime_error("Failed to create upload buffer");
            }
            // 3. 填充上传缓冲区并复制每个 Mipmap 级别
            UINT8 *mappedData;
            D3D12_RANGE readRange = {};// 不需要读取回数据
            uploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&mappedData));

            u8 *dst = mappedData;// 上传缓冲区的写入指针
            u32 currentWidth = _width;
            u32 currentHeight = _height;
            u32 currentDepth = _depth;
            u32 offset = 0u;
            for (u32 mip = 0; mip < _mipmap_count; ++mip)
            {
                // 计算当前 Mipmap 的尺寸
                u64 row_pitch = currentWidth * _pixel_size;
                u64 slice_pitch = row_pitch * currentHeight;
                u32 aligned_row_pitch = ALIGN_TO_256(row_pitch);// 行对齐到 256 字节
                u32 aligned_slice_pitch = aligned_row_pitch * currentHeight;
                //Color32 color[16];
                u32 index = 0;
                // 填充上传缓冲区
                for (u32 z = 0; z < currentDepth; ++z)
                {
                    for (u32 y = 0; y < currentHeight; ++y)
                    {
                        u32 cur_offset = z * aligned_slice_pitch + y * aligned_row_pitch;
                        memcpy(dst + cur_offset,
                               _pixel_data[mip] + z * slice_pitch + y * row_pitch,
                               row_pitch);
                    }
                }
                // 更新 D3D12_PLACED_SUBRESOURCE_FOOTPRINT
                D3D12_TEXTURE_COPY_LOCATION destLocation = {};
                destLocation.pResource = _p_d3dres.Get();
                destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destLocation.SubresourceIndex = mip;

                D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
                srcLocation.pResource = uploadBuffer.Get();
                srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
                footprint.Footprint.Width = currentWidth;
                footprint.Footprint.Height = currentHeight;
                footprint.Footprint.Depth = currentDepth;
                footprint.Footprint.RowPitch = aligned_row_pitch;
                footprint.Footprint.Format = textureDesc.Format;
                srcLocation.PlacedFootprint = footprint;

                p_cmdlist->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);
                dst += aligned_slice_pitch * currentDepth;// 移动到上传缓冲区的下一部分

                // 缩小尺寸以计算下一 Mipmap 级别
                currentWidth = std::max(1u, currentWidth / 2);
                currentHeight = std::max(1u, currentHeight / 2);
                currentDepth = std::max(1u, currentDepth / 2);
            }
            uploadBuffer->Unmap(0, nullptr);
            _state_guard.MakesureResourceState(p_cmdlist, _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            d3d_ctx->TrackResource(uploadBuffer);
        }
        {
            D3DTextureViewInfo view_info(ETextureViewType::kSRV, false);
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv_desc.Texture3D.MipLevels = _mipmap_count;
            srv_desc.Texture3D.MostDetailedMip = 0;
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[kMainSRVIndex] = std::move(view_info);
        }
        CreateView(ETextureViewType::kSRV, 0, UINT16_MAX);
        if (_is_random_access)
            CreateView(ETextureViewType::kUAV, 0, UINT16_MAX);
        _p_d3dres->SetName(ToWStr(_name).c_str());
        _is_ready_for_rendering = true;
        Texture3D::CreateView();
    }
    void D3DTexture3D::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        if (auto param = dynamic_cast<BindParamsTexture*>(params); param != nullptr)
        {
            if (_views.contains(param->_view_idx))
            {
                GpuResource::BindImpl(rhi_cmd, params);
                auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
                auto view_type = _views[param->_view_idx]._view_type;
                if (view_type == ETextureViewType::kSRV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, param->_sub_res);
                    if (param->_is_compute_pipeline)
                        _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, param->_slot);
                    else
                        _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDraw(d3dcmd, param->_slot);
                }
                else if (view_type == ETextureViewType::kUAV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, param->_sub_res);
                    _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, param->_slot);
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
        }
    }
    void D3DTexture3D::CreateView(Texture::ETextureViewType view_type, u16 mipmap, u16 dpeth_slice)
    {
        if (!_p_d3dres)
        {
            LOG_ERROR("Can't create view before it been applied![Texture3d: {}]", _name);
            return;
        }
        dpeth_slice = dpeth_slice == UINT16_MAX ? _depth : dpeth_slice;
        Texture::CreateView(view_type, mipmap, dpeth_slice);
        if (view_type == kDSV || view_type == kRTV)
        {
            LOG_WARNING("Try to create dsv/rtv on a raw texture3d,call is on render texture instead!");
            return;
        }
        u16 view_index = CalculateViewIndex(view_type, mipmap, dpeth_slice);
        if (_views.contains(view_index) || view_index == kMainSRVIndex)
            return;
        D3DTextureViewInfo view_info(view_type, false);
        auto p_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
        auto [cpu_handle, gpu_handle] = alloc.At(0);
        if (view_type == ETextureViewType::kSRV)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Format = ConvertToDXGIFormat(_pixel_format);
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv_desc.Texture3D.MipLevels = 1;
            srv_desc.Texture3D.MostDetailedMip = mipmap;
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &srv_desc, cpu_handle);
        }
        else if (view_type == kUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC slice_uav_desc{};
            slice_uav_desc.Format = ConvertToDXGIFormat(_pixel_format);
            slice_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            slice_uav_desc.Texture3D.MipSlice = mipmap;
            slice_uav_desc.Texture3D.FirstWSlice = 0;
            slice_uav_desc.Texture3D.WSize = -1;
            p_device->CreateUnorderedAccessView(_p_d3dres.Get(), nullptr, &slice_uav_desc, cpu_handle);
        }
        else
        {
            AL_ASSERT(false);
        }
        view_info._gpu_handle = gpu_handle;
        view_info._gpu_alloc = std::move(alloc);
        _views[view_index] = std::move(view_info);
    }
    void D3DTexture3D::ReleaseView(Texture::ETextureViewType view_type, u16 mipmap, u16 dpeth_slice)
    {
        dpeth_slice = dpeth_slice == UINT16_MAX ? _depth : dpeth_slice;
        u16 idx = CalculateViewIndex(view_type, mipmap, dpeth_slice);
        _views.erase(idx);
    }
    void D3DTexture3D::Name(const String &new_name)
    {
        Object::Name(new_name);
        if (_p_d3dres)
            _p_d3dres->SetName(ToWStr(new_name).c_str());
    }
    void D3DTexture3D::GenerateMipmap()
    {
        auto cmd = CommandBufferPool::Get("MipmapGen0");
        u16 kernel = _p_mipmapgen_cs0->FindKernel("MipmapGen3D");
        u16 thread_num_x, thread_num_y, thread_num_z;
        _p_mipmapgen_cs0->GetThreadNum(kernel, thread_num_x, thread_num_y, thread_num_z);
        _p_mipmapgen_cs0->SetInt("SrcMipLevel", 0);
        _p_mipmapgen_cs0->SetInt("NumMipLevels", 4);
        _p_mipmapgen_cs0->SetInt("SrcDimension", 0);
        _p_mipmapgen_cs0->SetBool("IsSRGB", false);
        auto [mip1w, mip1h, mip1d] = CalculateMipSize(_width, _height, _depth, 1);
        _p_mipmapgen_cs0->SetVector("TexelSize", Vector4f(1.0f / (float) mip1w, 1.0f / (float) mip1h, 1.0f / (f32) mip1d, 0.0f));
        _p_mipmapgen_cs0->SetTexture("_SrcMip", this, 0);
        _p_mipmapgen_cs0->SetTexture("_OutMip1", this, 1);
        _p_mipmapgen_cs0->SetTexture("_OutMip2", this, 2);
        _p_mipmapgen_cs0->SetTexture("_OutMip3", this, 3);
        _p_mipmapgen_cs0->SetTexture("_OutMip4", this, 4);
        //保证线程数和第一级输出的mipmap像素数一一对应
        cmd->Dispatch(_p_mipmapgen_cs0.get(), kernel, mip1w / thread_num_x, mip1h / thread_num_y, mip1d / thread_num_z);
        cmd->StateTransition(this,D3DConvertUtils::ToALResState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS),0);
        if (_mipmap_count > 4)
        {
            auto [mip5w, mip5h, mip5d] = CalculateMipSize(_width, _height, _depth, 5);
            _p_mipmapgen_cs1->SetInt("SrcMipLevel", 4);
            _p_mipmapgen_cs1->SetInt("NumMipLevels", std::min<u16>(_mipmap_count - 5, 4));
            _p_mipmapgen_cs1->SetInt("SrcDimension", 0);
            _p_mipmapgen_cs1->SetBool("IsSRGB", false);
            _p_mipmapgen_cs1->SetVector("TexelSize", Vector4f(1.0f / (float) mip5w, 1.0f / (float) mip5h, 1.0f / (f32) mip5d, 0.0f));
            _p_mipmapgen_cs1->SetTexture("_SrcMip", this, 4);
            _p_mipmapgen_cs1->SetTexture("_OutMip1", this, 5);
            _p_mipmapgen_cs1->SetTexture("_OutMip2", this, 6);
            _p_mipmapgen_cs1->SetTexture("_OutMip3", this, 7);
            _p_mipmapgen_cs1->SetTexture("_OutMip4", this, 8);
            cmd->Dispatch(_p_mipmapgen_cs1.get(), kernel, mip5w / thread_num_x, mip5h / thread_num_y, mip5d / thread_num_z);
            cmd->StateTransition(this,D3DConvertUtils::ToALResState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS),4);
        }
        cmd->StateTransition(this,D3DConvertUtils::ToALResState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    TextureHandle D3DTexture3D::GetView(Texture::ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) const
    {
        dpeth_slice = dpeth_slice == -1 ? _depth : dpeth_slice;
        u16 idx = CalculateViewIndex(view_type, mipmap, dpeth_slice);
        if (_views.contains(idx))
        {
            return D3DDescriptorMgr::Get().GetBindGpuHandle(_views.at(idx)._gpu_alloc).ptr;
        }
        else
            return 0;
    }
    void D3DTexture3D::StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res)
    {
        _state_guard.MakesureResourceState(static_cast<D3DCommandBuffer*>(rhi_cmd)->NativeCmdList(),_p_d3dres.Get(),D3DConvertUtils::FromALResState(new_state),sub_res);
    }
    //----------------------------------------------------------------------------D3D3DTexture-----------------------------------------------------------------------------
    #pragma endregion

    #pragma region D3DRenderTexture
    //--------------------------------------------------------------------------D3DRenderTexture---------------------------------------------------------------------------
    static D3D12_RESOURCE_STATES ConvertToD3DResourceState(ETextureResState state)
    {
        switch (state)
        {
            case ETextureResState::kColorTagret:
                return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case ETextureResState::kShaderResource:
                return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            case ETextureResState::kDepthTarget:
                return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        return D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COMMON;
    }

    D3DRenderTexture::D3DRenderTexture(const RenderTextureDesc &desc) : RenderTexture(desc)
    {

    }

    D3DRenderTexture::~D3DRenderTexture()
    {
        g_pGfxContext->WaitForFence(_fence_value);
    }

    void D3DRenderTexture::UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) 
    {
        GpuResource::UploadImpl(ctx,rhi_cmd,params);
        D3D12_SRV_DIMENSION main_view_dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        u16 texture_num = 1u;
        if (_dimension == ETextureDimension::kCube)
        {
            main_view_dimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            texture_num = 6u;
        }
        else if (_dimension == ETextureDimension::kTex2DArray)
        {
            main_view_dimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            texture_num = _slice_num;
        }
        else if (_dimension == ETextureDimension::kCubeArray)
        {
            main_view_dimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            texture_num = _slice_num * 6;
            _mipmap_count = 1;
            LOG_WARNING("Cubemap array not support multi mipmap");
        }
        else {}
        bool is_for_depth = _depth_bit > 0;
        auto p_device = static_cast<D3DContext*>(ctx)->GetDevice();
        _tex_desc.MipLevels = _mipmap_count;
        //https://gamedev.stackexchange.com/questions/26687/what-are-the-valid-depthbuffer-texture-formats-in-directx-11-and-which-are-also
        //depth buffer format
        _tex_desc.Format = ConvertToDXGIFormat(_pixel_format);
        _tex_desc.Flags = is_for_depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (_is_random_access)
            _tex_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        _tex_desc.Width = std::max<u16>(_width, 1u);
        _tex_desc.Height = _height;
        _tex_desc.DepthOrArraySize = texture_num;
        _tex_desc.SampleDesc.Count = 1;
        _tex_desc.SampleDesc.Quality = 0;
        _tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = _tex_desc.Format;
        if (is_for_depth)
            clear_value.DepthStencil = {kZFar, 0};
        else
            memcpy(clear_value.Color, kClearColor, sizeof(kClearColor));
        CD3DX12_HEAP_PROPERTIES heap_prop(D3D12_HEAP_TYPE_DEFAULT);
        _state_guard = D3DResourceStateGuard(is_for_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET);
        ThrowIfFailed(p_device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE, &_tex_desc, _state_guard.CurState(),
                                                        &clear_value, IID_PPV_ARGS(_p_d3dres.GetAddressOf())));
        _p_d3dres->SetName(ToWStr(_name).c_str());
        u16 view_slice_count = std::max<u16>(1, _slice_num);
        _is_ready_for_rendering = true;
        //Main srv
        {
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            D3DTextureViewInfo view_info(ETextureViewType::kSRV, false);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
            view_desc.Format = GetCompatibleSRVFormat(_tex_desc.Format);
            view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (_dimension == ETextureDimension::kTex2D)
            {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view_desc.Texture2D.MostDetailedMip = 0;
                view_desc.Texture2D.MipLevels = _mipmap_count;
            }
            else if (_dimension == ETextureDimension::kTex2DArray)
            {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                view_desc.Texture2DArray.FirstArraySlice = 0;
                view_desc.Texture2DArray.ArraySize = texture_num;
                view_desc.Texture2DArray.MipLevels = _mipmap_count;
                view_desc.Texture2DArray.MostDetailedMip = 0;
            }
            else if (_dimension == ETextureDimension::kCube)
            {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                view_desc.TextureCube.MipLevels = _mipmap_count;
                view_desc.TextureCube.MostDetailedMip = 0;
            }
            else if (_dimension == ETextureDimension::kCubeArray)
            {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                view_desc.TextureCubeArray.MipLevels = _mipmap_count;
                view_desc.TextureCubeArray.First2DArrayFace = 0;
                view_desc.TextureCubeArray.MostDetailedMip = 0;
                view_desc.TextureCubeArray.NumCubes = _slice_num;
            }
            else
                AL_ASSERT(false);
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &view_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[kMainSRVIndex] = std::move(view_info);
        }
        if (_dimension == ETextureDimension::kTex2D || _dimension == ETextureDimension::kTex2DArray)
        {
            for (u16 i = 0; i < view_slice_count; i++)
            {
                if (is_for_depth)
                    CreateView(ETextureViewType::kDSV, 0, i);
                else
                    CreateView(ETextureViewType::kRTV, 0, i);
                CreateView(ETextureViewType::kSRV, 0, i);
                if (_is_random_access)
                    CreateView(ETextureViewType::kUAV, 0, i);
            }
        }
        else if (_dimension == ETextureDimension::kCube || _dimension == ETextureDimension::kCubeArray)
        {
            for (u16 i = 0; i < view_slice_count; i++)
            {
                for (u16 j = 1; j <= 6; j++)
                {
                    if (is_for_depth)
                        CreateView(ETextureViewType::kDSV, (ECubemapFace::ECubemapFace) j, 0, i);
                    else
                        CreateView(ETextureViewType::kRTV, (ECubemapFace::ECubemapFace) j, 0, i);
                    CreateView(ETextureViewType::kSRV, (ECubemapFace::ECubemapFace) j, 0, i);
                    if (_is_random_access)
                        CreateView(ETextureViewType::kUAV, (ECubemapFace::ECubemapFace) j, 0, i);
                }
            }
        }
        else
        {
            AL_ASSERT(false);
        }
        RenderTexture::CreateView();
    }

    void D3DRenderTexture::BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params)
    {
        if (auto param = dynamic_cast<BindParamsTexture*>(params); param != nullptr)
        {
            if (_views.contains(param->_view_idx))
            {
                GpuResource::BindImpl(rhi_cmd, params);
                auto d3dcmd = static_cast<D3DCommandBuffer *>(rhi_cmd);
                if (!RenderTexture::CanAsShaderResource(this) && !param->_is_compute_pipeline)
                {
                    LOG_WARNING("D3DRenderTexture::BindImpl: try to use a render texture: {} as rt and srv at the same time!", _name);
                    return;
                }
                auto view_type = _views[param->_view_idx]._view_type;
                if (view_type == ETextureViewType::kSRV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, param->_sub_res);
                    if (param->_is_compute_pipeline)
                        _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, param->_slot);
                    else
                        _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDraw(d3dcmd, param->_slot);
                }
                else if (view_type == ETextureViewType::kUAV)
                {
                    _state_guard.MakesureResourceState(d3dcmd->NativeCmdList(), _p_d3dres.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, param->_sub_res);
                    _views[param->_view_idx]._gpu_alloc.CommitDescriptorsForDispatch(d3dcmd, param->_slot);
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
            else
            {
                LOG_WARNING("D3DRenderTexture({})::BindImpl: invalid view index", _name);
            }
        }
    }

    void D3DRenderTexture::CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice)
    {
        if (!_p_d3dres)
        {
            LOG_ERROR("Can't create view before it been applied![RenderTexture: {}]", _name);
            return;
        }
        if (_dimension == ETextureDimension::kCube || _dimension == ETextureDimension::kCubeArray)
            return;
        u16 view_index = CalculateViewIndex(view_type, mipmap, array_slice);
        if (_views.contains(view_index))
            return;
        auto p_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        DXGI_FORMAT dx_format = ConvertToDXGIFormat(_pixel_format);
        if (view_type == ETextureViewType::kRTV)
        {
            CPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocCPU(1u,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3DTextureViewInfo view_info(view_type, true);
            auto cpu_handle = alloc.At(0);
            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
            rtv_desc.ViewDimension = _dimension == ETextureDimension::kTex2DArray ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
            rtv_desc.Format = dx_format;
            if (_dimension == ETextureDimension::kTex2D)
            {
                rtv_desc.Texture2D.MipSlice = mipmap;
                rtv_desc.Texture2D.PlaneSlice = 0;
            }
            else if (_dimension == ETextureDimension::kTex2DArray)
            {
                rtv_desc.Texture2DArray.FirstArraySlice = array_slice;
                rtv_desc.Texture2DArray.MipSlice = mipmap;
                rtv_desc.Texture2DArray.ArraySize = 1;
            }
            else { AL_ASSERT(false); }
            p_device->CreateRenderTargetView(_p_d3dres.Get(), &rtv_desc, cpu_handle);
            view_info._cpu_handle = cpu_handle;
            view_info._cpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
        else if (view_type == ETextureViewType::kDSV)
        {
            CPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocCPU(1u,D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            D3DTextureViewInfo view_info(view_type, true);
            auto cpu_handle = alloc.At(0);
            D3D12_DEPTH_STENCIL_VIEW_DESC view_desc{};
            view_desc.ViewDimension = _dimension == ETextureDimension::kTex2DArray ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : D3D12_DSV_DIMENSION_TEXTURE2D;
            if (_dimension == ETextureDimension::kTex2D)
            {
                view_desc.Texture2D.MipSlice = mipmap;
            }
            else if (_dimension == ETextureDimension::kTex2DArray)
            {
                view_desc.Texture2DArray.FirstArraySlice = array_slice;
                view_desc.Texture2DArray.MipSlice = mipmap;
                view_desc.Texture2DArray.ArraySize = 1;
            }
            else { AL_ASSERT(false); }
            p_device->CreateDepthStencilView(_p_d3dres.Get(), &view_desc, cpu_handle);
            view_info._cpu_handle = cpu_handle;
            view_info._cpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
        else if (view_type == ETextureViewType::kSRV)
        {
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            D3DTextureViewInfo view_info(view_type, false);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
            view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view_desc.ViewDimension = _dimension == ETextureDimension::kTex2DArray ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
            view_desc.Format = GetCompatibleSRVFormat(dx_format);
            if (_dimension == ETextureDimension::kTex2D)
            {
                view_desc.Texture2D.MostDetailedMip = mipmap;
                view_desc.Texture2D.MipLevels = 1;
            }
            else if (_dimension == ETextureDimension::kTex2DArray)
            {
                view_desc.Texture2DArray.FirstArraySlice = array_slice;
                view_desc.Texture2DArray.ArraySize = 1;
                view_desc.Texture2DArray.MipLevels = 1;
                view_desc.Texture2DArray.MostDetailedMip = mipmap;
            }
            else { AL_ASSERT(false); }
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &view_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
        else//UAV
        {
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            D3DTextureViewInfo view_info(view_type, false);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC view_desc{};
            view_desc.ViewDimension = _dimension == ETextureDimension::kTex2DArray ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
            view_desc.Format = dx_format;
            if (_dimension == ETextureDimension::kTex2D)
            {
                view_desc.Texture2D.MipSlice = mipmap;
            }
            else if (_dimension == ETextureDimension::kTex2DArray)
            {
                view_desc.Texture2DArray.FirstArraySlice = array_slice;
                view_desc.Texture2DArray.ArraySize = 1;
                view_desc.Texture2DArray.MipSlice = mipmap;
            }
            else { AL_ASSERT(false); }
            p_device->CreateUnorderedAccessView(_p_d3dres.Get(), nullptr, &view_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
    }

    TextureHandle D3DRenderTexture::GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice) const
    {
        u16 idx = CalculateViewIndex(view_type, mipmap, array_slice);
        if (_views.contains(idx))
        {
            return D3DDescriptorMgr::Get().GetBindGpuHandle(_views.at(idx)._gpu_alloc).ptr;
        }
        else
            return 0;
    }

    void D3DRenderTexture::ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice)
    {
        u16 idx = CalculateViewIndex(view_type, mipmap, array_slice);
        _views.erase(idx);
    }

    void D3DRenderTexture::CreateView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice)
    {
        if (_dimension == ETextureDimension::kTex2D || _dimension == ETextureDimension::kTex2DArray)
            return;
        u16 view_index = CalculateViewIndex(view_type, face, mipmap, array_slice);
        if (_views.contains(view_index))
            return;
        auto p_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        DXGI_FORMAT dx_format = ConvertToDXGIFormat(_pixel_format);
        u16 face_index = (u16) face - 1;
        if (view_type == ETextureViewType::kRTV)
        {
            CPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocCPU(1u,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3DTextureViewInfo view_info(view_type, true);
            auto cpu_handle = alloc.At(0);
            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv_desc.Format = dx_format;
            rtv_desc.Texture2DArray.FirstArraySlice = array_slice * 6 + face_index;
            rtv_desc.Texture2DArray.MipSlice = mipmap;
            rtv_desc.Texture2DArray.ArraySize = 1;
            p_device->CreateRenderTargetView(_p_d3dres.Get(), &rtv_desc, cpu_handle);
            view_info._cpu_handle = cpu_handle;
            view_info._cpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
        else if (view_type == ETextureViewType::kDSV)
        {
            CPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocCPU(1u,D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            D3DTextureViewInfo view_info(view_type, true);
            auto cpu_handle = alloc.At(0);
            D3D12_DEPTH_STENCIL_VIEW_DESC view_desc{};
            view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            view_desc.Texture2DArray.FirstArraySlice = array_slice * 6 + face_index;
            view_desc.Texture2DArray.MipSlice = mipmap;
            view_desc.Texture2DArray.ArraySize = 1;
            p_device->CreateDepthStencilView(_p_d3dres.Get(), &view_desc, cpu_handle);
            view_info._cpu_handle = cpu_handle;
            view_info._cpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
        else if (view_type == ETextureViewType::kSRV)
        {
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            D3DTextureViewInfo view_info(view_type, false);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
            //view_desc.ViewDimension = _dimension == ETextureDimension::kCube? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION::D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view_desc.Format = GetCompatibleSRVFormat(dx_format);
            view_desc.Texture2DArray.FirstArraySlice = array_slice * 6 + face_index;
            view_desc.Texture2DArray.ArraySize = 1;
            view_desc.Texture2DArray.MipLevels = 1;
            view_desc.Texture2DArray.MostDetailedMip = mipmap;
            p_device->CreateShaderResourceView(_p_d3dres.Get(), &view_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
        else//UAV
        {
            GPUVisibleDescriptorAllocation alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
            D3DTextureViewInfo view_info(view_type, false);
            auto [cpu_handle, gpu_handle] = alloc.At(0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC view_desc{};
            view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            view_desc.Format = dx_format;
            view_desc.Texture2DArray.FirstArraySlice = array_slice * 6 + face_index;
            view_desc.Texture2DArray.ArraySize = 1;
            view_desc.Texture2DArray.MipSlice = mipmap;
            p_device->CreateUnorderedAccessView(_p_d3dres.Get(), nullptr, &view_desc, cpu_handle);
            view_info._gpu_handle = gpu_handle;
            view_info._gpu_alloc = std::move(alloc);
            _views[view_index] = std::move(view_info);
        }
    }

    TextureHandle D3DRenderTexture::GetView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice) const
    {
        u16 idx = CalculateViewIndex(view_type, face, mipmap, array_slice);
        if (_views.contains(idx))
        {
            return D3DDescriptorMgr::Get().GetBindGpuHandle(_views.at(idx)._gpu_alloc).ptr;
        }
        else
            return 0;
    }

    void D3DRenderTexture::ReleaseView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice)
    {
        u16 idx = CalculateViewIndex(view_type, face, mipmap, array_slice);
        _views.erase(idx);
    }

    void D3DRenderTexture::GenerateMipmap()
    {
        static ComputeShader* s_mipmap_gen = g_pResourceMgr->Get<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
        auto cmd = CommandBufferPool::Get("MipmapGen");
        cmd->StateTransition(this,D3DConvertUtils::ToALResState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        u16 kernel = s_mipmap_gen->FindKernel("MipmapGen2D");
        for (int i = 1; i < 7; i++)
        {
            s_mipmap_gen->SetInt("SrcMipLevel", 0);
            s_mipmap_gen->SetInt("NumMipLevels", 4);
            s_mipmap_gen->SetInt("SrcDimension", 0);
            s_mipmap_gen->SetBool("IsSRGB", false);
            auto [mip1w, mip1h] = CalculateMipSize(_width, _height, 1);
            s_mipmap_gen->SetVector("TexelSize", Vector4f(1.0f / (float) mip1w, 1.0f / (float) mip1h, 0.0f, 0.0f));
            s_mipmap_gen->SetTexture("SrcMip", this, (ECubemapFace::ECubemapFace) i, 0);
            s_mipmap_gen->SetTexture("OutMip1", this, (ECubemapFace::ECubemapFace) i, 1);
            s_mipmap_gen->SetTexture("OutMip2", this, (ECubemapFace::ECubemapFace) i, 2);
            s_mipmap_gen->SetTexture("OutMip3", this, (ECubemapFace::ECubemapFace) i, 3);
            s_mipmap_gen->SetTexture("OutMip4", this, (ECubemapFace::ECubemapFace) i, 4);
            //static_cast<D3DComputeShader*>(_p_mipmapgen_cs0.get())->BindImpl(cmd, 32, 32, 1);
            //保证线程数和第一级输出的mipmap像素数一一对应
            cmd->Dispatch(s_mipmap_gen, kernel, mip1w / 8, mip1h / 8, 1);
            auto [mip5w, mip5h] = CalculateMipSize(_width, _height, 5);
            s_mipmap_gen->SetInt("SrcMipLevel", 4);
            s_mipmap_gen->SetInt("NumMipLevels", std::min<u16>(_mipmap_count - 5, 4));
            s_mipmap_gen->SetInt("SrcDimension", 0);
            s_mipmap_gen->SetBool("IsSRGB", false);
            s_mipmap_gen->SetVector("TexelSize", Vector4f(1.0f / (float) mip5w, 1.0f / (float) mip5h, 0.0f, 0.0f));
            s_mipmap_gen->SetTexture("SrcMip", this, (ECubemapFace::ECubemapFace) i, 4);
            s_mipmap_gen->SetTexture("OutMip1", this, (ECubemapFace::ECubemapFace) i, 5);
            s_mipmap_gen->SetTexture("OutMip2", this, (ECubemapFace::ECubemapFace) i, 6);
            s_mipmap_gen->SetTexture("OutMip3", this, (ECubemapFace::ECubemapFace) i, 7);
            if (_mipmap_count > 6)
            s_mipmap_gen->SetTexture("OutMip4", this, (ECubemapFace::ECubemapFace) i, 8);
            cmd->Dispatch(s_mipmap_gen, kernel, mip5w / 8, mip5h / 8, 1);
            cmd->StateTransition(this,D3DConvertUtils::ToALResState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        }
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }


    void *D3DRenderTexture::ReadBack(u16 mipmap, u16 array_slice, ECubemapFace::ECubemapFace face)
    {
        return nullptr;
        // auto d3d_device = dynamic_cast<D3DContext *>(g_pGfxContext)->GetDevice();
        // // Get the copy target location
        // auto [w, h] = CalculateMipSize(_width, _height, mipmap);
        // u64 buffer_size = w * h * _pixel_size;
        // // buffer_size = Math::AlignTo(buffer_size, 256);
        // D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
        // u32 row_count;
        // u64 aligned_row_pitch, aligned_buffer_size;//pitch size must 256 align
        // u32 subres_index = 0;
        // if (_dimension == ETextureDimension::kTex2D)
        //     subres_index = mipmap;
        // else if (_dimension == ETextureDimension::kCube)
        //     subres_index = (face - 1) * _mipmap_count + mipmap;
        // else
        //     AL_ASSERT(false);
        // d3d_device->GetCopyableFootprints(&_tex_desc, subres_index, 1, 0, &bufferFootprint, &row_count, &aligned_row_pitch, &aligned_buffer_size);
        // aligned_row_pitch = bufferFootprint.Footprint.RowPitch;
        // bufferFootprint.Footprint.Format = ConvertToDXGIFormat(_pixel_format);
        // bufferFootprint.Footprint.Width = w;
        // bufferFootprint.Footprint.Height = h;
        // bufferFootprint.Footprint.Depth = 1;
        // //bufferFootprint.Offset = std::max<u64>(buf_size_need - buffer_size, 0);
        // D3D12_HEAP_PROPERTIES readbackHeapProperties{CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK)};
        // D3D12_RESOURCE_DESC readbackBufferDesc{CD3DX12_RESOURCE_DESC::Buffer(aligned_buffer_size)};
        // ComPtr<ID3D12Resource> readback_buf;
        // d3d_device->CreateCommittedResource(&readbackHeapProperties, D3D12_HEAP_FLAG_NONE, &readbackBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        //                                     nullptr, IID_PPV_ARGS(readback_buf.GetAddressOf()));
        // auto cmd = CommandBufferPool::Get("Readback", ECommandBufferType::kCommandBufTypeCopy);
        // auto d3d_cmd = static_cast<D3DCommandBuffer *>(cmd.get())->NativeCmdList();
        // _state_guard.MakesureResourceState(d3d_cmd, _p_d3dres.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        // // bufferFootprint.Footprint.RowPitch = static_cast<u32>(_pixel_size * w);
        // // bufferFootprint.Footprint.Format = ConvertToDXGIFormat(_pixel_format);
        // const CD3DX12_TEXTURE_COPY_LOCATION copyDest(readback_buf.Get(), bufferFootprint);
        // AL_ASSERT(mipmap <= _mipmap_count);
        // const CD3DX12_TEXTURE_COPY_LOCATION copySrc(_p_d3dres.Get(), subres_index);
        // d3d_cmd->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);
        // //d3d_cmd->CopyResource(readback_buf.Get(), _p_d3dres.Get());
        // g_pGfxContext->ExecuteAndWaitCommandBuffer(cmd);
        // D3D12_RANGE readbackBufferRange{0, aligned_buffer_size};
        // f32 *data;
        // readback_buf->Map(0, &readbackBufferRange, reinterpret_cast<void **>(&data));
        // //use data here
        // u8 *aligned_out_data = new u8[aligned_buffer_size];
        // memcpy(aligned_out_data, data, aligned_buffer_size);
        // //end use
        // D3D12_RANGE emptyRange{0, 0};
        // readback_buf->Unmap(0, &emptyRange);
        // if (aligned_buffer_size != buffer_size)
        // {
        //     u8 *out_data = new u8[buffer_size];
        //     u32 row_pitch = _pixel_size * w;
        //     for (u32 i = 0; i < row_count; i++)
        //     {
        //         memcpy(out_data + row_pitch * i, aligned_out_data + aligned_row_pitch * i, row_pitch);
        //     }
        //     delete[] aligned_out_data;
        //     return out_data;
        // }
        // return aligned_out_data;
    }
    void D3DRenderTexture::ReadBackAsync(std::function<void(void *)> callback, u16 mipmap, u16 array_slice, ECubemapFace::ECubemapFace face)
    {
        // 使用引用捕获，处理异常并确保回调有效
        auto ret = std::async(std::launch::async, [&, callback]()
                   {
        try {
            void* result = ReadBack(mipmap, array_slice, face);
            callback(result);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception: {}", e.what());
            std::cerr << "Exception: " << e.what() << std::endl;
            callback(nullptr);
        } });
    }

    void D3DRenderTexture::Name(const String &value)
    {
        _name = value;
        if (_p_d3dres)
            _p_d3dres->SetName(ToWStr(value).c_str());
    }
    void D3DRenderTexture::StateTranslation(RHICommandBuffer *rhi_cmd, EResourceState new_state, u32 sub_res)
    {
        _state_guard.MakesureResourceState(static_cast<D3DCommandBuffer *>(rhi_cmd)->NativeCmdList(), _p_d3dres.Get(), D3DConvertUtils::FromALResState(new_state),sub_res);
    }

    TextureHandle D3DRenderTexture::ColorTexture(u16 view_index)
    {
        if (/*!CanAsShaderResource(this) || */ !_views.contains(view_index))
            return 0;
    #if defined(DEAR_IMGUI)
        static_cast<D3DContext&>(GraphicsContext::Get()).RecordImguiUsedTexture(this);
    #endif
        return D3DDescriptorMgr::Get().GetBindGpuHandle(_views[view_index]._gpu_alloc).ptr;
    }

    TextureHandle D3DRenderTexture::DepthTexture(u16 view_index)
    {
        if (!CanAsShaderResource(this) || !_views.contains(view_index))
            return 0;
    #if defined(DEAR_IMGUI)
        static_cast<D3DContext&>(GraphicsContext::Get()).RecordImguiUsedTexture(this);
    #endif
        return D3DDescriptorMgr::Get().GetBindGpuHandle(_views[view_index]._gpu_alloc).ptr;
        //return _views[view_index]._gpu_handle.ptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE *D3DRenderTexture::TargetCPUHandle(RHICommandBuffer *cmd, u16 index)
    {
        if (index == 0)
        {
            index = _depth_bit > 0 ? kMainDSVIndex : kMainRTVIndex;
        }
        if (!_views.contains(index))
            return nullptr;
        _state_guard.MakesureResourceState(static_cast<D3DCommandBuffer *>(cmd)->NativeCmdList(), _p_d3dres.Get(), _depth_bit > 0 ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET);
        RenderTexture::ResetRenderTarget(this);
        return &_views[index]._cpu_handle;
    }
    #pragma endregion
    //----------------------------------------------------------D3DRenderTexture----------------------------------------------------------------------
}// namespace Ailu