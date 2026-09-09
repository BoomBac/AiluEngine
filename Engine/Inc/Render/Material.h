#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include "Buffer.h"
#include "Assets/AssetRegistry.h"
#include "Framework/Common/Reflect.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/List.h"
#include "Framework/Core/Containers/Queue.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Core/Delegate.h"
#include "Objects/Object.h"
#include "Shader.h"
#include "Texture.h"
#include "RenderConstants.h"
#include "MaterialDrawState.h"
#include "RenderingStates.h"
#include <map>
#include <unordered_set>
#include "generated/Material.gen.h"


namespace Ailu::Render
{
    class FrameAllocator;
    struct FrameUploadAllocation;

    enum class ETextureUsage : u8
    {
        kAlbedo = 0,
        kNormal,
        kEmission,
        kRoughness,
        kMetallic,
        kSpecular,
        kAnisotropy,
        kNone
    };

    AENUM()
    enum class EMaterialID
    {
        kStandard,
        kSubsurface,
        kChecker
    };

    AENUM()
    enum class ESurfaceType
    {
        kOpaque,
        kTransparent,
        kAlphaTest
    };

    // Standard Lit 是 shader/property 约定，而不是独立的 Material C++ 类型。
    // 该结构集中描述标准 Lit 材质的属性命名与 sampler mask 位约定。
    struct StandardMaterialProperty
    {
        struct Info
        {
            String _group_name;
            String _tex_name;
            String _value_name;
            u16 _mask_flag;
        };
        inline static const Info kAlbedo{"Albedo", "_AlbedoTex", "_AlbedoValue", 1};
        inline static const Info kNormal{"Normal", "_NormalTex", "_NormalValue", 2};
        inline static const Info kEmission{"Emission", "_EmissionTex", "_EmissionValue", 4};
        inline static const Info kRoughness{"Roughness", "_RoughnessMetallicTex", "_RoughnessValue", 8};
        inline static const Info kMetallic{"Metallic", "_RoughnessMetallicTex", "_MetallicValue", 8};
        inline static const Info kSpecular{"Specular", "_SpecularTex", "_SpecularValue", 16};
        inline static const Info kAnisotropy{"Anisotropy", "_AnisotropyTex", "_Anisotropy", 32};
        static const Info &GetInfoByUsage(ETextureUsage usage)
        {
            switch (usage)
            {
                case ETextureUsage::kAlbedo:
                    return kAlbedo;
                case ETextureUsage::kNormal:
                    return kNormal;
                case ETextureUsage::kEmission:
                    return kEmission;
                case ETextureUsage::kRoughness:
                    return kRoughness;
                case ETextureUsage::kMetallic:
                    return kMetallic;
                case ETextureUsage::kSpecular:
                    return kSpecular;
                case ETextureUsage::kAnisotropy:
                    return kAnisotropy;
                default:
                    break;
            }
            return kAlbedo;
        }
    };

    ACLASS()
    class AILU_API Material : public Object
    {
        GENERATED_BODY()
        friend class ResourceMgr;
        struct PassVariantInfo
        {
            String _pass_name;
            ShaderVariantHash _variant_hash;
            std::set<String> _keywords;
        };

    public:
        struct PropertyBlock
        {
            u8* _data = nullptr;
            u32 _size = 0u;
            ~PropertyBlock()
            {
                if (_data)
                    AL_FREE(_data);
            }
        };
        struct PropertyBlockView
        {
            u8 *_data = nullptr;
            u32 _size = 0u;
            GpuResource *_upload_buffer = nullptr;// 帧上传缓冲区，用于烘焙进binding snapshot并做资源追踪
            u64 _gpu_handle = 0u;                  // GPU虚拟地址
        };
        struct AssetSnapshot
        {
            Ref<const Shader> _shader;
            Map<ShaderPropertyId, Ref<const Texture>> _textures;
            u64 _shader_revision = 0u;
            Map<ShaderPropertyId, u64> _texture_revisions;
        };
        inline static std::weak_ptr<Material> s_standard_defered_lit;
        inline static std::weak_ptr<Material> s_standard_forward_lit;
        inline static std::weak_ptr<Material> s_checker;

        Material() = default;
        Material(Shader *shader, String name);
        Material(const Material &other);
        Material &operator=(const Material &other);
        Material &operator=(Material &&other) noexcept;
        Material(Material &&other) noexcept;

