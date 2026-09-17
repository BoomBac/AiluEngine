#pragma once

#include "Render/ImageCapture/ImageCaptureService.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu::Render
{
struct RenderGraphCaptureSelector
{
    String _pass_name;
    u32 _pass_name_ordinal = 0u;
    String _resource_name;
    // The index is an input index for input captures and an output index for output captures.
    u32 _input_access_index = 0u;
    bool _capture_output = false;
    // RenderPipeline may execute the same pass once per camera in one engine frame.
    u32 _camera_ordinal = 0u;
};

class AILU_API RenderGraphImageCaptureBridge final
{
public:
    static bool TryCapturePassInput(ImageCaptureSessionId session_id, RDG::RenderGraph &graph,
                                    const RDG::CompiledRenderPass &compiled_pass, u32 input_index,
                                    CommandBuffer &cmd);
    static bool TryCapturePassOutput(ImageCaptureSessionId session_id, RDG::RenderGraph &graph,
                                     const RDG::CompiledRenderPass &compiled_pass, u32 output_index,
                                     CommandBuffer &cmd);

    static void SetSelector(ImageCaptureSessionId session_id, RenderGraphCaptureSelector selector);
    static void RemoveSelector(ImageCaptureSessionId session_id);
    static void TryCaptureActivePassInputs(RDG::RenderGraph &graph, const RDG::CompiledRenderPass &compiled_pass,
                                           CommandBuffer &cmd);
    static void TryCaptureActivePassOutputs(RDG::RenderGraph &graph, const RDG::CompiledRenderPass &compiled_pass,
                                            CommandBuffer &cmd);
};
} // namespace Ailu::Render
