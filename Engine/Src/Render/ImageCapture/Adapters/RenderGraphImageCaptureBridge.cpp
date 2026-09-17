#include "Render/ImageCapture/Adapters/RenderGraphImageCaptureBridge.h"

#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"

#include <mutex>
#include <unordered_map>

namespace Ailu::Render
{
namespace
{
std::mutex s_selector_mutex;
std::unordered_map<ImageCaptureSessionId, RenderGraphCaptureSelector> s_selectors;

struct SelectorMatchState
{
    u64 _frame = ~u64(0);
    u32 _camera_ordinal = 0u;
};

std::unordered_map<ImageCaptureSessionId, SelectorMatchState> s_selector_match_states;

bool TryCapturePassResource(ImageCaptureSessionId session_id, RDG::RenderGraph &graph,
                            const RDG::CompiledRenderPass &compiled_pass, u32 resource_index, bool is_input,
                            CommandBuffer &cmd)
{
    if (compiled_pass._pass == nullptr)
    {
        ImageCaptureService::ReportError(session_id, "render graph pass is invalid");
        return false;
    }
    const auto &records = is_input ? compiled_pass._pass->_input_access_records :
                                     compiled_pass._pass->_output_access_records;
    if (resource_index >= records.size())
    {
        ImageCaptureService::ReportError(session_id, is_input ? "render graph input index is invalid" :
                                                         "render graph output index is invalid");
        return false;
    }

    const auto &record = records[resource_index];
    auto *texture = graph.Resolve<Texture>(record._handle);
    if (texture == nullptr)
    {
        ImageCaptureService::ReportError(session_id, "render graph capture target was not found");
        return false;
    }

    ImageCaptureTextureDesc texture_desc;
    texture_desc._all_sub_resources = record._access._all_sub_resources;
    texture_desc._mip_level = record._access._mip_level;
    texture_desc._mip_count = record._access._mip_count;
    texture_desc._array_slice = record._access._array_slice;
    texture_desc._array_slice_count = record._access._array_slice_count;
    bool found_restore_state = false;
    for (const auto &usage: compiled_pass._resource_usages)
    {
        if (usage._handle != record._handle)
            continue;
        const bool range_matches = texture_desc._all_sub_resources ||
            usage._sub_resource == kTotalSubRes ||
            usage._sub_resource == texture->CalculateSubResIndex(
                static_cast<u16>(texture_desc._mip_level), static_cast<u16>(texture_desc._array_slice));
        if (range_matches)
        {
            texture_desc._has_explicit_restore_state = true;
            texture_desc._restore_state = usage._required_state;
            found_restore_state = true;
            break;
        }
    }
    if (!found_restore_state)
    {
        ImageCaptureService::ReportError(session_id, "render graph capture state was not compiled");
        return false;
    }
    const auto handle = texture->Handle();
    LOG_INFO("[ImageCapture] Graph target session={} pass={} resource={} input={} index={} texture={} handle=({}, {}) "
             "size={}x{} format={} restore_state={}",
             session_id, compiled_pass._pass->_name, graph.GetResourceName(record._handle), is_input, resource_index,
             static_cast<void *>(texture), handle._index, handle._generation, texture->Width(), texture->Height(),
             static_cast<u32>(texture->PixelFormat()), static_cast<u32>(texture_desc._restore_state));
    return ImageCaptureService::TryCapture(session_id, cmd, texture, texture_desc);
}
} // namespace

bool RenderGraphImageCaptureBridge::TryCapturePassInput(ImageCaptureSessionId session_id, RDG::RenderGraph &graph,
                                                        const RDG::CompiledRenderPass &compiled_pass, u32 input_index,
                                                        CommandBuffer &cmd)
{
    return TryCapturePassResource(session_id, graph, compiled_pass, input_index, true, cmd);
}

bool RenderGraphImageCaptureBridge::TryCapturePassOutput(ImageCaptureSessionId session_id, RDG::RenderGraph &graph,
                                                         const RDG::CompiledRenderPass &compiled_pass, u32 output_index,
                                                         CommandBuffer &cmd)
{
    return TryCapturePassResource(session_id, graph, compiled_pass, output_index, false, cmd);
}

void RenderGraphImageCaptureBridge::SetSelector(ImageCaptureSessionId session_id, RenderGraphCaptureSelector selector)
{
    std::lock_guard lock(s_selector_mutex);
    s_selectors[session_id] = std::move(selector);
    s_selector_match_states[session_id] = {};
}

void RenderGraphImageCaptureBridge::RemoveSelector(ImageCaptureSessionId session_id)
{
    std::lock_guard lock(s_selector_mutex);
    s_selectors.erase(session_id);
    s_selector_match_states.erase(session_id);
}

void RenderGraphImageCaptureBridge::TryCaptureActivePassInputs(RDG::RenderGraph &graph,
                                                               const RDG::CompiledRenderPass &compiled_pass,
                                                               CommandBuffer &cmd)
{
    if (compiled_pass._pass == nullptr)
        return;

    Vector<std::pair<ImageCaptureSessionId, RenderGraphCaptureSelector>> selectors;
    {
        std::lock_guard lock(s_selector_mutex);
        selectors.reserve(s_selectors.size());
        for (const auto &[session_id, selector]: s_selectors)
            selectors.emplace_back(session_id, selector);
    }

    const u32 pass_ordinal = graph.GetPassNameOrdinal(compiled_pass);

    for (const auto &[session_id, selector]: selectors)
    {
        if (selector._capture_output || selector._pass_name != compiled_pass._pass->_name ||
            selector._pass_name_ordinal != pass_ordinal)
            continue;
        const auto &inputs = compiled_pass._pass->_input_access_records;
        if (selector._input_access_index >= inputs.size())
        {
            ImageCaptureService::ReportError(session_id, "render graph input index is invalid");
            RemoveSelector(session_id);
            continue;
        }
        const String resource_name = graph.GetResourceName(inputs[selector._input_access_index]._handle);
        if (!selector._resource_name.empty() && resource_name != selector._resource_name)
        {
            ImageCaptureService::ReportError(session_id, "render graph capture resource changed during capture");
            RemoveSelector(session_id);
            continue;
        }
        TryCapturePassInput(session_id, graph, compiled_pass, selector._input_access_index, cmd);
        const auto status = ImageCaptureService::GetStatus(session_id);
        if (status._state == EImageCaptureState::kCompleted || status._state == EImageCaptureState::kFailed ||
            status._state == EImageCaptureState::kCancelled)
            RemoveSelector(session_id);
    }
}

void RenderGraphImageCaptureBridge::TryCaptureActivePassOutputs(RDG::RenderGraph &graph,
                                                                const RDG::CompiledRenderPass &compiled_pass,
                                                                CommandBuffer &cmd)
{
    if (compiled_pass._pass == nullptr)
        return;

    Vector<std::pair<ImageCaptureSessionId, RenderGraphCaptureSelector>> selectors;
    {
        std::lock_guard lock(s_selector_mutex);
        selectors.reserve(s_selectors.size());
        for (const auto &[session_id, selector]: s_selectors)
            selectors.emplace_back(session_id, selector);
    }

    const u32 pass_ordinal = graph.GetPassNameOrdinal(compiled_pass);

    for (const auto &[session_id, selector]: selectors)
    {
        if (!selector._capture_output || selector._pass_name != compiled_pass._pass->_name ||
            selector._pass_name_ordinal != pass_ordinal)
            continue;

        u32 camera_ordinal = 0u;
        {
            std::lock_guard lock(s_selector_mutex);
            auto &match_state = s_selector_match_states[session_id];
            const u64 frame = GraphicsContext::Get().GetFrameCount();
            if (match_state._frame != frame)
            {
                match_state._frame = frame;
                match_state._camera_ordinal = 0u;
            }
            camera_ordinal = match_state._camera_ordinal++;
        }
        if (camera_ordinal != selector._camera_ordinal)
            continue;

        const auto &outputs = compiled_pass._pass->_output_access_records;
        if (selector._input_access_index >= outputs.size())
        {
            ImageCaptureService::ReportError(session_id, "render graph output index is invalid");
            RemoveSelector(session_id);
            continue;
        }
        const String resource_name = graph.GetResourceName(outputs[selector._input_access_index]._handle);
        if (!selector._resource_name.empty() && resource_name != selector._resource_name)
        {
            ImageCaptureService::ReportError(session_id, "render graph capture resource changed during capture");
            RemoveSelector(session_id);
            continue;
        }
        // The output is still bound as an RTV after the pass callback. Unbind it before the readback
        // transitions the resource to COPY_SOURCE; otherwise DX12 may observe the active OM binding
        // while recording CopyTextureRegion.
        Vector<RenderTexture *> no_render_targets;
        cmd.SetRenderTargets(no_render_targets, nullptr);
        TryCapturePassOutput(session_id, graph, compiled_pass, selector._input_access_index, cmd);
        const auto status = ImageCaptureService::GetStatus(session_id);
        if (status._state == EImageCaptureState::kCompleted || status._state == EImageCaptureState::kFailed ||
            status._state == EImageCaptureState::kCancelled)
            RemoveSelector(session_id);
    }
}
} // namespace Ailu::Render
