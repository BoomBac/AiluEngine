#pragma once
#ifndef __D3D_RAY_TRACING_SHADER_H__
#define __D3D_RAY_TRACING_SHADER_H__
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include "Render/RayTracing/RayTracingShader.h"

namespace Ailu::RHI::DX12
{
    using Microsoft::WRL::ComPtr;

    class D3DRayTracingShader : public Render::RayTracingShader
    {
    public:
        D3DRayTracingShader(const WString &sys_path);
        ~D3DRayTracingShader();
        const D3D12_DISPATCH_RAYS_DESC& GetDispatchDesc(u32 w,u32 h, u32 depth);
    private:
        void DispatchRays(RHICommandBuffer* cmd,uint w, uint h, uint depth) final;
        bool RHICompileImpl(Render::ShaderVariantHash variant_hash, bool is_load_cache) final;
        void BuildShaderTables();
        void LoadReflectionInfo(ID3D12LibraryReflection *reflection);
        void GenerateRootSignature();
        void BindResources(RHICommandBuffer *cmd, const BindState &state);
    private:
        ComPtr<ID3DBlob> _byte_code;
        ComPtr<ID3D12StateObject> _state_object;
        // Root signatures
        ComPtr<ID3D12RootSignature> _local_root_signature;
        ComPtr<ID3D12RootSignature> _global_root_signature;
        ComPtr<ID3D12Resource> _ray_gen_stb;
        ComPtr<ID3D12Resource> _miss_stb;
        ComPtr<ID3D12Resource> _hit_group_stb;
        u32 _hit_group_shader_record_size = 0;
        ID3D12Device5* _dev = nullptr;
        D3D12_DISPATCH_RAYS_DESC _dispatch_desc{};
        bool _has_bindless_texture2d = false;
        u16 _bindless_texture_slot = static_cast<u16>(-1);
    };
}

#endif // !__D3D_RAY_TRACING_SHADER_H__