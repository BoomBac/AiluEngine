#include "Framework/Parser/GltfParser.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Utils.h"
#include "pch.h"

#include <Ext/rapidjson/inc/document.h>

using namespace Ailu::Render;

namespace Ailu
{
    namespace
    {
        constexpr u32 kGltfModeTriangles = 4u;
        constexpr u32 kComponentTypeByte = 5120u;
        constexpr u32 kComponentTypeUnsignedByte = 5121u;
        constexpr u32 kComponentTypeShort = 5122u;
        constexpr u32 kComponentTypeUnsignedShort = 5123u;
        constexpr u32 kComponentTypeUnsignedInt = 5125u;
        constexpr u32 kComponentTypeFloat = 5126u;
        constexpr f32 kBoundsPadding = 0.01f;

        using Mat4 = std::array<f32, 16>;

        struct GltfBuffer
        {
            Vector<u8> _data;
            String _uri;
        };

        struct GltfBufferView
        {
            i32 _buffer = -1;
            u64 _byte_offset = 0u;
            u64 _byte_length = 0u;
            u64 _byte_stride = 0u;
        };

        struct GltfAccessor
        {
            i32 _buffer_view = -1;
            u64 _byte_offset = 0u;
            u32 _count = 0u;
            u32 _component_type = 0u;
            String _type;
            bool _normalized = false;
            bool _sparse = false;
        };

        struct GltfImage
        {
            String _uri;
            i32 _buffer_view = -1;
        };

        struct GltfTexture
        {
            i32 _source = -1;
        };

        struct GltfMaterial
        {
            String _name;
            Color _base_color = Color(1.0f);
            f32 _roughness = 1.0f;
            i32 _base_color_texture = -1;
            i32 _normal_texture = -1;
        };

        struct GltfPrimitive
        {
            i32 _position = -1;
            i32 _normal = -1;
            i32 _texcoord0 = -1;
            i32 _indices = -1;
            i32 _material = -1;
            u32 _mode = kGltfModeTriangles;
        };

        struct GltfMeshDef
        {
            String _name;
            Vector<GltfPrimitive> _primitives;
        };

        struct GltfNode
        {
            String _name;
            i32 _mesh = -1;
            i32 _skin = -1;
            Mat4 _local_matrix = Mat4{1.0f, 0.0f, 0.0f, 0.0f,
                                      0.0f, 1.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 1.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f, 1.0f};
            Vector<i32> _children;
        };

        struct GltfScene
        {
            Vector<i32> _nodes;
        };

        struct GltfDocument
        {
            Vector<GltfBuffer> _buffers;
            Vector<GltfBufferView> _buffer_views;
            Vector<GltfAccessor> _accessors;
            Vector<GltfImage> _images;
            Vector<GltfTexture> _textures;
            Vector<GltfMaterial> _materials;
            Vector<GltfMeshDef> _meshes;
            Vector<GltfNode> _nodes;
            Vector<GltfScene> _scenes;
            i32 _scene = -1;
        };

        struct NodeInstance
        {
            i32 _node_index = -1;
            i32 _mesh_index = -1;
            String _name;
            Mat4 _world_matrix = Mat4{1.0f, 0.0f, 0.0f, 0.0f,
                                      0.0f, 1.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 1.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f, 1.0f};
            bool _has_skin = false;
        };

        struct PrimitiveData
        {
            Vector<Vector3f> _positions;
            Vector<Vector3f> _normals;
            Vector<Vector2f> _uv0;
            Vector<u32> _indices;
            Mesh::ImportedMaterialInfo _material;
            AABB _bounds{};
            bool _has_normals = false;
            bool _has_uv0 = false;
        };

        constexpr Mat4 IdentityMatrix()
        {
            return Mat4{1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f};
        }

        bool IsDataUri(const String &uri)
        {
            return StringUtils::BeginWith(StringUtils::ToLower(uri), "data:");
        }

        std::optional<Vector<u8>> DecodeBase64DataUri(const String &uri)
        {
            auto comma = uri.find(',');
            if (comma == String::npos)
                return std::nullopt;

            String header = StringUtils::ToLower(uri.substr(0, comma));
            if (!StringUtils::EndWith(header, ";base64"))
            {
                LOG_WARNING("GltfParser only supports base64 data URIs, skip unsupported payload");
                return std::nullopt;
            }

            const String payload = uri.substr(comma + 1);
            static constexpr i16 kInvalid = -1;
            static constexpr std::array<i16, 256> kDecodeTable = []()
            {
                std::array<i16, 256> table{};
                table.fill(kInvalid);
                for (u8 index = 0; index < 26; ++index)
                {
                    table[static_cast<u8>('A' + index)] = index;
                    table[static_cast<u8>('a' + index)] = index + 26;
                }
                for (u8 index = 0; index < 10; ++index)
                    table[static_cast<u8>('0' + index)] = index + 52;
                table[static_cast<u8>('+')] = 62;
                table[static_cast<u8>('/')] = 63;
                return table;
            }();

            Vector<u8> out;
            out.reserve((payload.size() * 3u) / 4u);

            u32 accumulator = 0u;
            i32 bits_collected = 0;
            for (char c: payload)
            {
                if (c == '=')
                    break;
                if (std::isspace(static_cast<unsigned char>(c)))
                    continue;

                i16 value = kDecodeTable[static_cast<u8>(c)];
                if (value == kInvalid)
                {
                    LOG_WARNING("Invalid base64 payload found in glTF data URI");
                    return std::nullopt;
                }

                accumulator = (accumulator << 6u) | static_cast<u32>(value);
                bits_collected += 6;
                if (bits_collected >= 8)
                {
                    bits_collected -= 8;
                    out.emplace_back(static_cast<u8>((accumulator >> bits_collected) & 0xffu));
                }
            }
            return out;
        }

        WString ResolveDependencyPath(const WString &gltf_sys_path, const String &uri)
        {
            fs::path base_dir = fs::path(gltf_sys_path).parent_path();
            fs::path resolved = (base_dir / fs::path(ToWChar(uri))).lexically_normal();
            return PathUtils::FormatFilePath(resolved.wstring());
        }

        Mat4 Multiply(const Mat4 &lhs, const Mat4 &rhs)
        {
            Mat4 result{};
            for (u32 col = 0u; col < 4u; ++col)
            {
                for (u32 row = 0u; row < 4u; ++row)
                {
                    f32 value = 0.0f;
                    for (u32 index = 0u; index < 4u; ++index)
                        value += lhs[index * 4u + row] * rhs[col * 4u + index];
                    result[col * 4u + row] = value;
                }
            }
            return result;
        }

