#include "pch.h"

#include <algorithm>
#include <cstring>

#include "Framework/Common/Utils.h"
#include "Render/ShaderArtifact.h"

namespace Ailu::Render
{
    namespace
    {
        constexpr u64 kFnvOffset = 14695981039346656037ull;
        constexpr u64 kFnvPrime = 1099511628211ull;

        void HashByte(u64 &hash, u8 value)
        {
            hash ^= value;
            hash *= kFnvPrime;
        }

        void HashBytes(u64 &hash, const void *data, size_t size)
        {
            const auto *bytes = static_cast<const u8 *>(data);
            for (size_t index = 0u; index < size; ++index)
                HashByte(hash, bytes[index]);
        }

        void HashString(u64 &hash, const String &value)
        {
            const u32 size = static_cast<u32>(value.size());
            HashBytes(hash, &size, sizeof(size));
            HashBytes(hash, value.data(), value.size());
        }

        void HashU64(u64 &hash, u64 value)
        {
            HashBytes(hash, &value, sizeof(value));
        }
    }

    const char *ShaderArtifactInvalidReasonToString(EShaderArtifactInvalidReason reason)
    {
        switch (reason)
        {
            case EShaderArtifactInvalidReason::kNone: return "None";
            case EShaderArtifactInvalidReason::kNotFound: return "NotFound";
            case EShaderArtifactInvalidReason::kInvalidHeader: return "InvalidHeader";
            case EShaderArtifactInvalidReason::kArtifactVersionChanged: return "ArtifactVersionChanged";
            case EShaderArtifactInvalidReason::kShaderAbiChanged: return "ShaderAbiChanged";
            case EShaderArtifactInvalidReason::kCompilerChanged: return "CompilerChanged";
            case EShaderArtifactInvalidReason::kCompileConfigChanged: return "CompileConfigChanged";
            case EShaderArtifactInvalidReason::kSourceChanged: return "SourceChanged";
            case EShaderArtifactInvalidReason::kDependencyChanged: return "DependencyChanged";
            case EShaderArtifactInvalidReason::kCorrupted: return "Corrupted";
        }
        return "Unknown";
    }

    u64 ShaderArtifactKey::Hash() const
    {
        u64 hash = kFnvOffset;
        HashByte(hash, static_cast<u8>(_type));
        HashString(hash, _source_path);
        HashString(hash, _pass_or_kernel_name);
        HashU64(hash, _pass_or_kernel_index);
        HashU64(hash, _variant_hash);
        HashString(hash, _backend);
        HashU64(hash, _shader_artifact_version);
        HashU64(hash, _shader_abi_version);
        HashU64(hash, _compiler_version);
        HashU64(hash, _compile_config_hash);
        return hash;
    }

    u64 HashCompileConfig(const String &backend, const String &shader_model, EShaderCompileMode compile_mode,
                          const Vector<ShaderMacro> &macros)
    {
        Vector<ShaderMacro> sorted_macros = macros;
        std::sort(sorted_macros.begin(), sorted_macros.end(), [](const ShaderMacro &left, const ShaderMacro &right)
        {
            if (left._name != right._name)
                return left._name < right._name;
            return left._definition < right._definition;
        });

        u64 hash = kFnvOffset;
        HashString(hash, backend);
        HashString(hash, shader_model);
        HashByte(hash, static_cast<u8>(compile_mode));
        for (const auto &macro : sorted_macros)
        {
            HashString(hash, macro._name);
            HashString(hash, macro._definition);
        }
        return hash;
    }

    u64 HashArtifactBytes(const Vector<u8> &bytes)
    {
        u64 hash = kFnvOffset;
        HashBytes(hash, bytes.data(), bytes.size());
        return hash;
    }

    namespace ShaderArtifactSerializer
    {
        namespace
        {
            class Writer
            {
            public:
                template<typename T>
                void Value(const T &value)
                {
                    const auto *data = reinterpret_cast<const u8 *>(&value);
                    _data.insert(_data.end(), data, data + sizeof(T));
                }

                void StringValue(const String &value)
                {
                    const u32 size = static_cast<u32>(value.size());
                    Value(size);
                    const auto *data = reinterpret_cast<const u8 *>(value.data());
                    _data.insert(_data.end(), data, data + value.size());
                }

