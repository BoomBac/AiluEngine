#pragma once

#include <cstdint>

namespace Ailu::Render
{
    class Camera;
    class Texture;
    class RHICommandBuffer;

    enum class ESuperResolutionColorDomain : std::uint8_t
    {
        kPreExposedSceneLinear,
        kSceneLinear
    };

    struct SuperResolutionFrameData
    {
        std::uint64_t _camera_id = 0u;
        std::uint64_t _frame_index = 0u;
        std::uint64_t _reset_revision = 0u;
        std::uint32_t _render_width = 0u;
        std::uint32_t _render_height = 0u;
        std::uint32_t _output_width = 0u;
        std::uint32_t _output_height = 0u;
        float _jitter_x = 0.0f;
        float _jitter_y = 0.0f;
        float _frame_time_ms = 16.667f;
        float _camera_near = 0.1f;
        float _camera_far = 1000.0f;
        float _camera_fov_vertical = 1.0f;
        float _pre_exposure = 1.0f;
        ESuperResolutionColorDomain _input_color_domain = ESuperResolutionColorDomain::kPreExposedSceneLinear;
    };

    class StreamlineSR
    {
    public:
        static void ResolveCamera(Camera& camera);
        static bool Evaluate(const Camera& camera, const SuperResolutionFrameData& frame_data, Texture* color,
                             Texture* depth, Texture* motion_vectors, Texture* output,
                             RHICommandBuffer* command_buffer);
    };

    class SuperResolution
    {
    public:
        static void ResolveCamera(Camera& camera);
        static SuperResolutionFrameData Snapshot(const Camera& camera, std::uint64_t frame_index, float pre_exposure,
                                                ESuperResolutionColorDomain input_color_domain);
        static bool Evaluate(const SuperResolutionFrameData& frame_data, Texture* color, Texture* depth,
                            Texture* motion_vectors, Texture* output, RHICommandBuffer* command_buffer);
    };
}
