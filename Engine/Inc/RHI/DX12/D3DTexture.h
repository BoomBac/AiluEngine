#pragma once
#ifndef __D3D_TEXTURE2D_H__
#define __D3D_TEXTURE2D_H__

#include "d3dx12.h"
#include <map>

#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/D3DResourceBase.h"
#include "RHI/DX12/DescriptorManager.h"
#include "Render/Texture.h"
using Microsoft::WRL::ComPtr;

namespace Ailu
{
    class ComputeShader;
    struct D3DTextureViewInfo
    {
    public:
        D3DTextureViewInfo() {};
        D3DTextureViewInfo(Texture::ETextureViewType type, bool is_cpu_view, u16 index = 0) : _view_type(type), _alloc_index(index), _is_cpu_view(is_cpu_view) {};
        void operator=(D3DTextureViewInfo &&other) noexcept
        {
            _alloc_index = other._alloc_index;
            _cpu_alloc = std::move(other._cpu_alloc);
            _cpu_handle = other._cpu_handle;
            _view_type = other._view_type;
            _is_cpu_view = other._is_cpu_view;
            other._cpu_handle = (D3D12_CPU_DESCRIPTOR_HANDLE) 0;
        }
        ~D3DTextureViewInfo()
        {
            if (_is_cpu_view)
                D3DDescriptorMgr::Get().Free(std::move(_cpu_alloc));
            else
                D3DDescriptorMgr::Get().Free(std::move(_gpu_alloc));
        }
        union
        {
            CPUVisibleDescriptorAllocation _cpu_alloc;
            GPUVisibleDescriptorAllocation _gpu_alloc;
        };
        union
        {
            D3D12_CPU_DESCRIPTOR_HANDLE _cpu_handle;
            D3D12_GPU_DESCRIPTOR_HANDLE _gpu_handle;
        };
        u16 _alloc_index;
        bool _is_cpu_view;
        Texture::ETextureViewType _view_type;
    };

    class D3DTexture2D : public Texture2D
    {
        friend class DDSParser;

    public:
        D3DTexture2D(const Texture2DInitializer& initializer);
        ~D3DTexture2D();
        void Release() final;
        //for texture2d(s)
        void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) final;
        TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) const final;
        void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) final;
        void Name(const String &new_name) final;
        D3D12_GPU_DESCRIPTOR_HANDLE GetMainGPUSRVHandle() const { return _views.at(0)._gpu_handle; };
        void GenerateMipmap() final;
        
        private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
    private:
        D3DResourceStateGuard _state_guard;
        ComPtr<ID3D12Resource> _p_d3dres;
        Map<u16, D3DTextureViewInfo> _views;
    };

    class D3DCubeMap : public CubeMap
    {
    public:
        D3DCubeMap(u16 width, bool mipmap_chain = true, ETextureFormat::ETextureFormat format = ETextureFormat::kRGBA32, bool linear = false, bool random_access = false);
        ~D3DCubeMap();
        
        void CreateView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) final;
        TextureHandle GetView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) const final;
        void ReleaseView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) final;

        private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
    private:
        ComPtr<ID3D12Resource> _p_d3dres;
        D3DResourceStateGuard _state_guard;
        Map<u16, D3DTextureViewInfo> _views;
    };

    class D3DTexture3D : public Texture3D
    {
    public:
        explicit D3DTexture3D(const Texture3DInitializer &initializer);
        ~D3DTexture3D() override;
        void CreateView(ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) final;
        [[nodiscard]] TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) const final;
        void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 dpeth_slice) final;
        void Name(const String &new_name) final;
        void GenerateMipmap() final;
        void StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res) final;
    private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
    private:
        ComPtr<ID3D12Resource> _p_d3dres;
        D3DResourceStateGuard _state_guard;
        Map<u16, D3DTextureViewInfo> _views;
        //gen mipmap for 1~4
        Ref<ComputeShader> _p_mipmapgen_cs0 = nullptr;
        //gen mipmap for 5~max
        Ref<ComputeShader> _p_mipmapgen_cs1 = nullptr;
    };

    class D3DRenderTexture : public RenderTexture
    {
        inline const static Vector4f kClearColor = Colors::kBlack;

    public:
        D3DRenderTexture(const RenderTextureDesc &desc);
        ~D3DRenderTexture() final;
        void StateTranslation(RHICommandBuffer* rhi_cmd,EResourceState new_state,u32 sub_res) final;
        //for texture2d(s)
        void CreateView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) final;
        TextureHandle GetView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) const final;
        void ReleaseView(ETextureViewType view_type, u16 mipmap, u16 array_slice = 0) final;
        //for cube_map(s)
        void CreateView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) final;
        TextureHandle GetView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) const final;
        void ReleaseView(ETextureViewType view_type, ECubemapFace::ECubemapFace face, u16 mipmap, u16 array_slice = 0) final;
        void Name(const String &value) final;
        TextureHandle ColorTexture(u16 view_index = kMainSRVIndex) final;
        TextureHandle DepthTexture(u16 view_index = kMainSRVIndex) final;
        void GenerateMipmap() final;
        void *ReadBack(u16 mipmap, u16 array_slice = 0, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown) final;
        void ReadBackAsync(std::function<void(void *)> callback, u16 mipmap, u16 array_slice = 0, ECubemapFace::ECubemapFace face = ECubemapFace::kUnknown) final;
        D3D12_CPU_DESCRIPTOR_HANDLE *TargetCPUHandle(RHICommandBuffer *cmd, u16 index);
        
        private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
        void BindImpl(RHICommandBuffer* rhi_cmd,BindParams* params) final;
    private:
        D3D12_RESOURCE_DESC _tex_desc{};
        ComPtr<ID3D12Resource> _p_d3dres;
        D3DResourceStateGuard _state_guard;
        Map<u16, D3DTextureViewInfo> _views;
    };
}// namespace Ailu


#endif// !D3D_TEXTURE2D_H__