                void Bytes(const Vector<u8> &value)
                {
                    const u32 size = static_cast<u32>(value.size());
                    Value(size);
                    _data.insert(_data.end(), value.begin(), value.end());
                }

                Vector<u8> _data;
            };

            class Reader
            {
            public:
                explicit Reader(const Vector<u8> &data) : _data(data) {}

                template<typename T>
                bool Value(T &value)
                {
                    if (_offset + sizeof(T) > _data.size())
                        return false;
                    std::memcpy(&value, _data.data() + _offset, sizeof(T));
                    _offset += sizeof(T);
                    return true;
                }

                bool StringValue(String &value)
                {
                    u32 size = 0u;
                    if (!Value(size) || size > _data.size() - _offset)
                        return false;
                    value.assign(reinterpret_cast<const char *>(_data.data() + _offset), size);
                    _offset += size;
                    return true;
                }

                bool Bytes(Vector<u8> &value)
                {
                    u32 size = 0u;
                    if (!Value(size) || size > _data.size() - _offset)
                        return false;
                    value.assign(_data.begin() + _offset, _data.begin() + _offset + size);
                    _offset += size;
                    return true;
                }

                bool IsAtEnd() const { return _offset == _data.size(); }

            private:
                const Vector<u8> &_data;
                size_t _offset = 0u;
            };

            void WriteHeader(Writer &writer, const ShaderArtifactHeader &header)
            {
                writer.Value(header._magic);
                writer.Value(header._version);
                writer.Value(static_cast<u8>(header._type));
                writer.Value(header._artifact_key);
                writer.Value(header._source_fingerprint);
                writer.Value(header._dependency_count);
                writer.Value(header._payload_size);
            }

            bool ReadHeader(Reader &reader, ShaderArtifactHeader &header, EShaderArtifactInvalidReason *reason)
            {
                u8 type = 0u;
                if (!reader.Value(header._magic) || !reader.Value(header._version) || !reader.Value(type) ||
                    !reader.Value(header._artifact_key) || !reader.Value(header._source_fingerprint) ||
                    !reader.Value(header._dependency_count) || !reader.Value(header._payload_size))
                {
                    if (reason)
                        *reason = EShaderArtifactInvalidReason::kCorrupted;
                    return false;
                }
                if (header._magic != ShaderArtifactHeader::kMagic)
                {
                    if (reason)
                        *reason = EShaderArtifactInvalidReason::kInvalidHeader;
                    return false;
                }
                if (header._version != ShaderArtifactHeader::kVersion)
                {
                    if (reason)
                        *reason = EShaderArtifactInvalidReason::kArtifactVersionChanged;
                    return false;
                }
                if (type > static_cast<u8>(EShaderArtifactType::kCompute))
                {
                    if (reason)
                        *reason = EShaderArtifactInvalidReason::kCorrupted;
                    return false;
                }
                header._type = static_cast<EShaderArtifactType>(type);
                return true;
            }

            void WriteDependency(Writer &writer, const ShaderArtifactDependency &dependency)
            {
                writer.StringValue(ToChar(dependency._path));
                writer.Value(dependency._file_size);
                writer.Value(dependency._last_write_time);
                writer.Value(dependency._content_hash);
            }

            bool ReadDependency(Reader &reader, ShaderArtifactDependency &dependency)
            {
                String path;
                if (!reader.StringValue(path) || !reader.Value(dependency._file_size) ||
                    !reader.Value(dependency._last_write_time) || !reader.Value(dependency._content_hash))
                    return false;
                dependency._path = ToWChar(path);
                return true;
            }

            void WriteBinding(Writer &writer, const ShaderResourceBindingArtifact &binding)
            {
                writer.StringValue(binding._name);
                writer.Value(binding._property_id);
                writer.Value(binding._resource_type);
                writer.Value(binding._resource_slot);
                writer.Value(binding._bind_slot);
                writer.Value(binding._register_space);
                writer.Value(binding._buffer_offset);
                writer.Value(binding._buffer_size);
                writer.Value(binding._array_size);
                writer.Value(binding._bind_flag);
                writer.Value(binding._constant_buffer_size);
                writer.StringValue(binding._root_constant_buffer_name);
            }

