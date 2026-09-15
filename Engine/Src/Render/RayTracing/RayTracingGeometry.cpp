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
                auto geometry = AdoptGpuResource(new RHI::DX12::D3DRayTracingGeometry(desc));
                geometry->Name(name);
                if (geometry->IsDynamic())
                    geometry->ApplySync();
                else
                {
                    geometry->Apply();
                    geometry->Build();
                }
                return geometry;
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }
    
    RayTracingGeometry::RayTracingGeometry(const RayTracingGeometryDesc &desc) : _desc(desc)
    {
        _res_type = Render::EGpuResType::kBottomAS;
        _index_buffers.reserve(desc._index_buffer.size());
        for (auto ib : desc._index_buffer)
        {
            _index_buffers.push_back(ib);
        }
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
        Build(cmd.get());
        GraphicsContext::Get().ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }

    void RayTracingGeometry::Build(CommandBuffer *cmd)
    {
        if (cmd == nullptr)
            return;
        cmd->BuildAS(this, false);
        _has_been_built = true;
    }

    void RayTracingGeometry::Update(CommandBuffer *cmd)
    {
        AL_ASSERT_MSG(IsDynamic(), "Only dynamic BLAS can be updated");
        AL_ASSERT_MSG(_has_been_built, "A dynamic BLAS must be built before it is updated");
        if (cmd == nullptr)
            return;
        cmd->BuildAS(this, true);
    }
}// namespace Ailu::Render
