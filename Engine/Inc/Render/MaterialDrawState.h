#pragma once
#ifndef __MATERIAL_DRAW_STATE_H__
#define __MATERIAL_DRAW_STATE_H__

#include "CoreType.h"
#include "PipelineState.h"
#include "Framework/Core/Containers/Array.h"
#include <type_traits>

namespace Ailu::Render
{
    class Shader;

    struct MaterialDrawBinding
    {
        GpuResource *_resource = nullptr;
        EBindResDescType _resource_type = EBindResDescType::kUnknown;
        u16 _slot = 0u;
        u16 _priority = 0u;
        PipelineResource::AddiInfo _addi_info;
    };

    struct MaterialDrawState
    {
        Shader *_shader = nullptr;
        const ShaderBindingLayout *_binding_layout = nullptr;
        u16 _pass_index = 0u;
        u32 _variant_hash = 0u;
        bool _is_ready = false;
        u64 _pipeline_shader_hash = 0u;
        u32 _binding_mask = 0u;
        Array<MaterialDrawBinding, 32u> _bindings;
        const u8 *_property_data = nullptr;
        u32 _property_size = 0u;
        u32 _material_version = 0u;
        i16 _material_cbuffer_slot = -1;
        ECullMode _cull_mode = ECullMode::kBack;
    };

    static_assert(std::is_trivially_copyable_v<MaterialDrawState>, "MaterialDrawState must stay POD-like for command pool reset.");
}

#endif // __MATERIAL_DRAW_STATE_H__