            bool ReadBinding(Reader &reader, ShaderResourceBindingArtifact &binding)
            {
                return reader.StringValue(binding._name) && reader.Value(binding._property_id) &&
                       reader.Value(binding._resource_type) &&
                       reader.Value(binding._resource_slot) && reader.Value(binding._bind_slot) &&
                       reader.Value(binding._register_space) && reader.Value(binding._buffer_offset) &&
                       reader.Value(binding._buffer_size) && reader.Value(binding._array_size) &&
                       reader.Value(binding._bind_flag) && reader.Value(binding._constant_buffer_size) &&
                       reader.StringValue(binding._root_constant_buffer_name);
            }

            void WriteLayout(Writer &writer, const Vector<VertexBufferLayoutDesc> &layout)
            {
                writer.Value(static_cast<u32>(layout.size()));
                for (const auto &desc : layout)
                {
                    writer.Value(static_cast<u32>(desc._semantic));
                    writer.Value(static_cast<u32>(desc.Type));
                    writer.Value(desc.Size);
                    writer.Value(desc.Offset);
                    writer.Value(desc.Stream);
                }
            }

            bool ReadLayout(Reader &reader, Vector<VertexBufferLayoutDesc> &layout)
            {
                u32 count = 0u;
                if (!reader.Value(count) || count > 64u)
                    return false;
                layout.clear();
                layout.reserve(count);
                for (u32 index = 0u; index < count; ++index)
                {
                    u32 semantic = 0u;
                    u32 type = 0u;
                    u8 size = 0u;
                    u8 offset = 0u;
                    u8 stream = 0u;
                    if (!reader.Value(semantic) || !reader.Value(type) || !reader.Value(size) ||
                        !reader.Value(offset) || !reader.Value(stream))
                        return false;
                    VertexBufferLayoutDesc desc(static_cast<EVertexSemantic>(semantic),
                                                static_cast<EShaderDateType>(type), stream);
                    desc.Size = size;
                    desc.Offset = offset;
                    layout.emplace_back(desc);
                }
                return true;
            }

            void WriteBindings(Writer &writer, const Vector<ShaderResourceBindingArtifact> &bindings)
            {
                writer.Value(static_cast<u32>(bindings.size()));
                for (const auto &binding : bindings)
                    WriteBinding(writer, binding);
            }

            bool ReadBindings(Reader &reader, Vector<ShaderResourceBindingArtifact> &bindings)
            {
                u32 count = 0u;
                if (!reader.Value(count) || count > 4096u)
                    return false;
                bindings.resize(count);
                for (auto &binding : bindings)
                {
                    if (!ReadBinding(reader, binding))
                        return false;
                }
                return true;
            }

            void WriteDependencies(Writer &writer, const Vector<ShaderArtifactDependency> &dependencies)
            {
                writer.Value(static_cast<u32>(dependencies.size()));
                for (const auto &dependency : dependencies)
                    WriteDependency(writer, dependency);
            }

            bool ReadDependencies(Reader &reader, Vector<ShaderArtifactDependency> &dependencies)
            {
                u32 count = 0u;
                if (!reader.Value(count) || count > 4096u)
                    return false;
                dependencies.resize(count);
                for (auto &dependency : dependencies)
                {
                    if (!ReadDependency(reader, dependency))
                        return false;
                }
                return true;
            }

            template<typename Artifact, typename WritePayload>
            bool SerializeImpl(const Artifact &artifact, Vector<u8> &data, WritePayload &&write_payload)
            {
                Writer payload;
                write_payload(payload);
                ShaderArtifactHeader header = artifact._header;
                header._magic = ShaderArtifactHeader::kMagic;
                header._version = ShaderArtifactHeader::kVersion;
                header._payload_size = static_cast<u32>(payload._data.size());
                Writer output;
                WriteHeader(output, header);
                output._data.insert(output._data.end(), payload._data.begin(), payload._data.end());
                data = std::move(output._data);
                return true;
            }

