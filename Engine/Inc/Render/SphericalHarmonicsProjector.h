#pragma once
#ifndef __SPHERICAL_HARMONICS_PROJECTOR_H__
#define __SPHERICAL_HARMONICS_PROJECTOR_H__

#include "Assets/AssetRef.h"
#include "Framework/Common/NonCopyable.h"
#include "Render/Buffer.h"
#include "Render/RenderGraph/RenderGraphFwd.h"
#include "Render/SphericalHarmonics.h"

namespace Ailu::Render
{
    class CommandBuffer;
    class ComputeShader;
    class Texture;

    // 投影器只负责生成 SH，不绑定全局 g_environment_sh。
    // 该全局槽位由 EnvironmentSystem 独占绑定；否则探针烘焙会把探针 SH 写进环境 SH 槽位，
    // 使下一颗探针 capture 把上一颗探针的间接光当成环境光。
    class AILU_API SphericalHarmonicsProjector : public NonCopyable
    {
    public:
        SphericalHarmonicsProjector();

        void Initialize();
        RDG::RGHandle RecordRenderGraph(RDG::RenderGraph &graph, RDG::RGHandle cubemap);
        void Execute(CommandBuffer *cmd, Texture *cubemap, GPUBuffer *output);

        const Ref<GPUBuffer> &Output() const { return _output; }

    private:
        static constexpr u32 kMaxProjectionSize = 128u;
        static constexpr u32 kMaxGroupCount = 8u * 8u * 6u;
        static constexpr u32 kPartialCount = kMaxGroupCount * SphericalHarmonicsL2::kCoefficientCount;

        Ref<GPUBuffer> _partial;
        Ref<GPUBuffer> _output;
        AssetRef<ComputeShader> _shader;
        ComputeShaderKernelId _project_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _reduce_kernel = kInvalidComputeShaderKernelId;
        RDG::RGHandle _cubemap_handle;
        RDG::RGHandle _partial_handle;
        RDG::RGHandle _output_handle;
        bool _initialized = false;
    };
}

#endif // __SPHERICAL_HARMONICS_PROJECTOR_H__
