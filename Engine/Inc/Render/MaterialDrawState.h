#pragma once
#ifndef __MATERIAL_DRAW_STATE_H__
#define __MATERIAL_DRAW_STATE_H__

#include "CoreType.h"
#include "GpuResource.h"
#include "PipelineState.h"
#include <type_traits>

namespace Ailu::Render
{
    class Shader;

    struct PipelineBindingSnapshotEntry
    {
        GpuResourceHandle _resource;
        EBindResDescType _resource_type = EBindResDescType::kUnknown;
        u16 _slot = 0u;
        u16 _priority = 0u;
        PipelineResource::AddiInfo _addi_info;
    };

    struct CommandResourceBinding
    {
        GpuResource *_resource = nullptr;
        EBindResDescType _resource_type = EBindResDescType::kUnknown;
        PipelineResource::AddiInfo _addi_info;
    };

    struct PipelineBindingSnapshot
    {
        const PipelineBindingSnapshotEntry *_entries = nullptr;
        u16 _entry_count = 0u;
        u16 _max_slot = 0u;
        u32 _binding_mask = 0u;
    };

    struct ComputeBindingSnapshotEntry
    {
        GpuResourceHandle _resource;
        EBindResDescType _resource_type = EBindResDescType::kUnknown;
        u16 _slot = 0u;
        u16 _priority = 0u;
        u16 _face = 0u;
        u16 _mipmap = 0u;
        u16 _slice = 0u;
        u16 _view_index = (u16) -1;
        u32 _sub_res = UINT32_MAX;
        bool _is_internal_cbuf = false;
        PipelineResource::AddiInfo _addi_info;
    };

    struct ComputeDispatchSnapshot
    {
        Array<ComputeBindingSnapshotEntry, 32> _entries{};
        Array<u8, 1024> _cbuf_data{};
        u16 _entry_count = 0u;
        u32 _variant_hash = 0u;
        bool _is_ready = false;
    };

    struct MaterialDrawState
    {
        ShaderHandle _shader;
        u16 _pass_index = 0u;
        u32 _variant_hash = 0u;
        bool _is_ready = false;
        u64 _pipeline_shader_hash = 0u;
        u8 _raster_state_hash = 0u;
        PipelineBindingSnapshot _bindings;
        u32 _material_version = 0u;
        u32 _material_binding_invalid_reasons = 0u;
        u8 _material_binding_result = 0u;
        u32 _binding_layout_version = 0u;
        u32 _global_layout_version = 0u;
        u32 _global_binding_version = 0u;
        ECullMode _cull_mode = ECullMode::kBack;
    };

    static_assert(std::is_trivially_copyable_v<MaterialDrawState>, "MaterialDrawState must stay POD-like for command pool reset.");
}

#endif // __MATERIAL_DRAW_STATE_H__
