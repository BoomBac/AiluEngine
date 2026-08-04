#pragma once

#include "Framework/Core/CoreMinimal.h"

namespace Ailu::Render::RDG
{
    class RenderGraph;
    struct CompiledRenderPass;
}

namespace Ailu::Render::FrameDebugger
{
using CaptureStringId = u32;
using CaptureObjectId = u64;
constexpr u32 kInvalidFrameEventId = ~0u;

// Transient metadata filled by RenderGraph::Execute() and forwarded to the command recording thread via
// CommandBuffer/SubmitParams. Resolved into RenderGraphPassCapture / RenderGraphResourceAccessCapture /
// ResourceBarrierCapture in RecordCommandGroup(). Must not be written into the final FrameCapture.
struct CapturePassMetadata
{
    RDG::RenderGraph *_render_graph = nullptr;
    const RDG::CompiledRenderPass *_compiled_pass = nullptr;
};

enum class ECaptureObjectType : u8
{
    kUnknown,
    kMaterial,
    kShader,
    kGraphicsPso,
    kComputeShader,
    kTexture,
    kBuffer,
    kVertexBuffer,
    kIndexBuffer,
    kConstantBuffer,
    kRenderTarget
};

struct CaptureObjectInfo
{
    CaptureObjectId _id = 0u;
    ECaptureObjectType _type = ECaptureObjectType::kUnknown;
    CaptureStringId _name = 0u;
    u64 _runtime_instance_id = 0u;
    u64 _native_handle = 0u;
};

enum class EFrameEventType : u8
{
    kFrame,
    kRenderGraphPass,
    kCommandGroup,
    kProfilerScope,
    kSetRenderTarget,
    kClearTarget,
    kDraw,
    kDispatch,
    kDispatchRays,
    kBuildAccelerationStructure,
    kResourceUpload,
    kResourceBarrier,
    kUavBarrier,
    kCopyCounter,
    kReadback,
    kPresent,
    kDebuggerInternal
};

enum class EFrameEventExecutionResult : u8
{
    kExecuted,
    kRecordedOnly,
    kSkippedShaderNotReady,
    kSkippedPsoNotReady,
    kSkippedInvalidDispatch,
    kSkippedInvalidResource,
    kSkippedUnknown
};

struct FrameEvent
{
    u32 _event_id = 0u;
    u32 _parent_event_id = kInvalidFrameEventId;
    u32 _submission_index = 0u;
    u32 _command_index = 0u;
    u16 _sub_event_index = 0u;
    EFrameEventType _type = EFrameEventType::kDraw;
    EFrameEventExecutionResult _execution_result = EFrameEventExecutionResult::kRecordedOnly;
    CaptureStringId _name = 0u;
    u32 _payload_index = 0u;
};

struct DrawEventCapture
{
    CaptureObjectId _material_id = 0u;
    CaptureObjectId _shader_id = 0u;
    CaptureObjectId _pso_id = 0u;
    CaptureObjectId _vertex_buffer_id = 0u;
    CaptureObjectId _index_buffer_id = 0u;
    CaptureObjectId _argument_buffer_id = 0u;
    u16 _pass_index = 0u;
    u16 _sub_mesh = 0u;
    u64 _variant_hash = 0u;
    u32 _vertex_count = 0u;
    u32 _index_count = 0u;
    u32 _index_start = 0u;
    u32 _instance_count = 0u;
    u32 _start_instance = 0u;
    u32 _argument_offset = 0u;
    bool _is_indexed = false;
    bool _is_indirect = false;
    bool _is_procedural = false;
    u32 _pipeline_state_index = 0u;
    u32 _render_target_state_index = 0u;
    u32 _binding_range_begin = 0u;
    u16 _binding_count = 0u;
    u32 _barrier_range_begin = 0u;
    u16 _barrier_count = 0u;
    u32 _pso_dirty_reasons = 0u;
    u8 _pso_lookup_result = 0u;
    u8 _pso_bind_reason = 0u;
    u32 _material_binding_invalid_reasons = 0u;
    u8 _material_binding_result = 0u;
};

struct RenderGraphPassCapture
{
    CaptureStringId _name = 0u;
    u8 _pass_type = 0u;
    u32 _submission_index = 0u;
    bool _allow_parallel_recording = true;
    u32 _input_range_begin = 0u;
    u16 _input_count = 0u;
    u32 _output_range_begin = 0u;
    u16 _output_count = 0u;
    u32 _pre_barrier_range_begin = 0u;
    u16 _pre_barrier_count = 0u;
    u32 _post_barrier_range_begin = 0u;
    u16 _post_barrier_count = 0u;
};

enum class EResourceUsage : u32;
enum class ELoadStoreAction : u8;

struct RenderGraphResourceAccessCapture
{
    CaptureObjectId _resource_id = 0u;
    CaptureStringId _resource_name = 0u;
    u32 _handle_id = 0u;
    u32 _handle_version = 0u;
    u32 _usage = 0u;
    u8 _load_action = 0u;
    u8 _store_action = 0u;
    u32 _mip_level = 0u;
    u32 _mip_count = 1u;
    u32 _array_slice = 0u;
    u32 _array_slice_count = 1u;
    bool _all_sub_resources = true;
};

struct PipelineBindingKey
{
    CaptureObjectId _resource_id = 0u;
    u32 _resource_type = 0u;
    u64 _gpu_handle = 0u;
    u64 _native_resource = 0u;
    u32 _view_index = 0u;
    u32 _sub_resource = 0u;
    u16 _slot = 0u;
    u16 _descriptor_heap_id = 0u;
};

struct PipelineBindingCapture
{
    u16 _slot = 0u;
    CaptureStringId _slot_name = 0u;
    u32 _shader_resource_type = 0u;
    PipelineBindingKey _key;
    u8 _cache_result = 0u;
    u32 _invalid_reasons = 0u;
    u8 _source = 0u;
    u32 _inherited_from_event = kInvalidFrameEventId;
    u16 _priority = 0u;
    bool _is_required = false;
};

struct GeometryBindingCapture
{
    CaptureObjectId _vertex_buffer_id = 0u;
    CaptureObjectId _index_buffer_id = 0u;
    bool _vertex_buffer_bound = false;
    bool _index_buffer_bound = false;
    u32 _vertex_buffer_reasons = 0u;
    u32 _index_buffer_reasons = 0u;
    u64 _vertex_buffer_view_version = 0u;
    u64 _index_buffer_view_version = 0u;
    u64 _input_layout_id = 0u;
};

struct MaterialBindingCapture
{
    u8 _result = 0u;
    u32 _invalid_reasons = 0u;
    u32 _material_resource_version = 0u;
    u32 _layout_version = 0u;
    u64 _variant_hash = 0u;
    u32 _global_layout_version = 0u;
    u32 _global_binding_version = 0u;
};

struct PsoLookupCapture
{
    u64 _previous_hash_low = 0u;
    u64 _previous_hash_high = 0u;
    u64 _current_hash_low = 0u;
    u64 _current_hash_high = 0u;
    u32 _dirty_reasons = 0u;
    u8 _pso_lookup_result = 0u;
    u8 _pso_bind_reason = 0u;
};

struct ResourceBarrierCapture
{
    CaptureObjectId _resource_id = 0u;
    CaptureStringId _resource_name = 0u;
    u32 _before = 0u;
    u32 _after = 0u;
    u32 _sub_resource = 0xFFFFFFFFu;
    bool _is_uav = false;
    bool _is_reconcile = false;
    bool _is_debugger_internal = false;
};

struct DispatchEventCapture
{
    CaptureObjectId _compute_shader_id = 0u;
    CaptureStringId _kernel_name = 0u;
    u16 _group_num_x = 0u;
    u16 _group_num_y = 0u;
    u16 _group_num_z = 0u;
    bool _is_indirect = false;
    u32 _argument_offset = 0u;
    u32 _binding_range_begin = 0u;
    u16 _binding_count = 0u;
};

enum class EFrameCaptureState : u8
{
    kIdle,
    kArmed,
    kCapturing,
    kFinalizing,
    kReady
};

enum class EFrameCaptureOutputMode : u8
{
    kMetadataOnly,
    kOutputThumbnails,
    kFullOutputHistory
};

struct FrameCaptureOptions
{
    EFrameCaptureOutputMode _output_mode = EFrameCaptureOutputMode::kMetadataOnly;
    u64 _memory_budget = 256ull * 1024ull * 1024ull;
    u16 _thumbnail_width = 320u;
    u16 _thumbnail_height = 180u;
    bool _capture_resource_names = true;
    bool _capture_binding_reasons = true;
    bool _capture_barriers = true;
};

enum class EFrameEventFilter : u32
{
    kNone = 0u,
    kDraw = 1u << 0u,
    kDispatch = 1u << 1u,
    kBarrier = 1u << 2u,
    kSkipped = 1u << 3u,
    kPsoDirty = 1u << 4u,
    kPsoCacheMiss = 1u << 5u,
    kMaterialCacheMiss = 1u << 6u,
    kRootSlotBind = 1u << 7u,
    kInheritedBinding = 1u << 8u,
    kWarnings = 1u << 9u
};

constexpr u32 operator|(EFrameEventFilter a, EFrameEventFilter b) { return (u32)a | (u32)b; }
constexpr u32 operator|(u32 a, EFrameEventFilter b) { return a | (u32)b; }
constexpr bool operator&(u32 a, EFrameEventFilter b) { return (a & (u32)b) != 0u; }

struct FrameCaptureStatistics
{
    u32 _capture_revision = 0u;
    u64 _capture_cpu_memory = 0u;
    u64 _capture_gpu_memory = 0u;
    u32 _event_count = 0u;
    u32 _draw_count = 0u;
    u32 _dispatch_count = 0u;
    u32 _barrier_count = 0u;
    u32 _pso_dirty_event_count = 0u;
    u32 _pso_miss_event_count = 0u;
    u32 _material_cache_miss_event_count = 0u;
    u32 _inherited_binding_count = 0u;
    u32 _warning_count = 0u;
    bool _is_truncated = false;
};

struct CaptureGraphicsStateCache
{
    PipelineBindingKey _slot_keys[32];
    u32 _slot_last_event_ids[32]{};
    u32 _valid_slot_mask = 0u;
    u32 _pso_invalidated_slot_mask = 0u;
    u32 _command_list_reset_slot_mask = 0u;
    u64 _last_pso_capture_id = 0u;
    bool _pso_ever_bound = false;
    u32 _cache_reset_generation = 0u;
};

} // namespace Ailu::Render::FrameDebugger