        ~Material();
        // 创建不关联资产的运行时材质实例。
        Ref<Material> CreateInstance() const;
        // 创建一个使用标准 Lit shader 的 Material，等价于旧的 StandardMaterial("name")
        static Ref<Material> CreateStandard(String name);
        bool IsStandardLit() const;
        void ChangeShader(Shader *shader);
        void SetActiveShader(Shader *shader);
        void SetFloat(const String &name, const float &f);
        void SetFloat(ShaderPropertyId property_id, const float &f);
        void SetInt(const String &name, i32 value);
        void SetInt(ShaderPropertyId property_id, i32 value);
        void SetVector(const String &name, const Vector4f &vector);
        void SetVector(ShaderPropertyId property_id, const Vector4f &vector);
        void SetVector(const String &name, const Vector4Int &vector);
        void SetVector(ShaderPropertyId property_id, const Vector4Int &vector);
        void SetMatrix(const String &name, const Matrix4x4f &matrix);
        void SetMatrix(ShaderPropertyId property_id, const Matrix4x4f &matrix);
        void SetBuffer(const String& name,GPUBuffer* buffer);
        void SetBuffer(ShaderPropertyId property_id, GPUBuffer *buffer);
        const Map<ShaderPropertyId, Texture *> &BoundTextures() const { return _bind_textures_by_id; }
        const Map<ShaderPropertyId, GPUBuffer *> &BoundBuffers() const { return _bind_buffers_by_id; }
        float GetFloat(const String &name);
        float GetFloat(ShaderPropertyId property_id);
        void SetCullMode(ECullMode mode);
        [[nodiscard]] ECullMode GetCullMode() const;
        [[nodiscard]] ShaderVariantHash ActiveVariantHash(u16 pass_index) const;
        [[nodiscard]] std::set<String> ActiveKeywords(u16 pass_index) const;
        [[nodiscard]] u16 RenderQueue() const { return _render_queue; };
        void RenderQueue(u16 new_queue) { _render_queue = new_queue; };
        u32 GetUint(const String &name);
        u32 GetUint(ShaderPropertyId property_id);
        Vector4f GetVector(const String &name);
        Vector4f GetVector(ShaderPropertyId property_id);
        void RemoveTexture(const String &name);
        virtual void SetTexture(const String &name, Texture *texture);
        virtual void SetTexture(ShaderPropertyId property_id, Texture *texture);
        virtual void SetTexture(const String &name, const WString &texture_path);
        virtual void SetTexture(const String &name, RTHandle texture);
        //标准材质纹理绑定，自动维护 _SamplerMask
        void SetTexture(ETextureUsage usage, Texture *tex);
        //开启一个关键字并关闭同组的其他关键字
        void EnableKeyword(const String &keyword);
        void DisableKeyword(const String &keyword);
        MaterialDrawState CaptureDrawState(u16 pass_index, u32 frame_slot, u64 frame_count, FrameAllocator &allocator,
                                           const HashMap<ShaderPropertyId, CommandResourceBinding> *command_resources = nullptr,
                                           CommandRenderingStatesData *statistics = nullptr);
        [[nodiscard]] Shader *GetShader() const { return _p_shader; };
        [[nodiscard]] Shader *GetActiveShader() const { return _p_active_shader; };
        static ShaderPropertyId SurfacePropertyId() { return ShaderPropertyRegistry::Get().Intern("_surface"); }
        DECLARE_EVENT_ROUTER(on_property_changed, ShaderPropertyId);
        bool IsReadyForDraw(u16 pass_index = 0) const;
        [[nodiscard]] const Guid &ShaderGuid() const { return _shader_guid; }
        void SetShaderGuid(const Guid &guid)
        {
            _shader_guid = guid;
            UpdateShaderHandle();
        }
        void SetTextureGuid(const String &name, const Guid &guid)
        {
            _texture_guids[name] = guid;
            UpdateTextureHandle(ShaderPropertyRegistry::Get().Intern(name), guid);
        }
        [[nodiscard]] AssetSnapshot CaptureAssetSnapshots() const;
        // Command construction calls this before CaptureDrawState; render recording only consumes the snapshot.
        void RefreshAssetReferences();
        [[nodiscard]] const Guid &TextureGuid(const String &name) const
        {
            auto iter = _texture_guids.find(name);
            return iter != _texture_guids.end() ? iter->second : Guid::EmptyGuid();
        }
        [[nodiscard]] const Map<String, Guid> &TextureGuids() const { return _texture_guids; }
        List<std::tuple<String, float>> GetAllFloatValue();
        List<std::tuple<String, Vector4f>> GetAllVectorValue();
        List<std::tuple<String, Vector4Int>> GetAllIntVectorValue();
        List<std::tuple<String, u32>> GetAllUintValue();
        Vector<ShaderPropertyInfo *> &GetShaderProperty() { return _prop_views; };
        ShaderPropertyInfo *GetShaderProperty(const String &name);
        ShaderPropertyInfo *GetShaderProperty(ShaderPropertyId property_id);
        //根据材质存储的关键字和传入shader的关键字，为每个Pass构建合法的关键字序列
        void ConstructKeywords(Shader *shader);
        /// 构造 drawcmd时调用，将当前状态推入队列,返回材质cbuf的绑定槽
        PropertyBlock* GetPropertyBlock(u16 block_index)
        {
            AL_ASSERT(block_index < _property_blocks.size());
            return &_property_blocks[block_index];
        }
        PropertyBlockView GetPropertyBlockForFrame(u16 pass_index, u32 frame_slot, u64 frame_count, FrameAllocator &allocator,
                                                   bool upload_to_gpu, CommandRenderingStatesData *statistics = nullptr);
        u32 PropertyVersion() const { return _property_data_version + _resource_binding_version; }
        std::set<String>& SavedKeyworkds() { return _all_keywords; }
        virtual void Construct(bool first_time);
        ESurfaceType SurfaceType() const;
        void SurfaceType(ESurfaceType value);
        EMaterialID MaterialID() const;
        void MaterialID(EMaterialID value);
        const Texture *MainTex(ETextureUsage usage) const;
        const ShaderPropertyInfo *MainProperty(ETextureUsage usage) const;
        bool IsTextureUsed(ETextureUsage usage) const;
    protected:

