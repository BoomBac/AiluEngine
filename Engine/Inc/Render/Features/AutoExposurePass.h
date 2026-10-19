#pragma once
#ifndef __AUTO_EXPOSURE_PASS_H__
#define __AUTO_EXPOSURE_PASS_H__

#include "Assets/AssetRef.h"
#include "Render/CoreType.h"
#include "Render/RenderGraph/RenderGraphFwd.h"
#include "Render/Shader.h"
#include "Render/Texture.h"

#include <unordered_map>

namespace Ailu::Render
{
    class Camera;
    struct AutoExposureSettings;
    struct RenderingData;

    class AutoExposurePass
    {
    public:
        AutoExposurePass();

        RDG::RGHandle Record(RDG::RenderGraph &graph, RDG::RGHandle source, const RenderingData &rendering_data,
                             const Camera &camera, const AutoExposureSettings &settings);
        RenderTexture *GetPreExposureTexture(const Camera &camera) const;
        RenderTexture *GetPreviousPreExposureTexture(const Camera &camera) const;

    private:
        struct CameraExposureState
        {
            Ref<RenderTexture> _history_a;
            Ref<RenderTexture> _history_b;
            const Camera *_camera_identity = nullptr;
            u64 _last_render_frame = 0u;
            u64 _camera_reset_revision = 0u;
            bool _write_a = true;
            bool _history_valid = false;
            bool _has_rendered = false;
            bool _previous_pre_exposure_valid = false;
        };

        AssetRef<ComputeShader> _shader;
        ComputeShaderKernelId _clear_histogram_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _build_histogram_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _reduce_exposure_kernel = kInvalidComputeShaderKernelId;
        std::unordered_map<u64, CameraExposureState> _camera_states;
    };
}

#endif // __AUTO_EXPOSURE_PASS_H__
