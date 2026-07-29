#ifndef __CORE_TYPE_H__
#define __CORE_TYPE_H__
#include "Framework/Core/String.h"
#include "Framework/Core/SmartPtr.h"
#include "RenderConstants.h"
#include "generated/CoreType.gen.h"
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>


namespace Ailu
{
    namespace Render
    {
        AENUM()
        enum class EResourceUsage : u32
        {
            kNone = 0,
            kReadSRV = 1 << 0,         // SRV - 着色器读取
            kWriteUAV = 1 << 1,        // UAV - 无序访问
            kWriteRTV = 1 << 2,        // RTV - 渲染目标
            kDSV = 1 << 3,             // DSV - 深度模板
            kCopySrc = 1 << 4,         // 拷贝源
            kCopyDst = 1 << 5,         // 拷贝目标
            kIndirectArgument = 1 << 6,// 间接绘制参数
            kRaytracingAccel = 1 << 7  // 光追加速结构
        };

        inline EResourceUsage operator|(EResourceUsage a, EResourceUsage b)
        {
            return static_cast<EResourceUsage>(static_cast<u32>(a) | static_cast<u32>(b));
        }

        inline EResourceUsage operator&(EResourceUsage a, EResourceUsage b)
        {
            return static_cast<EResourceUsage>(static_cast<u32>(a) & static_cast<u32>(b));
        }

        enum class ELoadStoreAction
        {
            kLoad,
            kStore,
            kClear,
            kNotCare
        };

        const static u32 kTotalSubRes = 0XFFFFFFFF;

        AENUM()
        enum class EResourceState
        {
            kCommon = 0,
            kVertexAndConstantBuffer = 0x1,
            kIndexBuffer = 0x2,
            kRenderTarget = 0x4,
            kUnorderedAccess = 0x8,
            kDepthWrite = 0x10,
            kDepthRead = 0x20,
            kNonPixelShaderResource = 0x40,
            kPixelShaderResource = 0x80,
            kStreamOut = 0x100,
            kIndirectArgument = 0x200,
            kCopyDest = 0x400,
            kCopySource = 0x800,
            kResolveDest = 0x1000,
            kResolveSource = 0x2000,
            kRaytracingAccelerationStructure = 0x400000,
            kShadingRateSource = 0x1000000,
            kGenericRead = ((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800,
            kAllShaderResource = (0x40 | 0x80),
            kPresent = 0,
            kPredication = 0x200,
            kVideoDecodeRead = 0x10000,
            kVideoDecodeWrite = 0x20000,
            kVideoProcessRead = 0x40000,
            kVideoProcessWrite = 0x80000,
            kVideoEncodeRead = 0x200000,
            kVideoEncodeWrite = 0x800000
        };

        inline bool operator&(EResourceState a, EResourceState b)
        {
            return (static_cast<u32>(a) & static_cast<u32>(b)) != 0;
        }

        enum class EGpuResType
        {
            kBuffer,
            kRWBuffer,
            kTexture,
            kRenderTexture,
            kVertexBuffer,
            kIndexBuffer,
            kConstBuffer,
            kGraphicsPSO,
            kBottomAS,
            kTopAS
        };

        struct ClearValue
        {
            enum class EType
            {
                kColor,
                kDepthStencil
            };
            EType type = EType::kColor;

            union
            {
                f32 color[4];
                struct
                {
                    f32 depth;
                    u32 stencil;
                } depthStencil;
            };

            static ClearValue Color(f32 r, f32 g, f32 b, f32 a = 1.0f)
            {
                ClearValue v;
                v.type = EType::kColor;
                v.color[0] = r;
                v.color[1] = g;
                v.color[2] = b;
                v.color[3] = a;
                return v;
            }

            static ClearValue DepthStencil(f32 depth, u32 stencil = 0)
            {
                ClearValue v;
                v.type = EType::kDepthStencil;
                v.depthStencil.depth = depth;
                v.depthStencil.stencil = stencil;
                return v;
            }
        };

        enum class EShaderType : u8
        {
            kVertex,
            kPixel,
            kGeometry,
            kHull,
            kDomain,
            kCompute,
            kRayTracing
        };

        struct ShaderPropertyType
        {
            inline static String Vector = "Vector";
            inline static String IntVector = "IntVector";
            inline static String Float = "Float";
            inline static String Uint = "Uint";
            inline static String Color = "Color";
            inline static String Texture2D = "Texture2D";
            inline static String Texture3D = "Texture3D";
            inline static String CubeMap = "CubeMap";
        };

        enum EBindResDescType
        {
            kConstBuffer = 0x01,
            kCBufferAttribute = 0x02,
            kCBufferFloat = 0x04,
            kCBufferFloats = 0x08,
            kCBufferUInt = 0x10,
            kCBufferUInts = 0x20,
            kCBufferMatrix = 0x40,
            kCBufferBool = 0x80,
            kTexture2D = 0x100,
            kTexture2DArray = 0x400,
            kCubeMap = 0x200,
            kSampler = 0x800,
            kUAVTexture2D = 0x1000,
            kConstBufferRaw = 0x2000,//为了兼容uploadbuffer直接绑定gpu address而不改变现有接口
            kBuffer = 0x4000,
            kRWBuffer = 0x8000,
            kTexture3D = 0x10000,
            kRWTexture3D = 0x20000,
            kCBufferInt = 0x40000,
            kCBufferInts = 0x80000,
            kAccelerationStructure = 0x100000,
            kUnknown
        };
        class GpuResource;

        using ShaderPropertyId = u32;
        inline constexpr ShaderPropertyId kInvalidShaderPropertyId = 0u;

        class AILU_API ShaderPropertyRegistry
        {
        public:
            static ShaderPropertyRegistry &Get()
            {
                static ShaderPropertyRegistry s_registry;
                return s_registry;
            }

            ShaderPropertyId Intern(const String &name)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (auto it = _name_to_id.find(name); it != _name_to_id.end())
                    return it->second;
                const auto id = static_cast<ShaderPropertyId>(_id_to_name.size());
                _name_to_id[name] = id;
                _id_to_name.emplace_back(name);
                return id;
            }

            const String &GetName(ShaderPropertyId property_id) const
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (property_id < _id_to_name.size())
                    return _id_to_name[property_id];
                return _id_to_name[kInvalidShaderPropertyId];
            }

