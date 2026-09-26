#pragma once

#include <cstdint>

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Map.h"
#include "Render/CoreType.h"
#include "Render/PipelineState.h"

namespace Ailu::Render
{
    enum class EShaderArtifactType : u8
    {
        kGraphics,
        kCompute,
        kRayTracing
    };

    enum class EShaderCompileMode : u8
    {
        kDevelopment,
        kDebug,
        kRelease
    };

    //debug 构建下 shader 恒定携带调试信息（-Zi -Zss -Od -Qembed_debug），artifact 通过 -Qembed_debug 自带调试信息
#ifdef _DEBUG
    inline constexpr EShaderCompileMode kRuntimeShaderCompileMode = EShaderCompileMode::kDebug;
#else
    inline constexpr EShaderCompileMode kRuntimeShaderCompileMode = EShaderCompileMode::kDevelopment;
#endif

    enum class EShaderArtifactInvalidReason : u8
    {
        kNone,
        kNotFound,
        kInvalidHeader,
        kArtifactVersionChanged,
        kShaderAbiChanged,
        kCompilerChanged,
        kCompileConfigChanged,
        kSourceChanged,
        kDependencyChanged,
        kCorrupted
    };

    const char *ShaderArtifactInvalidReasonToString(EShaderArtifactInvalidReason reason);

    inline constexpr u32 kShaderArtifactVersion = 2u;
    inline constexpr u32 kShaderAbiVersion = 2u;
    inline constexpr u32 kDxcCompilerVersion = 1u;

    struct ShaderMacro
    {
        String _name;
        String _definition;
    };

    struct ShaderArtifactKey
    {
        EShaderArtifactType _type = EShaderArtifactType::kGraphics;
        String _source_path;
        String _pass_or_kernel_name;
        u16 _pass_or_kernel_index = 0u;
        u32 _variant_hash = 0u;
        String _backend;
        u32 _shader_artifact_version = kShaderArtifactVersion;
        u32 _shader_abi_version = kShaderAbiVersion;
        u32 _compiler_version = kDxcCompilerVersion;
        u64 _compile_config_hash = 0u;

        u64 Hash() const;
    };

    u64 HashCompileConfig(const String &backend, const String &shader_model, EShaderCompileMode compile_mode,
                          const Vector<ShaderMacro> &macros);
    u64 HashArtifactBytes(const Vector<u8> &bytes);

    struct ShaderArtifactHeader
    {
        static constexpr u32 kMagic = 0x41485341u;
        static constexpr u32 kVersion = kShaderArtifactVersion;

        u32 _magic = kMagic;
        u32 _version = kVersion;
        EShaderArtifactType _type = EShaderArtifactType::kGraphics;
        u64 _artifact_key = 0u;
        u64 _source_fingerprint = 0u;
        u32 _dependency_count = 0u;
        u32 _payload_size = 0u;
    };

    struct ShaderArtifactDependency
    {
        WString _path;
        u64 _file_size = 0u;
        u64 _last_write_time = 0u;
        u64 _content_hash = 0u;
    };

    struct ShaderStageArtifact
    {
        Vector<u8> _byte_code;
    };

    struct ShaderResourceBindingArtifact
    {
        String _name;
        ShaderPropertyId _property_id = kInvalidShaderPropertyId;
        u32 _resource_type = static_cast<u32>(EBindResDescType::kUnknown);
        u32 _resource_slot = 0u;
        u16 _bind_slot = static_cast<u16>(-1);
        u16 _register_space = 0u;
        u32 _buffer_offset = 0u;
        u32 _buffer_size = 0u;
        u16 _array_size = 0u;
        u8 _bind_flag = 0u;
        u16 _constant_buffer_size = 0u;
        String _root_constant_buffer_name;
    };

    struct GraphicsShaderVariantArtifact
    {
        ShaderArtifactHeader _header;
        ShaderStageArtifact _vertex_shader;
        ShaderStageArtifact _pixel_shader;
        ShaderStageArtifact _geometry_shader;
        Vector<VertexBufferLayoutDesc> _vertex_input_layout;
        Vector<ShaderResourceBindingArtifact> _bindings;
        i8 _per_material_slot = -1;
        i8 _per_camera_slot = -1;
        i8 _per_scene_slot = -1;
        Vector<u8> _root_signature_blob;
        u64 _root_signature_hash = 0u;
        Vector<ShaderArtifactDependency> _dependencies;
    };

    struct ComputeShaderVariantArtifact
    {
        ShaderArtifactHeader _header;
        ShaderStageArtifact _compute_shader;
        Vector3UInt _thread_group_size = Vector3UInt::kOne;
        Vector<ShaderResourceBindingArtifact> _bindings;
        Vector<u8> _root_signature_blob;
        u64 _root_signature_hash = 0u;
        Vector<ShaderArtifactDependency> _dependencies;
    };

    struct RayTracingShaderVariantArtifact
    {
        ShaderArtifactHeader _header;
        ShaderStageArtifact _library;
        Vector<u8> _reflection_blob;
        Vector<ShaderArtifactDependency> _dependencies;
    };

    namespace ShaderArtifactSerializer
    {
        bool Serialize(const GraphicsShaderVariantArtifact &artifact, Vector<u8> &data);
        bool Serialize(const RayTracingShaderVariantArtifact &artifact, Vector<u8> &data);
        bool Deserialize(const Vector<u8> &data, RayTracingShaderVariantArtifact &artifact,
                         EShaderArtifactInvalidReason *reason = nullptr);
        bool Serialize(const ComputeShaderVariantArtifact &artifact, Vector<u8> &data);
        bool Deserialize(const Vector<u8> &data, GraphicsShaderVariantArtifact &artifact,
                         EShaderArtifactInvalidReason *reason = nullptr);
        bool Deserialize(const Vector<u8> &data, ComputeShaderVariantArtifact &artifact,
                         EShaderArtifactInvalidReason *reason = nullptr);
    }
}
