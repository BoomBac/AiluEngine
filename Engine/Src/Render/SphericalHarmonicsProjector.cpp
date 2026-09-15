#include "pch.h"

#include "Render/SphericalHarmonicsProjector.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Shader.h"
#include "Render/Texture.h"

namespace Ailu::Render
{
    namespace
    {
        constexpr u32 kMaxProjectionSize = 128u;

        u32 GetProjectionSize(const Texture *cubemap)
        {
            return std::min<u32>(cubemap->Width(), kMaxProjectionSize);
        }

        u32 GetGroupCount(u32 projection_size)
        {
            const u32 groups_per_axis = (projection_size + 15u) / 16u;
            return groups_per_axis * groups_per_axis * 6u;
        }
    }

    SphericalHarmonicsProjector::SphericalHarmonicsProjector()
    {
        Initialize();
    }

    void SphericalHarmonicsProjector::Initialize()
    {
        if (_initialized)
            return;

        _shader = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/environment_sh_project.alasset");
        _project_kernel = _shader != nullptr ? _shader->FindKernel("SHProject") : kInvalidComputeShaderKernelId;
        _reduce_kernel = _shader != nullptr ? _shader->FindKernel("SHReduce") : kInvalidComputeShaderKernelId;
        BufferDesc partial_desc;
        partial_desc._target = EGPUBufferTarget::kStructured;
        partial_desc._element_size = sizeof(Vector4f);
        partial_desc._element_num = kPartialCount;
        _partial = GPUBuffer::CreateSync(partial_desc, "EnvironmentSHPartial");
        BufferDesc output_desc;
        output_desc._target = EGPUBufferTarget::kStructured;
        output_desc._element_size = sizeof(Vector4f);
        output_desc._element_num = SphericalHarmonicsL2::kCoefficientCount;
        _output = GPUBuffer::CreateSync(output_desc, "EnvironmentSH");
        _initialized = _shader != nullptr && _partial != nullptr && _output != nullptr &&
                       _project_kernel != kInvalidComputeShaderKernelId && _reduce_kernel != kInvalidComputeShaderKernelId;
    }

    RDG::RGHandle SphericalHarmonicsProjector::RecordRenderGraph(RDG::RenderGraph &graph, RDG::RGHandle cubemap)
    {
        if (!_initialized || !cubemap.IsValid())
            return {};

        _cubemap_handle = cubemap;
        graph.AddPass("SphericalHarmonicsProject", RDG::PassDesc{RDG::EPassType::kCompute},
                      [&](RDG::RenderGraphBuilder &builder)
                      {
                          builder.Read(_cubemap_handle);
                          _partial_handle = builder.Write(builder.Import(_partial.get()), EResourceUsage::kWriteUAV);
                          _output_handle = builder.Write(builder.Import(_output.get()), EResourceUsage::kWriteUAV);
                      },
                      [this](RDG::RenderGraph &render_graph, CommandBuffer *cmd, const RenderingData &data)
                      {
                          auto *cubemap_texture = render_graph.Resolve<Texture>(_cubemap_handle);
                          auto *partial = render_graph.Resolve<GPUBuffer>(_partial_handle);
                          auto *output = render_graph.Resolve<GPUBuffer>(_output_handle);
                          if (cubemap_texture == nullptr || partial == nullptr || output == nullptr)
                              return;
                          const u32 projection_size = GetProjectionSize(cubemap_texture);
                          const u32 group_count = GetGroupCount(projection_size);
                          const u32 groups_per_axis = (projection_size + 15u) / 16u;

                          _shader->SetTexture(_project_kernel, "_Cubemap", cubemap_texture);
                          _shader->SetBuffer(_project_kernel, "_Partial", partial);
                          _shader->SetInt("_ProjectionSize", projection_size);
                          cmd->RequireState(partial, EResourceState::kUnorderedAccess);
                          cmd->Dispatch(_shader.get(), _project_kernel, groups_per_axis, groups_per_axis, 6u);
                          cmd->RequireState(partial, EResourceState::kNonPixelShaderResource);

                          _shader->SetBuffer(_reduce_kernel, "_Partial", partial);
                          _shader->SetBuffer(_reduce_kernel, "_Output", output);
                          _shader->SetInt("_PartialCount", group_count);
                          cmd->RequireState(output, EResourceState::kUnorderedAccess);
                          cmd->Dispatch(_shader.get(), _reduce_kernel, 1u, 1u, 1u);
                          cmd->RequireState(output, EResourceState::kAllShaderResource);
                      });
        return _output_handle;
    }

    void SphericalHarmonicsProjector::Execute(CommandBuffer *cmd, Texture *cubemap, GPUBuffer *output)
    {
        if (!_initialized || cmd == nullptr || cubemap == nullptr || output == nullptr)
            return;
        const u32 projection_size = GetProjectionSize(cubemap);
        const u32 group_count = GetGroupCount(projection_size);
        const u32 groups_per_axis = (projection_size + 15u) / 16u;
        _shader->SetTexture(_project_kernel, "_Cubemap", cubemap);
        _shader->SetBuffer(_project_kernel, "_Partial", _partial.get());
        _shader->SetInt("_ProjectionSize", projection_size);
        cmd->RequireState(_partial.get(), EResourceState::kUnorderedAccess);
        cmd->Dispatch(_shader.get(), _project_kernel, groups_per_axis, groups_per_axis, 6u);
        cmd->RequireState(_partial.get(), EResourceState::kNonPixelShaderResource);
        _shader->SetBuffer(_reduce_kernel, "_Partial", _partial.get());
        _shader->SetBuffer(_reduce_kernel, "_Output", output);
        _shader->SetInt("_PartialCount", group_count);
        cmd->RequireState(output, EResourceState::kUnorderedAccess);
        cmd->Dispatch(_shader.get(), _reduce_kernel, 1u, 1u, 1u);
        cmd->RequireState(output, EResourceState::kAllShaderResource);
    }
}