        private:
            ShaderPropertyRegistry()
            {
                _id_to_name.emplace_back("");
            }

            mutable std::mutex _mutex;
            std::unordered_map<String, ShaderPropertyId> _name_to_id;
            std::vector<String> _id_to_name;
        };

        using ComputeShaderKernelId = u16;
        inline constexpr ComputeShaderKernelId kInvalidComputeShaderKernelId = static_cast<ComputeShaderKernelId>(-1);

        class AILU_API ComputeShaderKernelRegistry
        {
        public:
            static ComputeShaderKernelRegistry &Get()
            {
                static ComputeShaderKernelRegistry s_registry;
                return s_registry;
            }

            ComputeShaderKernelId Intern(const String &name)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (auto it = _name_to_id.find(name); it != _name_to_id.end())
                    return it->second;
                AL_ASSERT_MSG(_id_to_name.size() < kInvalidComputeShaderKernelId, "Too many compute shader kernels!");
                const auto id = static_cast<ComputeShaderKernelId>(_id_to_name.size());
                _name_to_id[name] = id;
                _id_to_name.emplace_back(name);
                return id;
            }

            ComputeShaderKernelId Find(const String &name) const
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (auto it = _name_to_id.find(name); it != _name_to_id.end())
                    return it->second;
                return kInvalidComputeShaderKernelId;
            }