        Mat4 TranslationMatrix(const Vector3f &translation)
        {
            Mat4 result = IdentityMatrix();
            result[12] = translation.x;
            result[13] = translation.y;
            result[14] = translation.z;
            return result;
        }

        Mat4 ScaleMatrix(const Vector3f &scale)
        {
            Mat4 result = IdentityMatrix();
            result[0] = scale.x;
            result[5] = scale.y;
            result[10] = scale.z;
            return result;
        }

        Mat4 RotationMatrix(const Vector4f &rotation)
        {
            const f32 x = rotation.x;
            const f32 y = rotation.y;
            const f32 z = rotation.z;
            const f32 w = rotation.w;

            const f32 xx = x * x;
            const f32 yy = y * y;
            const f32 zz = z * z;
            const f32 xy = x * y;
            const f32 xz = x * z;
            const f32 yz = y * z;
            const f32 wx = w * x;
            const f32 wy = w * y;
            const f32 wz = w * z;

            const f32 m00 = 1.0f - 2.0f * (yy + zz);
            const f32 m01 = 2.0f * (xy - wz);
            const f32 m02 = 2.0f * (xz + wy);
            const f32 m10 = 2.0f * (xy + wz);
            const f32 m11 = 1.0f - 2.0f * (xx + zz);
            const f32 m12 = 2.0f * (yz - wx);
            const f32 m20 = 2.0f * (xz - wy);
            const f32 m21 = 2.0f * (yz + wx);
            const f32 m22 = 1.0f - 2.0f * (xx + yy);

            return Mat4{m00, m10, m20, 0.0f,
                        m01, m11, m21, 0.0f,
                        m02, m12, m22, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f};
        }

        Mat4 ComposeNodeMatrix(const rapidjson::Value &node)
        {
            if (node.HasMember("matrix") && node["matrix"].IsArray() && node["matrix"].Size() == 16u)
            {
                Mat4 result{};
                for (u32 index = 0u; index < 16u; ++index)
                    result[index] = static_cast<f32>(node["matrix"][index].GetDouble());
                return result;
            }

            Vector3f translation = Vector3f::kZero;
            Vector3f scale = Vector3f{1.0f, 1.0f, 1.0f};
            Vector4f rotation = Vector4f{0.0f, 0.0f, 0.0f, 1.0f};

            if (node.HasMember("translation") && node["translation"].IsArray() && node["translation"].Size() == 3u)
            {
                translation = Vector3f{static_cast<f32>(node["translation"][0].GetDouble()),
                                       static_cast<f32>(node["translation"][1].GetDouble()),
                                       static_cast<f32>(node["translation"][2].GetDouble())};
            }
            if (node.HasMember("scale") && node["scale"].IsArray() && node["scale"].Size() == 3u)
            {
                scale = Vector3f{static_cast<f32>(node["scale"][0].GetDouble()),
                                 static_cast<f32>(node["scale"][1].GetDouble()),
                                 static_cast<f32>(node["scale"][2].GetDouble())};
            }
            if (node.HasMember("rotation") && node["rotation"].IsArray() && node["rotation"].Size() == 4u)
            {
                rotation = Vector4f{static_cast<f32>(node["rotation"][0].GetDouble()),
                                    static_cast<f32>(node["rotation"][1].GetDouble()),
                                    static_cast<f32>(node["rotation"][2].GetDouble()),
                                    static_cast<f32>(node["rotation"][3].GetDouble())};
            }

            return Multiply(TranslationMatrix(translation), Multiply(RotationMatrix(rotation), ScaleMatrix(scale)));
        }