    private:
        struct BindState
        {
            u16 _pass_index = 0;
            u16 _max_bind_slot = 0;
            i16 _cbuf_bind_slot = -1;
            bool _is_ready = false;
            ShaderVariantHash _variant_hash;
            Array<GpuResource*,32> _bind_res;
            Array<EBindResDescType,32u> _bind_res_type;
            Array<u16,32u> _bind_res_priority;
        };
        struct ResolvedResourceBinding
        {
            ShaderPropertyId _property_id = kInvalidShaderPropertyId;
            u8 _bind_slot = 0u;
            EBindResDescType _resource_type = EBindResDescType::kUnknown;
        };
        struct BindingCacheEntry
        {
            u32 _resource_binding_version = 0u;
            u32 _layout_version = 0u;
            ShaderVariantHash _variant_hash = 0u;
            u64 _global_res_layout_version = 0u;
            u64 _global_res_binding_version = 0u;
            Vector<ResolvedResourceBinding> _global_res_bindings;
            BindState _state;
        };
        struct FramePropertyBlockCache
        {
            u32 _property_data_version = 0u;
            u64 _frame_count = static_cast<u64>(-1);
            PropertyBlockView _block;
        };
        struct CachedPropertyValue
        {
            Vector<u8> _data;
        };
        Vector<BindingCacheEntry> _binding_cache;
        Array<Vector<FramePropertyBlockCache>, RenderConstants::kFrameCount + 1u> _frame_property_block_cache;
    private:
        void UpdateShaderHandle();
        void UpdateTextureHandle(ShaderPropertyId property_id, const Guid &guid);
        void UpdateBindTexture(u16 pass_index, ShaderVariantHash new_hash);
        void ResolveStandardMaterialLayout();
        void MarkTextureUsed(std::initializer_list<ETextureUsage> usages, bool used);
        void SyncPropertyValue(ShaderPropertyId property_id);
        void RebuildShaderState(bool first_time);
    protected:
        void MarkPropertyDataDirty() { ++_property_data_version; }
        void MarkResourceBindingsDirty() { ++_resource_binding_version; }
        void MarkPropertiesDirty()
        {
            MarkPropertyDataDirty();
            MarkResourceBindingsDirty();
        }
    private:
        void ResetMaterialCaches();

    protected:
        inline static u32 s_total_material_num = 0u;
        inline const static String kCullModeKey = "_cull";
        inline const static String kSurfaceKey = "_surface";
        u16 _standard_pass_index = -1;
        u16 _render_queue = 2000;
        Vector<u16> _mat_cbuf_per_pass_size;
        Shader *_p_shader = nullptr;
        Shader *_p_active_shader = nullptr;
        Guid _shader_guid = Guid::EmptyGuid();
        AssetHandle<Shader> _shader_handle;
        Map<String, Guid> _texture_guids;
        Map<ShaderPropertyId, AssetHandle<Texture>> _texture_handles_by_id;
        //运行时每个pass的关键字信息
        Vector<PassVariantInfo> _pass_variants;
        //跟随材质持久化的关键字
        std::set<String> _all_keywords;
        Map<ShaderPropertyId, CachedPropertyValue> _property_values;
        Map<String, ShaderPropertyInfo> _properties;
        Vector<ShaderPropertyInfo *> _prop_views;
        Vector<PropertyBlock> _property_blocks;
        u32 _property_data_version = 1u;
        u32 _resource_binding_version = 1u;
        Map<ShaderPropertyId, Texture *> _bind_textures_by_id{};
        Map<ShaderPropertyId, GPUBuffer *> _bind_buffers_by_id{};
        //非shader使用的变量
        Map<String, u32> _common_uint_property;
        Map<String, f32> _common_float_property;
        Map<String, Vector4f> _common_vector_property;
        ECullMode _cull_mode = ECullMode::kBack;
    private:
        ESurfaceType _surface = ESurfaceType::kOpaque;
        EMaterialID _material_id = EMaterialID::kStandard;
        u16 _sampler_mask_offset = 0u;
        u16 _material_id_offset = 0u;
        u32 _sampler_mask = 0u;
        bool _is_standard_lit = false;
    };
}// namespace Ailu

#endif// !MATERIAL_H__