            const String &GetName(ComputeShaderKernelId kernel_id) const
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (kernel_id < _id_to_name.size())
                    return _id_to_name[kernel_id];
                return _empty_name;
            }

        private:
            mutable std::mutex _mutex;
            std::unordered_map<String, ComputeShaderKernelId> _name_to_id;
            std::vector<String> _id_to_name;
            String _empty_name;
        };

        struct ShaderPropertyBinding
        {
            ShaderPropertyId _property_id = kInvalidShaderPropertyId;
            EBindResDescType _resource_type = EBindResDescType::kUnknown;
            i16 _bind_slot = -1;
            u32 _buffer_offset = 0u;
            u32 _buffer_size = 0u;
            u16 _register_space = 0u;
            u8 _bind_flag = 0u;
        };

        class AILU_API ShaderBindingLayout
        {
        public:
            u32 Version() const { return _version; }
            void Version(u32 version) { _version = version; }

            void Add(const ShaderPropertyBinding &binding)
            {
                if (binding._property_id == kInvalidShaderPropertyId)
                    return;
                _bindings[binding._property_id] = binding;
            }

            const ShaderPropertyBinding *Find(ShaderPropertyId property_id) const
            {
                const auto it = _bindings.find(property_id);
                return it == _bindings.end() ? nullptr : &it->second;
            }

        private:
            u32 _version = 0u;
            std::unordered_map<ShaderPropertyId, ShaderPropertyBinding> _bindings;
        };

        struct ShaderBindResourceInfo
        {
            inline static const u8 kBindFlagPerObject = 0x01;
            inline static const u8 kBindFlagPerMaterial = 0x02;
            inline static const u8 kBindFlagPerCamera = 0x04;
            inline static const u8 kBindFlagPerScene = 0x08;
            inline static const u8 kBindFlagInternal = 0x10;
            inline static const u8 kBindFlagLocal = 0x20;
            static u8 GetBindResourceFlag(const char *name)
            {
                String name_str(name);
                if (name_str == RenderConstants::kCBufNamePerObject)
                    return kBindFlagPerObject;
                else if (name_str == RenderConstants::kCBufNamePerMaterial)
                    return kBindFlagPerMaterial;
                else if (name_str == RenderConstants::kCBufNamePerCamera)
                    return kBindFlagPerCamera;
                else if (name_str == RenderConstants::kCBufNamePerScene)
                    return kBindFlagPerScene;
                else
                {
                    return kBindFlagInternal;
                }
            }
            inline const static std::set<String> s_reversed_res_name{
                    "SceneObjectBuffer",
                    "_MatrixWorld",
                    "SceneMaterialBuffer",
                    "SceneStatetBuffer",
                    "_MatrixV",
                    "_MatrixP",
                    "_MatrixVP",
                    "_CameraPos",
                    "_DirectionalLights",
                    "_PointLights",
                    "_SpotLights",
                    "padding",
                    "padding0",
                    "padding1"};
            inline static u16 GetVariableSize(const ShaderBindResourceInfo &info) { return info._cbuf_member_offset & 0XFFFF; }
            inline static u16 GetVariableOffset(const ShaderBindResourceInfo &info) { return info._cbuf_member_offset >> 16; }
            ShaderBindResourceInfo() = default;
            ShaderBindResourceInfo(EBindResDescType res_type, u32 slot_or_offset, u8 bind_slot, const String &name)
                : _res_type(res_type), _bind_slot(bind_slot), _name(name), _property_id(ShaderPropertyRegistry::Get().Intern(name))
            {
                if (res_type & EBindResDescType::kCBufferAttribute)
                    _cbuf_member_offset = slot_or_offset;
                else
                    _res_slot = slot_or_offset;
            }
            bool operator==(const ShaderBindResourceInfo &other) const
            {
                return _name == other._name;
            }
            EBindResDescType _res_type;
            union
            {
                u32 _res_slot;
                u32 _cbuf_member_offset;
            };
            u8 _bind_slot;
            String _name;
            ShaderPropertyId _property_id = kInvalidShaderPropertyId;
            GpuResource *_p_res = nullptr;
            ShaderBindResourceInfo *_p_root_cbuf;
            //1 for per obj,2for per mat,4 for per pass,8 for per frame
            u8 _bind_flag = 0u;
            u16 _register_space = 0u;
            u8 _array_size = 0u;
            u16 _cbuf_size = 0u;
        };

        // Hash function for ShaderBindResourceInfo
        struct ShaderBindResourceInfoHash
        {
            std::size_t operator()(const ShaderBindResourceInfo &info) const
            {
                return std::hash<String>{}(info._name);
            }
        };

        // Equality function for ShaderBindResourceInfo
        struct ShaderBindResourceInfoEqual
        {
            bool operator()(const ShaderBindResourceInfo &lhs, const ShaderBindResourceInfo &rhs) const
            {
                return lhs._name == rhs._name;
            }
        };

        using ShaderReflectionResourceMap = std::unordered_map<String, ShaderBindResourceInfo>;

        enum class EShaderPropertyType
        {
            kUndefined,
            kBool,
            kFloat,
            kRange,
            kVector,
            kColor,
            kTexture2D,
            kTexture3D,
            kEnum
        };

        struct ShaderPropertyInfo
        {
            bool IsHDRProperty() const
            {
                return _params.x == 1;
            }
            void SetHDRIntensity(f32 intensity)
            {
                if (IsHDRProperty())
                    _params.y = intensity;
            };
            f32 GetHDRIntensity() const
            {
                if (IsHDRProperty())
                    return _params.y;
            };
            String _value_name;
            String _prop_name;
            ShaderPropertyId _property_id = kInvalidShaderPropertyId;
            u32 _offset = 0;
            void *_value_ptr = nullptr;
            EShaderPropertyType _type;
            Vector4f _default_value;
            //目前用于标识hdr属性，x为1表示hdr，y为强度
            Vector4f _params;
            ShaderPropertyInfo() = default;
            ShaderPropertyInfo(const String &value_name, const String &prop_name, EShaderPropertyType prop_type, const Vector4f &default_value)
                : _value_name(value_name), _prop_name(prop_name), _property_id(ShaderPropertyRegistry::Get().Intern(value_name)),
                  _type(prop_type), _default_value(default_value), _params(Vector4f::kZero)
            {
            }
            template<typename T>
            T GetValue() const
            {
                return *static_cast<T *>(_value_ptr);
            }
            template<typename T>
            void SetValue(T value)
            {
                *static_cast<T *>(_value_ptr) = value;
            }
        };

        struct PipelineResource
        {
            inline static const u16 kPriorityGlobal = 0x0u;
            inline static const u16 kPriorityCmd = 0x1u;
            inline static const u16 kPriorityLocal = 0x2u;
            struct AddiInfo
            {
                //目前给upload buffer使用
                u64 _gpu_handle = 0u;
                //texture
                void *_native_res_ptr = nullptr;
                u16 _view_index = 9999u; //sync with Texture::kMainSRVIndex
                u32 _sub_res = UINT32_MAX;
                bool operator==(const AddiInfo &other) const
                {
                    return _gpu_handle == other._gpu_handle && _native_res_ptr == other._native_res_ptr && _view_index == other._view_index && _sub_res == other._sub_res;
                }
            };
            EBindResDescType _res_type = EBindResDescType::kUnknown;
            String _name;
            u16 _priority;
            u16 _slot;//构造时不赋值，实际绑定时由pso mgr/pso赋值
            bool _is_compute = false;
            PipelineResource() = default;
            PipelineResource(GpuResource *res, EBindResDescType resType, String name, u16 priority, bool is_compute = false, u16 register_space = 0u)
                : _p_resource(res), _res_type(resType), _name(std::move(name)), _priority(priority), _is_compute(is_compute) {}
            PipelineResource(GpuResource *res, EBindResDescType resType, u16 slot, u16 priority, bool is_compute = false, u16 register_space = 0u)
                : _p_resource(res), _res_type(resType), _slot(slot), _priority(priority), _is_compute(is_compute) {}
            bool operator<(const PipelineResource &other) const
            {
                return _priority > other._priority;
            }
            bool operator==(const PipelineResource &other) const
            {
                return _res_type == other._res_type && _priority == other._priority && _is_compute == other._is_compute && _p_resource == other._p_resource && _addi_info == other._addi_info;
            }
            PipelineResource(const PipelineResource &other)
            {
                _p_resource = other._p_resource;
                _res_type = other._res_type;
                _name = other._name;
                _priority = other._priority;
                _slot = other._slot;
                _is_compute = other._is_compute;
                _addi_info = other._addi_info;
            }
            PipelineResource(PipelineResource &&other) noexcept
            {
                _p_resource = other._p_resource;
                _res_type = other._res_type;
                _name = std::move(other._name);
                _priority = other._priority;
                _slot = other._slot;
                _is_compute = other._is_compute;
                _addi_info = other._addi_info;
            }
            PipelineResource &operator=(const PipelineResource &other)
            {
                _p_resource = other._p_resource;
                _res_type = other._res_type;
                _name = other._name;
                _priority = other._priority;
                _slot = other._slot;
                _is_compute = other._is_compute;
                _addi_info = other._addi_info;
                return *this;
            }
            PipelineResource &operator=(PipelineResource &&other) noexcept
            {
                _p_resource = other._p_resource;
                _res_type = other._res_type;
                _name = std::move(other._name);
                _priority = other._priority;
                _slot = other._slot;
                _is_compute = other._is_compute;
                _addi_info = other._addi_info;
                return *this;
            }
            void Clear() noexcept
            {
                _p_resource = nullptr;
                _res_type = EBindResDescType::kUnknown;
                _name.clear();
                _priority = 0u;
                _slot = 0u;
                _is_compute = false;
                _addi_info = {};
            }
            bool IsResolved() const { return _name.empty() && _slot < 32; }
            bool IsNamed() const { return !_name.empty(); }

            GpuResource *_p_resource;
            AddiInfo _addi_info;
        };
    };// namespace Render
};// namespace Ailu

#endif