        Vector3f TransformPoint(const Mat4 &matrix, const Vector3f &value)
        {
            return Vector3f{
                    matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12],
                    matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13],
                    matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14]};
        }

        Vector3f TransformDirection(const Mat4 &matrix, const Vector3f &value)
        {
            return Vector3f{
                    matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z,
                    matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z,
                    matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z};
        }

        Vector3f TransformNormal(const Mat4 &matrix, const Vector3f &value)
        {
            const f32 a00 = matrix[0];
            const f32 a01 = matrix[4];
            const f32 a02 = matrix[8];
            const f32 a10 = matrix[1];
            const f32 a11 = matrix[5];
            const f32 a12 = matrix[9];
            const f32 a20 = matrix[2];
            const f32 a21 = matrix[6];
            const f32 a22 = matrix[10];

            const f32 c00 = a11 * a22 - a12 * a21;
            const f32 c01 = a12 * a20 - a10 * a22;
            const f32 c02 = a10 * a21 - a11 * a20;
            const f32 c10 = a02 * a21 - a01 * a22;
            const f32 c11 = a00 * a22 - a02 * a20;
            const f32 c12 = a01 * a20 - a00 * a21;
            const f32 c20 = a01 * a12 - a02 * a11;
            const f32 c21 = a02 * a10 - a00 * a12;
            const f32 c22 = a00 * a11 - a01 * a10;

            const f32 determinant = a00 * c00 + a01 * c01 + a02 * c02;
            if (std::abs(determinant) <= Math::kFloatEpsilon)
                return Normalize(TransformDirection(matrix, value));

            const f32 inv_det = 1.0f / determinant;
            return Normalize(Vector3f{
                    (c00 * value.x + c01 * value.y + c02 * value.z) * inv_det,
                    (c10 * value.x + c11 * value.y + c12 * value.z) * inv_det,
                    (c20 * value.x + c21 * value.y + c22 * value.z) * inv_det});
        }

        Vector3f ToEnginePosition(const Vector3f &value)
        {
            return Vector3f{value.x, value.y, -value.z};
        }

        Vector3f ToEngineDirection(const Vector3f &value)
        {
            return Normalize(Vector3f{value.x, value.y, -value.z});
        }

        u32 ComponentCount(const String &type)
        {
            if (type == "SCALAR") return 1u;
            if (type == "VEC2") return 2u;
            if (type == "VEC3") return 3u;
            if (type == "VEC4") return 4u;
            if (type == "MAT4") return 16u;
            return 0u;
        }

        u64 ComponentSize(u32 component_type)
        {
            switch (component_type)
            {
                case kComponentTypeByte:
                case kComponentTypeUnsignedByte:
                    return 1u;
                case kComponentTypeShort:
                case kComponentTypeUnsignedShort:
                    return 2u;
                case kComponentTypeUnsignedInt:
                case kComponentTypeFloat:
                    return 4u;
                default:
                    return 0u;
            }
        }

        template<typename T>
        T ReadScalar(const u8 *data)
        {
            T value{};
            memcpy(&value, data, sizeof(T));
            return value;
        }

        f32 ReadComponentAsFloat(const u8 *data, u32 component_type, bool normalized)
        {
            switch (component_type)
            {
                case kComponentTypeByte:
                {
                    const i8 value = ReadScalar<i8>(data);
                    if (!normalized)
                        return static_cast<f32>(value);
                    return std::max(static_cast<f32>(value) / 127.0f, -1.0f);
                }
                case kComponentTypeUnsignedByte:
                {
                    const u8 value = ReadScalar<u8>(data);
                    return normalized ? static_cast<f32>(value) / 255.0f : static_cast<f32>(value);
                }
                case kComponentTypeShort:
                {
                    const i16 value = ReadScalar<i16>(data);
                    if (!normalized)
                        return static_cast<f32>(value);
                    return std::max(static_cast<f32>(value) / 32767.0f, -1.0f);
                }
                case kComponentTypeUnsignedShort:
                {
                    const u16 value = ReadScalar<u16>(data);
                    return normalized ? static_cast<f32>(value) / 65535.0f : static_cast<f32>(value);
                }
                case kComponentTypeUnsignedInt:
                    return static_cast<f32>(ReadScalar<u32>(data));
                case kComponentTypeFloat:
                    return ReadScalar<f32>(data);
                default:
                    return 0.0f;
            }
        }

        template<size_t ComponentNum>
        bool ReadFloatAccessor(const GltfDocument &document, i32 accessor_index, const char *expected_type, Vector<std::array<f32, ComponentNum>> &out_values)
        {
            if (accessor_index < 0 || accessor_index >= static_cast<i32>(document._accessors.size()))
                return false;

            const auto &accessor = document._accessors[accessor_index];
            if (accessor._sparse)
            {
                LOG_WARNING("Sparse glTF accessors are not supported yet");
                return false;
            }
            if (accessor._buffer_view < 0 || accessor._buffer_view >= static_cast<i32>(document._buffer_views.size()))
                return false;
            if (accessor._type != expected_type)
                return false;

            const auto &view = document._buffer_views[accessor._buffer_view];
            if (view._buffer < 0 || view._buffer >= static_cast<i32>(document._buffers.size()))
                return false;

            const auto &buffer = document._buffers[view._buffer];
            const u64 component_size = ComponentSize(accessor._component_type);
            if (component_size == 0u)
                return false;

            const u64 element_size = component_size * ComponentNum;
            const u64 stride = view._byte_stride == 0u ? element_size : view._byte_stride;
            const u64 start = view._byte_offset + accessor._byte_offset;
            const u64 required_size = accessor._count == 0u ? 0u : start + stride * (static_cast<u64>(accessor._count) - 1u) + element_size;
            if (required_size > buffer._data.size())
            {
                LOG_ERROR("glTF accessor range is out of bounds");
                return false;
            }

            out_values.resize(accessor._count);
            for (u32 element_index = 0u; element_index < accessor._count; ++element_index)
            {
                const u8 *element = buffer._data.data() + start + stride * element_index;
                for (size_t component_index = 0u; component_index < ComponentNum; ++component_index)
                {
                    out_values[element_index][component_index] = ReadComponentAsFloat(
                            element + component_index * component_size,
                            accessor._component_type,
                            accessor._normalized);
                }
            }
            return true;
        }

        bool ReadIndexAccessor(const GltfDocument &document, i32 accessor_index, Vector<u32> &out_indices)
        {
            if (accessor_index < 0 || accessor_index >= static_cast<i32>(document._accessors.size()))
                return false;

            const auto &accessor = document._accessors[accessor_index];
            if (accessor._sparse)
            {
                LOG_WARNING("Sparse glTF index accessors are not supported yet");
                return false;
            }
            if (accessor._buffer_view < 0 || accessor._buffer_view >= static_cast<i32>(document._buffer_views.size()))
                return false;
            if (accessor._type != "SCALAR")
                return false;

            const auto &view = document._buffer_views[accessor._buffer_view];
            if (view._buffer < 0 || view._buffer >= static_cast<i32>(document._buffers.size()))
                return false;

            const auto &buffer = document._buffers[view._buffer];
            const u64 component_size = ComponentSize(accessor._component_type);
            const u64 stride = view._byte_stride == 0u ? component_size : view._byte_stride;
            const u64 start = view._byte_offset + accessor._byte_offset;
            const u64 required_size = accessor._count == 0u ? 0u : start + stride * (static_cast<u64>(accessor._count) - 1u) + component_size;
            if (required_size > buffer._data.size())
            {
                LOG_ERROR("glTF index accessor range is out of bounds");
                return false;
            }

            out_indices.resize(accessor._count);
            for (u32 element_index = 0u; element_index < accessor._count; ++element_index)
            {
                const u8 *element = buffer._data.data() + start + stride * element_index;
                switch (accessor._component_type)
                {
                    case kComponentTypeUnsignedByte:
                        out_indices[element_index] = ReadScalar<u8>(element);
                        break;
                    case kComponentTypeUnsignedShort:
                        out_indices[element_index] = ReadScalar<u16>(element);
                        break;
                    case kComponentTypeUnsignedInt:
                        out_indices[element_index] = ReadScalar<u32>(element);
                        break;
                    default:
                        LOG_WARNING("Unsupported glTF index component type {}", accessor._component_type);
                        return false;
                }
            }
            return true;
        }

        void CalculateNormals(const Vector<Vector3f> &positions, const Vector<Vector<u32>> &submesh_indices, Vector<Vector3f> &normals)
        {
            normals.assign(positions.size(), Vector3f::kZero);
            for (const auto &indices: submesh_indices)
            {
                for (size_t index = 0u; index + 2u < indices.size(); index += 3u)
                {
                    const u32 i0 = indices[index + 0u];
                    const u32 i1 = indices[index + 1u];
                    const u32 i2 = indices[index + 2u];
                    if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                        continue;

                    const Vector3f edge1 = positions[i1] - positions[i0];
                    const Vector3f edge2 = positions[i2] - positions[i0];
                    const Vector3f face_normal = Normalize(CrossProduct(edge1, edge2));
                    normals[i0] += face_normal;
                    normals[i1] += face_normal;
                    normals[i2] += face_normal;
                }
            }

            for (auto &normal: normals)
            {
                if (DotProduct(normal, normal) > Math::kFloatEpsilon)
                    normal = Normalize(normal);
                else
                    normal = Vector3f::kUp;
            }
        }

        String ResolveTexturePath(const GltfDocument &document, i32 texture_index, const WString &gltf_sys_path)
        {
            if (texture_index < 0 || texture_index >= static_cast<i32>(document._textures.size()))
                return {};

            const auto &texture = document._textures[texture_index];
            if (texture._source < 0 || texture._source >= static_cast<i32>(document._images.size()))
                return {};

            const auto &image = document._images[texture._source];
            if (image._uri.empty())
            {
                if (image._buffer_view >= 0)
                    LOG_WARNING("Embedded glTF image bufferViews are not exported as external texture assets yet");
                return {};
            }
            if (IsDataUri(image._uri))
            {
                LOG_WARNING("glTF data URI textures are not exported as external texture assets yet");
                return {};
            }

            return ToChar(ResolveDependencyPath(gltf_sys_path, image._uri));
        }

        Mesh::ImportedMaterialInfo BuildMaterialInfo(
                const GltfDocument &document,
                const GltfPrimitive &primitive,
                u16 slot,
                const String &mesh_name,
                const WString &gltf_sys_path)
        {
            Mesh::ImportedMaterialInfo info(slot, std::format("{}_{}", mesh_name, slot));
            if (primitive._material < 0 || primitive._material >= static_cast<i32>(document._materials.size()))
                return info;

            const auto &material = document._materials[primitive._material];
            if (!material._name.empty())
                info._name = material._name;
            info._diffuse = material._base_color;
            info._roughness = material._roughness;
            info._textures[0] = ResolveTexturePath(document, material._base_color_texture, gltf_sys_path);
            info._textures[1] = ResolveTexturePath(document, material._normal_texture, gltf_sys_path);
            return info;
        }

        bool LoadBufferData(const WString &gltf_sys_path, const rapidjson::Value &buffer_value, GltfBuffer &out_buffer)
        {
            if (!buffer_value.HasMember("uri") || !buffer_value["uri"].IsString())
            {
                LOG_ERROR("glTF buffer without uri is unsupported, .glb is not handled by GltfParser");
                return false;
            }

            out_buffer._uri = buffer_value["uri"].GetString();
            if (IsDataUri(out_buffer._uri))
            {
                auto decoded = DecodeBase64DataUri(out_buffer._uri);
                if (!decoded.has_value())
                    return false;
                out_buffer._data = std::move(decoded.value());
                return true;
            }

            const WString dependency_path = ResolveDependencyPath(gltf_sys_path, out_buffer._uri);
            auto [file_data, file_size] = FileManager::ReadFile(dependency_path);
            if (file_data == nullptr)
            {
                LOG_ERROR(L"Failed to read glTF buffer {}", dependency_path);
                return false;
            }

            out_buffer._data.assign(file_data, file_data + file_size);
            AL_FREE(file_data);
            return true;
        }

        bool LoadGltfDocument(const WString &sys_path, GltfDocument &out_document)
        {
            String json_text;
            if (!FileManager::ReadFile(sys_path, json_text))
            {
                LOG_ERROR(L"Failed to read glTF file {}", sys_path);
                return false;
            }

            rapidjson::Document document;
            document.Parse(json_text.c_str());
            if (document.HasParseError() || !document.IsObject())
            {
                LOG_ERROR(L"Failed to parse glTF file {}", sys_path);
                return false;
            }

            if (document.HasMember("scene") && document["scene"].IsInt())
                out_document._scene = document["scene"].GetInt();

            if (document.HasMember("buffers") && document["buffers"].IsArray())
            {
                for (const auto &buffer_value: document["buffers"].GetArray())
                {
                    GltfBuffer buffer;
                    if (!LoadBufferData(sys_path, buffer_value, buffer))
                        return false;
                    out_document._buffers.emplace_back(std::move(buffer));
                }
            }

            if (document.HasMember("bufferViews") && document["bufferViews"].IsArray())
            {
                for (const auto &view_value: document["bufferViews"].GetArray())
                {
                    GltfBufferView view;
                    if (view_value.HasMember("buffer") && view_value["buffer"].IsInt())
                        view._buffer = view_value["buffer"].GetInt();
                    if (view_value.HasMember("byteOffset") && view_value["byteOffset"].IsUint64())
                        view._byte_offset = view_value["byteOffset"].GetUint64();
                    if (view_value.HasMember("byteLength") && view_value["byteLength"].IsUint64())
                        view._byte_length = view_value["byteLength"].GetUint64();
                    if (view_value.HasMember("byteStride") && view_value["byteStride"].IsUint64())
                        view._byte_stride = view_value["byteStride"].GetUint64();
                    out_document._buffer_views.emplace_back(view);
                }
            }

            if (document.HasMember("accessors") && document["accessors"].IsArray())
            {
                for (const auto &accessor_value: document["accessors"].GetArray())
                {
                    GltfAccessor accessor;
                    if (accessor_value.HasMember("bufferView") && accessor_value["bufferView"].IsInt())
                        accessor._buffer_view = accessor_value["bufferView"].GetInt();
                    if (accessor_value.HasMember("byteOffset") && accessor_value["byteOffset"].IsUint64())
                        accessor._byte_offset = accessor_value["byteOffset"].GetUint64();
                    if (accessor_value.HasMember("count") && accessor_value["count"].IsUint())
                        accessor._count = accessor_value["count"].GetUint();
                    if (accessor_value.HasMember("componentType") && accessor_value["componentType"].IsUint())
                        accessor._component_type = accessor_value["componentType"].GetUint();
                    if (accessor_value.HasMember("type") && accessor_value["type"].IsString())
                        accessor._type = accessor_value["type"].GetString();
                    if (accessor_value.HasMember("normalized") && accessor_value["normalized"].IsBool())
                        accessor._normalized = accessor_value["normalized"].GetBool();
                    accessor._sparse = accessor_value.HasMember("sparse");
                    out_document._accessors.emplace_back(accessor);
                }
            }

            if (document.HasMember("images") && document["images"].IsArray())
            {
                for (const auto &image_value: document["images"].GetArray())
                {
                    GltfImage image;
                    if (image_value.HasMember("uri") && image_value["uri"].IsString())
                        image._uri = image_value["uri"].GetString();
                    if (image_value.HasMember("bufferView") && image_value["bufferView"].IsInt())
                        image._buffer_view = image_value["bufferView"].GetInt();
                    out_document._images.emplace_back(std::move(image));
                }
            }

            if (document.HasMember("textures") && document["textures"].IsArray())
            {
                for (const auto &texture_value: document["textures"].GetArray())
                {
                    GltfTexture texture;
                    if (texture_value.HasMember("source") && texture_value["source"].IsInt())
                        texture._source = texture_value["source"].GetInt();
                    out_document._textures.emplace_back(texture);
                }
            }

            if (document.HasMember("materials") && document["materials"].IsArray())
            {
                for (const auto &material_value: document["materials"].GetArray())
                {
                    GltfMaterial material;
                    if (material_value.HasMember("name") && material_value["name"].IsString())
                        material._name = material_value["name"].GetString();

                    if (material_value.HasMember("pbrMetallicRoughness") && material_value["pbrMetallicRoughness"].IsObject())
                    {
                        const auto &pbr = material_value["pbrMetallicRoughness"];
                        if (pbr.HasMember("baseColorFactor") && pbr["baseColorFactor"].IsArray() && pbr["baseColorFactor"].Size() >= 3u)
                        {
                            f32 r = static_cast<f32>(pbr["baseColorFactor"][0].GetDouble());
                            f32 g = static_cast<f32>(pbr["baseColorFactor"][1].GetDouble());
                            f32 b = static_cast<f32>(pbr["baseColorFactor"][2].GetDouble());
                            f32 a = pbr["baseColorFactor"].Size() > 3u ? static_cast<f32>(pbr["baseColorFactor"][3].GetDouble()) : 1.0f;
                            material._base_color = Color(r, g, b, a);
                        }
                        if (pbr.HasMember("roughnessFactor") && pbr["roughnessFactor"].IsNumber())
                            material._roughness = static_cast<f32>(pbr["roughnessFactor"].GetDouble());
                        if (pbr.HasMember("baseColorTexture") && pbr["baseColorTexture"].IsObject())
                        {
                            const auto &texture = pbr["baseColorTexture"];
                            if (texture.HasMember("index") && texture["index"].IsInt())
                                material._base_color_texture = texture["index"].GetInt();
                        }
                    }

                    if (material_value.HasMember("normalTexture") && material_value["normalTexture"].IsObject())
                    {
                        const auto &texture = material_value["normalTexture"];
                        if (texture.HasMember("index") && texture["index"].IsInt())
                            material._normal_texture = texture["index"].GetInt();
                    }

                    out_document._materials.emplace_back(std::move(material));
                }
            }

            if (document.HasMember("meshes") && document["meshes"].IsArray())
            {
                for (const auto &mesh_value: document["meshes"].GetArray())
                {
                    GltfMeshDef mesh;
                    if (mesh_value.HasMember("name") && mesh_value["name"].IsString())
                        mesh._name = mesh_value["name"].GetString();
                    if (mesh_value.HasMember("primitives") && mesh_value["primitives"].IsArray())
                    {
                        for (const auto &primitive_value: mesh_value["primitives"].GetArray())
                        {
                            GltfPrimitive primitive;
                            if (primitive_value.HasMember("mode") && primitive_value["mode"].IsUint())
                                primitive._mode = primitive_value["mode"].GetUint();
                            if (primitive_value.HasMember("indices") && primitive_value["indices"].IsInt())
                                primitive._indices = primitive_value["indices"].GetInt();
                            if (primitive_value.HasMember("material") && primitive_value["material"].IsInt())
                                primitive._material = primitive_value["material"].GetInt();
                            if (primitive_value.HasMember("attributes") && primitive_value["attributes"].IsObject())
                            {
                                const auto &attributes = primitive_value["attributes"];
                                if (attributes.HasMember("POSITION") && attributes["POSITION"].IsInt())
                                    primitive._position = attributes["POSITION"].GetInt();
                                if (attributes.HasMember("NORMAL") && attributes["NORMAL"].IsInt())
                                    primitive._normal = attributes["NORMAL"].GetInt();
                                if (attributes.HasMember("TEXCOORD_0") && attributes["TEXCOORD_0"].IsInt())
                                    primitive._texcoord0 = attributes["TEXCOORD_0"].GetInt();
                            }
                            mesh._primitives.emplace_back(primitive);
                        }
                    }
                    out_document._meshes.emplace_back(std::move(mesh));
                }
            }

            if (document.HasMember("nodes") && document["nodes"].IsArray())
            {
                for (const auto &node_value: document["nodes"].GetArray())
                {
                    GltfNode node;
                    if (node_value.HasMember("name") && node_value["name"].IsString())
                        node._name = node_value["name"].GetString();
                    if (node_value.HasMember("mesh") && node_value["mesh"].IsInt())
                        node._mesh = node_value["mesh"].GetInt();
                    if (node_value.HasMember("skin") && node_value["skin"].IsInt())
                        node._skin = node_value["skin"].GetInt();
                    node._local_matrix = ComposeNodeMatrix(node_value);
                    if (node_value.HasMember("children") && node_value["children"].IsArray())
                    {
                        for (const auto &child_value: node_value["children"].GetArray())
                        {
                            if (child_value.IsInt())
                                node._children.emplace_back(child_value.GetInt());
                        }
                    }
                    out_document._nodes.emplace_back(std::move(node));
                }
            }

            if (document.HasMember("scenes") && document["scenes"].IsArray())
            {
                for (const auto &scene_value: document["scenes"].GetArray())
                {
                    GltfScene scene;
                    if (scene_value.HasMember("nodes") && scene_value["nodes"].IsArray())
                    {
                        for (const auto &node_value: scene_value["nodes"].GetArray())
                        {
                            if (node_value.IsInt())
                                scene._nodes.emplace_back(node_value.GetInt());
                        }
                    }
                    out_document._scenes.emplace_back(std::move(scene));
                }
            }

            return true;
        }

        void CollectNodeInstancesRecursive(const GltfDocument &document, i32 node_index, const Mat4 &parent_matrix, Vector<NodeInstance> &out_instances)
        {
            if (node_index < 0 || node_index >= static_cast<i32>(document._nodes.size()))
                return;

            const auto &node = document._nodes[node_index];
            const Mat4 world_matrix = Multiply(parent_matrix, node._local_matrix);
            if (node._mesh >= 0 && node._mesh < static_cast<i32>(document._meshes.size()))
            {
                String name = node._name;
                if (name.empty())
                    name = document._meshes[node._mesh]._name;
                out_instances.emplace_back(NodeInstance{node_index, node._mesh, std::move(name), world_matrix, node._skin >= 0});
            }

            for (i32 child_index: node._children)
                CollectNodeInstancesRecursive(document, child_index, world_matrix, out_instances);
        }
    }// namespace

    Vector<String> GltfParser::CollectExternalDependencyUris(const WString &sys_path)
    {
        String json_text;
        if (!FileManager::ReadFile(sys_path, json_text))
            return {};

        rapidjson::Document document;
        document.Parse(json_text.c_str());
        if (document.HasParseError() || !document.IsObject())
            return {};

        std::set<String> unique_uris;
        auto collect_uris = [&unique_uris](const rapidjson::Value &array)
        {
            for (const auto &item: array.GetArray())
            {
                if (!item.IsObject() || !item.HasMember("uri") || !item["uri"].IsString())
                    continue;

                const String uri = item["uri"].GetString();
                if (!IsDataUri(uri))
                    unique_uris.emplace(uri);
            }
        };

        if (document.HasMember("buffers") && document["buffers"].IsArray())
            collect_uris(document["buffers"]);
        if (document.HasMember("images") && document["images"].IsArray())
            collect_uris(document["images"]);

        return Vector<String>(unique_uris.begin(), unique_uris.end());
    }

    void GltfParser::Parser(const WString &sys_path, const MeshImportSetting &import_setting)
    {
        std::unique_lock<std::mutex> lock(_parser_lock);
        _import_setting = import_setting;
        _cur_file_sys_path = sys_path;
        _loaded_meshes.clear();
        _loaded_anims.clear();
        ParserImpl(sys_path);
    }

    bool GltfParser::CalculateTangant(Mesh *mesh)
    {
        auto positions = mesh->GetVertices();
        auto uv0 = mesh->GetUVs(0);
        if (positions.empty() || uv0.empty())
            return false;

        Vector<Vector4f> tangents(mesh->GetVertexCount(), Vector4f{0.0f, 0.0f, 0.0f, 1.0f});
        Vector<Vector4f> bitangents(mesh->GetVertexCount(), Vector4f{0.0f, 0.0f, 0.0f, 1.0f});
        for (u16 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
        {
            auto indices = mesh->GetIndices(submesh_index);
            for (size_t index = 0u; index + 2u < indices.size(); index += 3u)
            {
                const u32 i0 = indices[index + 0u];
                const u32 i1 = indices[index + 1u];
                const u32 i2 = indices[index + 2u];
                const Vector3f &v0 = positions[i0];
                const Vector3f &v1 = positions[i1];
                const Vector3f &v2 = positions[i2];
                const Vector2f &t0 = uv0[i0];
                const Vector2f &t1 = uv0[i1];
                const Vector2f &t2 = uv0[i2];

                const Vector3f edge1 = v1 - v0;
                const Vector3f edge2 = v2 - v0;
                const Vector2f delta_uv1 = t1 - t0;
                const Vector2f delta_uv2 = t2 - t0;
                const f32 factor = 1.0f / ((delta_uv1.x * delta_uv2.y - delta_uv2.x * delta_uv1.y) + 0.0000001f);

                Vector4f tangent{};
                Vector4f bitangent{};
                tangent.x = factor * (delta_uv2.y * edge1.x - delta_uv1.y * edge2.x);
                tangent.y = factor * (delta_uv2.y * edge1.y - delta_uv1.y * edge2.y);
                tangent.z = factor * (delta_uv2.y * edge1.z - delta_uv1.y * edge2.z);
                bitangent.x = factor * (delta_uv1.x * edge2.x - delta_uv2.x * edge1.x);
                bitangent.y = factor * (delta_uv1.x * edge2.y - delta_uv2.x * edge1.y);
                bitangent.z = factor * (delta_uv1.x * edge2.z - delta_uv2.x * edge1.z);

                tangents[i0] = tangent;
                tangents[i1] = tangent;
                tangents[i2] = tangent;
                bitangents[i0] = bitangent;
                bitangents[i1] = bitangent;
                bitangents[i2] = bitangent;
            }
        }

        auto normals = mesh->GetNormals();
        for (size_t index = 0u; index < mesh->_vertex_count; ++index)
        {
            const Vector3f &normal = normals[index];
            const Vector3f &tangent = tangents[index].xyz;
            const Vector3f &bitangent = bitangents[index].xyz;
            tangents[index].xyz = Normalize(tangent - normal * DotProduct(normal, tangent));
            tangents[index].w = DotProduct(CrossProduct(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
        }
        mesh->SetTangents({tangents.data(), tangents.size()});
        return true;
    }

    bool GltfParser::CombineLoadedMeshes()
    {
        if (!_import_setting._is_combine_mesh || _loaded_meshes.size() <= 1u)
            return false;

        Ref<Mesh> combined_mesh = MakeRef<Mesh>(ToChar(PathUtils::GetFileName(_cur_file_sys_path)));
        Vector<Vector3f> positions;
        Vector<Vector3f> normals;
        Vector<Vector2f> uv0s;

        u64 total_vertex_count = 0u;
        bool has_normals = false;
        bool has_uv0 = false;
        for (const auto &mesh: _loaded_meshes)
        {
            total_vertex_count += mesh->GetVertices().size();
            has_normals |= !mesh->GetNormals().empty();
            has_uv0 |= !mesh->GetUVs(0).empty();
        }

        positions.reserve(total_vertex_count);
        if (has_normals)
            normals.reserve(total_vertex_count);
        if (has_uv0)
            uv0s.reserve(total_vertex_count);

        u32 vertex_offset = 0u;
        bool has_bounds = false;
        Vector3f bounds_min{};
        Vector3f bounds_max{};
        combined_mesh->_bounds.clear();

        for (const auto &mesh: _loaded_meshes)
        {
            const auto mesh_positions = mesh->GetVertices();
            const auto mesh_normals = mesh->GetNormals();
            const auto mesh_uv0 = mesh->GetUVs(0);

            positions.insert(positions.end(), mesh_positions.begin(), mesh_positions.end());
            if (has_normals)
            {
                if (mesh_normals.size() == mesh_positions.size())
                    normals.insert(normals.end(), mesh_normals.begin(), mesh_normals.end());
                else
                    normals.resize(normals.size() + mesh_positions.size(), Vector3f::kZero);
            }
            if (has_uv0)
            {
                if (mesh_uv0.size() == mesh_positions.size())
                    uv0s.insert(uv0s.end(), mesh_uv0.begin(), mesh_uv0.end());
                else
                    uv0s.resize(uv0s.size() + mesh_positions.size(), Vector2f::kZero);
            }

            const auto &materials = mesh->GetCacheMaterials();
            for (u16 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
            {
                Vector<u32> combined_indices;
                const auto indices = mesh->GetIndices(submesh_index);
                combined_indices.reserve(indices.size());
                for (u32 index: indices)
                    combined_indices.emplace_back(index + vertex_offset);
                combined_mesh->AddSubmesh(combined_indices);

                if (submesh_index < materials.size())
                    combined_mesh->AddCacheMaterial(materials[submesh_index]);

                const AABB &submesh_bounds = mesh->GetBoundBox(submesh_index + 1u);
                combined_mesh->_bounds.emplace_back(submesh_bounds);
                if (!has_bounds)
                {
                    bounds_min = submesh_bounds._min;
                    bounds_max = submesh_bounds._max;
                    has_bounds = true;
                }
                else
                {
                    bounds_min = Min(bounds_min, submesh_bounds._min);
                    bounds_max = Max(bounds_max, submesh_bounds._max);
                }
            }

            vertex_offset += static_cast<u32>(mesh_positions.size());
        }

        combined_mesh->SetVertices(std::move(positions));
        if (has_normals)
            combined_mesh->SetNormals(std::move(normals));
        if (has_uv0)
            combined_mesh->SetUVs(std::move(uv0s), 0u);
        if (has_uv0)
            CalculateTangant(combined_mesh.get());

        if (!has_bounds && !combined_mesh->GetVertices().empty())
        {
            bounds_min = combined_mesh->GetVertices().front();
            bounds_max = combined_mesh->GetVertices().front();
            for (const auto &position: combined_mesh->GetVertices())
            {
                bounds_min = Min(bounds_min, position);
                bounds_max = Max(bounds_max, position);
            }
            has_bounds = true;
        }

        if (has_bounds)
        {
            AABB combined_bounds(bounds_min, bounds_max);
            combined_bounds._min -= kBoundsPadding;
            combined_bounds._max += kBoundsPadding;
            combined_mesh->_bounds.insert(combined_mesh->_bounds.begin(), combined_bounds);
        }

        _loaded_meshes.clear();
        _loaded_meshes.emplace_back(combined_mesh);
        LOG_INFO("Combine glTF meshes into a single mesh");
        return true;
    }

    void GltfParser::ParserImpl(const WString &sys_path)
    {
        GltfDocument document;
        if (!LoadGltfDocument(sys_path, document))
            return;

        Vector<NodeInstance> node_instances;
        if (!document._scenes.empty())
        {
            const i32 scene_index = document._scene >= 0 && document._scene < static_cast<i32>(document._scenes.size()) ? document._scene : 0;
            for (i32 node_index: document._scenes[scene_index]._nodes)
                CollectNodeInstancesRecursive(document, node_index, IdentityMatrix(), node_instances);
        }
        else
        {
            Vector<bool> is_child(document._nodes.size(), false);
            for (const auto &node: document._nodes)
            {
                for (i32 child_index: node._children)
                {
                    if (child_index >= 0 && child_index < static_cast<i32>(is_child.size()))
                        is_child[child_index] = true;
                }
            }

            bool found_root = false;
            for (i32 node_index = 0; node_index < static_cast<i32>(document._nodes.size()); ++node_index)
            {
                if (!is_child[node_index])
                {
                    CollectNodeInstancesRecursive(document, node_index, IdentityMatrix(), node_instances);
                    found_root = true;
                }
            }

            if (!found_root)
            {
                for (i32 node_index = 0; node_index < static_cast<i32>(document._nodes.size()); ++node_index)
                    CollectNodeInstancesRecursive(document, node_index, IdentityMatrix(), node_instances);
            }
        }

        if (node_instances.empty())
        {
            LOG_WARNING(L"glTF file {} does not contain mesh nodes", sys_path);
            return;
        }

        const String file_name = ToChar(PathUtils::GetFileName(sys_path));
        for (size_t instance_index = 0u; instance_index < node_instances.size(); ++instance_index)
        {
            const auto &instance = node_instances[instance_index];
            const auto &mesh_def = document._meshes[instance._mesh_index];
            String mesh_name = instance._name;
            if (mesh_name.empty())
                mesh_name = mesh_def._name.empty() ? std::format("{}_{}", file_name, instance_index) : mesh_def._name;

            if (!_import_setting._mesh_name.empty() && !_import_setting._is_combine_mesh)
            {
                if (mesh_name != _import_setting._mesh_name && mesh_def._name != _import_setting._mesh_name)
                    continue;
            }

            if (instance._has_skin)
                LOG_WARNING("glTF skin data is currently loaded as static mesh: {}", mesh_name);

            Vector<PrimitiveData> primitive_data_list;
            primitive_data_list.reserve(mesh_def._primitives.size());
            for (u16 primitive_index = 0u; primitive_index < mesh_def._primitives.size(); ++primitive_index)
            {
                const auto &primitive = mesh_def._primitives[primitive_index];
                if (primitive._mode != kGltfModeTriangles)
                {
                    LOG_WARNING("Skip unsupported glTF primitive mode {} in mesh {}", primitive._mode, mesh_name);
                    continue;
                }
                if (primitive._position < 0)
                    continue;

                Vector<std::array<f32, 3>> raw_positions;
                if (!ReadFloatAccessor<3>(document, primitive._position, "VEC3", raw_positions))
                {
                    LOG_WARNING("Skip glTF primitive without readable POSITION accessor in mesh {}", mesh_name);
                    continue;
                }

                PrimitiveData primitive_data;
                primitive_data._material = BuildMaterialInfo(document, primitive, primitive_index, mesh_name, sys_path);
                primitive_data._positions.resize(raw_positions.size());
                for (size_t index = 0u; index < raw_positions.size(); ++index)
                {
                    Vector3f position{raw_positions[index][0], raw_positions[index][1], raw_positions[index][2]};
                    primitive_data._positions[index] = ToEnginePosition(TransformPoint(instance._world_matrix, position));
                }

                if (primitive._normal >= 0)
                {
                    Vector<std::array<f32, 3>> raw_normals;
                    if (ReadFloatAccessor<3>(document, primitive._normal, "VEC3", raw_normals) && raw_normals.size() == primitive_data._positions.size())
                    {
                        primitive_data._has_normals = true;
                        primitive_data._normals.resize(raw_normals.size());
                        for (size_t index = 0u; index < raw_normals.size(); ++index)
                        {
                            Vector3f normal{raw_normals[index][0], raw_normals[index][1], raw_normals[index][2]};
                            primitive_data._normals[index] = ToEngineDirection(TransformNormal(instance._world_matrix, normal));
                        }
                    }
                }

                if (primitive._texcoord0 >= 0)
                {
                    Vector<std::array<f32, 2>> raw_uv0;
                    if (ReadFloatAccessor<2>(document, primitive._texcoord0, "VEC2", raw_uv0) && raw_uv0.size() == primitive_data._positions.size())
                    {
                        primitive_data._has_uv0 = true;
                        primitive_data._uv0.resize(raw_uv0.size());
                        for (size_t index = 0u; index < raw_uv0.size(); ++index)
                            primitive_data._uv0[index] = Vector2f{raw_uv0[index][0], raw_uv0[index][1]};
                    }
                }

                if (primitive._indices >= 0)
                {
                    if (!ReadIndexAccessor(document, primitive._indices, primitive_data._indices))
                    {
                        LOG_WARNING("Skip glTF primitive with unreadable index accessor in mesh {}", mesh_name);
                        continue;
                    }
                }
                else
                {
                    primitive_data._indices.resize(primitive_data._positions.size());
                    std::iota(primitive_data._indices.begin(), primitive_data._indices.end(), 0u);
                }

                // The renderer uses D3D12's default clockwise front-face convention.
                // After the glTF right-handed to engine left-handed conversion flips Z,
                // the imported triangle order already matches the engine's visible side.
                // Rewriting indices here would invert front faces relative to the mesh normals.

                if (!primitive_data._positions.empty())
                {
                    Vector3f bounds_min = primitive_data._positions.front();
                    Vector3f bounds_max = primitive_data._positions.front();
                    for (u32 vertex_index: primitive_data._indices)
                    {
                        if (vertex_index >= primitive_data._positions.size())
                            continue;
                        bounds_min = Min(bounds_min, primitive_data._positions[vertex_index]);
                        bounds_max = Max(bounds_max, primitive_data._positions[vertex_index]);
                    }
                    primitive_data._bounds = AABB(bounds_min, bounds_max);
                    primitive_data._bounds._min -= kBoundsPadding;
                    primitive_data._bounds._max += kBoundsPadding;
                }

                primitive_data_list.emplace_back(std::move(primitive_data));
            }

            if (primitive_data_list.empty())
                continue;

            Ref<Mesh> mesh = MakeRef<Mesh>(mesh_name);
            Vector<Vector3f> positions;
            Vector<Vector3f> normals;
            Vector<Vector2f> uv0s;
            Vector<Vector<u32>> submesh_indices;
            bool has_uv0 = false;
            bool need_recalculate_normals = _import_setting._is_recalculate_normals;
            Vector3f bounds_min{};
            Vector3f bounds_max{};
            bool has_bounds = false;

            for (const auto &primitive_data: primitive_data_list)
            {
                const u32 vertex_offset = static_cast<u32>(positions.size());
                positions.insert(positions.end(), primitive_data._positions.begin(), primitive_data._positions.end());
                if (!primitive_data._has_normals)
                    need_recalculate_normals = true;
                if (primitive_data._has_uv0)
                    has_uv0 = true;
            }

            u32 vertex_offset = 0u;
            for (const auto &primitive_data: primitive_data_list)
            {
                if (!need_recalculate_normals)
                    normals.insert(normals.end(), primitive_data._normals.begin(), primitive_data._normals.end());
                if (has_uv0)
                {
                    if (primitive_data._has_uv0)
                        uv0s.insert(uv0s.end(), primitive_data._uv0.begin(), primitive_data._uv0.end());
                    else
                        uv0s.resize(uv0s.size() + primitive_data._positions.size(), Vector2f::kZero);
                }

                Vector<u32> indices = primitive_data._indices;
                for (auto &index: indices)
                    index += vertex_offset;
                submesh_indices.emplace_back(indices);
                mesh->AddSubmesh(indices);
                mesh->AddCacheMaterial(primitive_data._material);
                mesh->_bounds.emplace_back(primitive_data._bounds);

                if (!has_bounds)
                {
                    bounds_min = primitive_data._bounds._min;
                    bounds_max = primitive_data._bounds._max;
                    has_bounds = true;
                }
                else
                {
                    bounds_min = Min(bounds_min, primitive_data._bounds._min);
                    bounds_max = Max(bounds_max, primitive_data._bounds._max);
                }

                vertex_offset += static_cast<u32>(primitive_data._positions.size());
            }

            if (need_recalculate_normals)
                CalculateNormals(positions, submesh_indices, normals);

            mesh->SetVertices(std::move(positions));
            mesh->SetNormals(std::move(normals));
            if (has_uv0)
                mesh->SetUVs(std::move(uv0s), 0u);

            if (has_bounds)
            {
                AABB mesh_bounds(bounds_min, bounds_max);
                mesh_bounds._min -= kBoundsPadding;
                mesh_bounds._max += kBoundsPadding;
                mesh->_bounds.insert(mesh->_bounds.begin(), mesh_bounds);
            }

            if (has_uv0)
                CalculateTangant(mesh.get());

            _loaded_meshes.emplace_back(mesh);
        }

        if (_import_setting._is_combine_mesh)
            CombineLoadedMeshes();

        if ((_import_setting._import_flag & MeshImportSetting::kImportFlagAnimation) != 0u)
            LOG_WARNING("glTF animation import is not implemented yet for {}", ToChar(sys_path));

        if ((_import_setting._import_flag & MeshImportSetting::kImportFlagMesh) == 0u)
            _loaded_meshes.clear();

        LOG_INFO(L"glTF file {} parsed with {} mesh", sys_path, _loaded_meshes.size());
    }
}
