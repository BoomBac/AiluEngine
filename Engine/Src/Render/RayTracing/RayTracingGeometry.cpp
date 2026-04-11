#include "Render/RayTracing/RayTracingGeometry.h"
#include "Render/Mesh.h"
#include "Render/GraphicsContext.h"
#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "Render/CommandBuffer.h"
#include "pch.h"
namespace Ailu::Render
{
    RayTracingGeometryDesc::RayTracingGeometryDesc(Mesh *mesh, bool opaque)
    {
        _vertex_buffer = mesh->GetVertexBuffer().get();
        _index_buffer.reserve(mesh->SubmeshCount());
        for(u32 i = 0; i < mesh->SubmeshCount(); ++i)
        {
            _index_buffer.push_back(mesh->GetIndexBuffer(i).get());
        }
        _vertex_stride = _vertex_buffer->GetLayout().GetStride(0);
        _vertex_count = _vertex_buffer->GetVertexCount();
        _opaque = opaque;
    }

    Ref<RayTracingGeometry> RayTracingGeometry::Create(const RayTracingGeometryDesc &desc,const String& name)
    {
        switch (RendererAPI::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                auto geometry = MakeRef<RHI::DX12::D3DRayTracingGeometry>(desc);
                geometry->Name(name);
                geometry->Apply();
                geometry->Build();
                return geometry;
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }
    
    RayTracingGeometry::RayTracingGeometry(const RayTracingGeometryDesc &desc) : _desc(desc)
    {
        _res_type = Render::EGpuResType::kBottomAS;
    }

    RayTracingGeometry::~RayTracingGeometry()
    {
    }
    NativeHandle RayTracingGeometry::NativeResource()
    {
        return _native_resource;
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