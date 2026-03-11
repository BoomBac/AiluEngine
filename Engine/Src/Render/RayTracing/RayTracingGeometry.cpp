#include "Render/RayTracing/RayTracingGeometry.h"
#include "Render/Buffer.h"
#include "Render/Mesh.h"
#include "Render/GraphicsContext.h"
#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "Render/CommandBuffer.h"
#include "pch.h"
namespace Ailu::Render
{
    RayTracingGeometryDesc::RayTracingGeometryDesc(Mesh *mesh, u16 sub_mesh, bool opaque)
    {
        AL_ASSERT(sub_mesh < mesh->SubmeshCount());
        _vertex_buffer = mesh->GetVertexBuffer().get();
        _index_buffer = mesh->GetIndexBuffer().get();
        _vertex_stride = _vertex_buffer->GetLayout().GetStride(0);
        _vertex_count = _vertex_buffer->GetVertexCount();
        _index_count = _index_buffer->GetCount();
        _opaque = opaque;
    }

    Ref<RayTracingGeometry> RayTracingGeometry::Create(const RayTracingGeometryDesc &desc)
    {
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                return MakeRef<RHI::DX12::D3DRayTracingGeometry>(desc);
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }
    
    RayTracingGeometry::RayTracingGeometry(const RayTracingGeometryDesc &desc) : _desc(desc)
    {
    }

    RayTracingGeometry::~RayTracingGeometry()
    {
    }
    NativeHandle RayTracingGeometry::NativeResource()
    {
        if (_blas_buffer == nullptr)
            return NativeHandle();
        return _blas_buffer->NativeResource();
    }

    void RayTracingGeometry::Build()
    {
        auto cmd = CommandBufferPool::Get();
        cmd->BuildAS(this);
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

    void RayTracingGeometry::Update()
    {
        auto cmd = CommandBufferPool::Get();
        cmd->BuildAS(this,false);
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
}// namespace Ailu::Render