            template<typename Artifact, typename ReadPayload>
            bool DeserializeImpl(const Vector<u8> &data, Artifact &artifact, ReadPayload &&read_payload,
                                  EShaderArtifactInvalidReason *reason)
            {
                if (reason)
                    *reason = EShaderArtifactInvalidReason::kNone;
                Reader reader(data);
                if (!ReadHeader(reader, artifact._header, reason))
                    return false;
                const size_t header_size = sizeof(u32) * 2u + sizeof(u8) + sizeof(u64) * 2u + sizeof(u32) * 2u;
                if (artifact._header._payload_size != data.size() - header_size)
                {
                    if (reason)
                        *reason = EShaderArtifactInvalidReason::kCorrupted;
                    return false;
                }
                if (!read_payload(reader) || !reader.IsAtEnd())
                {
                    if (reason)
                        *reason = EShaderArtifactInvalidReason::kCorrupted;
                    return false;
                }
                return true;
            }
        }

        bool Serialize(const GraphicsShaderVariantArtifact &artifact, Vector<u8> &data)
        {
            return SerializeImpl(artifact, data, [&](Writer &writer)
            {
                writer.Bytes(artifact._vertex_shader._byte_code);
                writer.Bytes(artifact._pixel_shader._byte_code);
                writer.Bytes(artifact._geometry_shader._byte_code);
                WriteLayout(writer, artifact._vertex_input_layout);
                WriteBindings(writer, artifact._bindings);
                writer.Value(artifact._per_material_slot);
                writer.Value(artifact._per_camera_slot);
                writer.Value(artifact._per_scene_slot);
                writer.Bytes(artifact._root_signature_blob);
                writer.Value(artifact._root_signature_hash);
                WriteDependencies(writer, artifact._dependencies);
            });
        }

        bool Serialize(const ComputeShaderVariantArtifact &artifact, Vector<u8> &data)
        {
            return SerializeImpl(artifact, data, [&](Writer &writer)
            {
                writer.Bytes(artifact._compute_shader._byte_code);
                writer.Value(artifact._thread_group_size.x);
                writer.Value(artifact._thread_group_size.y);
                writer.Value(artifact._thread_group_size.z);
                WriteBindings(writer, artifact._bindings);
                writer.Bytes(artifact._root_signature_blob);
                writer.Value(artifact._root_signature_hash);
                WriteDependencies(writer, artifact._dependencies);
            });
        }

        bool Deserialize(const Vector<u8> &data, GraphicsShaderVariantArtifact &artifact,
                         EShaderArtifactInvalidReason *reason)
        {
            return DeserializeImpl(data, artifact, [&](Reader &reader)
            {
                if (artifact._header._type != EShaderArtifactType::kGraphics)
                    return false;
                return reader.Bytes(artifact._vertex_shader._byte_code) && reader.Bytes(artifact._pixel_shader._byte_code) &&
                       reader.Bytes(artifact._geometry_shader._byte_code) && ReadLayout(reader, artifact._vertex_input_layout) &&
                       ReadBindings(reader, artifact._bindings) && reader.Value(artifact._per_material_slot) &&
                       reader.Value(artifact._per_camera_slot) && reader.Value(artifact._per_scene_slot) &&
                       reader.Bytes(artifact._root_signature_blob) && reader.Value(artifact._root_signature_hash) &&
                       ReadDependencies(reader, artifact._dependencies);
            }, reason);
        }

        bool Deserialize(const Vector<u8> &data, ComputeShaderVariantArtifact &artifact,
                         EShaderArtifactInvalidReason *reason)
        {
            return DeserializeImpl(data, artifact, [&](Reader &reader)
            {
                if (artifact._header._type != EShaderArtifactType::kCompute)
                    return false;
                return reader.Bytes(artifact._compute_shader._byte_code) && reader.Value(artifact._thread_group_size.x) &&
                       reader.Value(artifact._thread_group_size.y) && reader.Value(artifact._thread_group_size.z) &&
                       ReadBindings(reader, artifact._bindings) && reader.Bytes(artifact._root_signature_blob) &&
                       reader.Value(artifact._root_signature_hash) && ReadDependencies(reader, artifact._dependencies);
            }, reason);
        }
    }
}
