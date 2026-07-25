#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include "Buffer.h"
#include "Framework/Common/Reflect.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/List.h"
#include "Framework/Core/Containers/Queue.h"
#include "Framework/Core/Containers/Array.h"
#include "Objects/Object.h"
#include "Shader.h"
#include "Texture.h"
#include <map>
#include <unordered_set>
#include "generated/Material.gen.h"


namespace Ailu::Render
{
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
                    delete[] _data;
            }
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
        void ChangeShader(Shader *shader);
        void SetFloat(const String &name, const float &f);
        void SetInt(const String &name, i32 value);
        void SetVector(const String &name, const Vector4f &vector);
        void SetVector(const String &name, const Vector4Int &vector);
        void SetMatrix(const String &name, const Matrix4x4f &matrix);
        void SetBuffer(const String& name,GPUBuffer* buffer);
        float GetFloat(const String &name);
        void SetCullMode(ECullMode mode);
        [[nodiscard]] ECullMode GetCullMode() const;
        [[nodiscard]] ShaderVariantHash ActiveVariantHash(u16 pass_index) const;
        [[nodiscard]] std::set<String> ActiveKeywords(u16 pass_index) const;
        [[nodiscard]] u16 RenderQueue() const { return _render_queue; };
        void RenderQueue(u16 new_queue) { _render_queue = new_queue; };
        u32 GetUint(const String &name);
        Vector4f GetVector(const String &name);
        void RemoveTexture(const String &name);
        virtual void SetTexture(const String &name, Texture *texture);
        virtual void SetTexture(const String &name, const WString &texture_path);
        virtual void SetTexture(const String &name, RTHandle texture);
        //开启一个关键字并关闭同组的其他关键字
        void EnableKeyword(const String &keyword);
        void DisableKeyword(const String &keyword);
        virtual void Bind(u16 pass_index = 0);
        [[nodiscard]] Shader *GetShader() const { return _p_shader; };
        bool IsReadyForDraw(u16 pass_index = 0) const;
        List<std::tuple<String, float>> GetAllFloatValue();
        List<std::tuple<String, Vector4f>> GetAllVectorValue();
        List<std::tuple<String, Vector4Int>> GetAllIntVectorValue();
        List<std::tuple<String, u32>> GetAllUintValue();
        Vector<ShaderPropertyInfo *> &GetShaderProperty() { return _prop_views; };
        ShaderPropertyInfo *GetShaderProperty(const String &name);
        //根据材质存储的关键字和传入shader的关键字，为每个Pass构建合法的关键字序列
        void ConstructKeywords(Shader *shader);
        /// 构造 drawcmd时调用，将当前状态推入队列,返回材质cbuf的绑定槽
        i16 PushState(u16 pass_index = 0u);
        PropertyBlock* GetPropertyBlock(u16 block_index)
        {
            AL_ASSERT(block_index < _property_blocks.size());
            return &_property_blocks[block_index];
        }
        std::set<String>& SavedKeyworkds() { return _all_keywords; }
        virtual void Construct(bool first_time);
    protected:

    private:
        struct BindState
        {
            u16 _pass_index = 0;
            u16 _max_bind_slot = 0;
            i16 _cbuf_bind_slot = -1;
            ShaderVariantHash _variant_hash;
            Array<GpuResource*,32> _bind_res;
            Array<u16,32u> _bind_res_priority;
        };
        std::mutex _state_mutex;
        Queue<BindState> _states;
    private:
        void UpdateBindTexture(u16 pass_index, ShaderVariantHash new_hash);

    protected:
        inline static u32 s_total_material_num = 0u;
        inline const static String kCullModeKey = "_cull";
        inline const static String kSurfaceKey = "_surface";
        u16 _standard_pass_index = -1;
        u16 _render_queue = 2000;
        Vector<u16> _mat_cbuf_per_pass_size;
        Shader *_p_shader;
        Shader *_p_active_shader;
        //运行时每个pass的关键字信息
        Vector<PassVariantInfo> _pass_variants;
        //跟随材质持久化的关键字
        std::set<String> _all_keywords;
        Map<String, ShaderPropertyInfo> _properties;
        Vector<ShaderPropertyInfo *> _prop_views;
        Vector<PropertyBlock> _property_blocks;
        Map<String,Texture *> _bind_textures{};
        //非shader使用的变量
        Map<String, u32> _common_uint_property;
        Map<String, f32> _common_float_property;
    };

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

    ACLASS()
    class AILU_API StandardMaterial : public Material
    {
        GENERATED_BODY()
    public:
        struct StandardPropertyName
        {
            struct StandardPropertyNameInfo
            {
                String _group_name;
                String _tex_name;
                String _value_name;
                u16 _mask_flag;
            };
            inline static const StandardPropertyNameInfo kAlbedo{"Albedo", "_AlbedoTex", "_AlbedoValue", 1};
            inline static const StandardPropertyNameInfo kNormal{"Normal", "_NormalTex", "_NormalValue", 2};
            inline static const StandardPropertyNameInfo kEmission{"Emission", "_EmissionTex", "_EmissionValue", 4};
            inline static const StandardPropertyNameInfo kRoughness{"Roughness", "_RoughnessMetallicTex", "_RoughnessValue", 8};
            inline static const StandardPropertyNameInfo kMetallic{"Metallic", "_RoughnessMetallicTex", "_MetallicValue", 8};
            inline static const StandardPropertyNameInfo kSpecular{"Specular", "_SpecularTex", "_SpecularValue", 16};
            inline static const StandardPropertyNameInfo kAnisotropy{"Anisotropy", "_AnisotropyTex", "_Anisotropy", 32};
            static const StandardPropertyNameInfo &GetInfoByUsage(ETextureUsage usage)
            {
                switch (usage)
                {
                    case ETextureUsage::kAlbedo:
                        return kAlbedo;
                        break;
                    case ETextureUsage::kNormal:
                        return kNormal;
                        break;
                    case ETextureUsage::kEmission:
                        return kEmission;
                        break;
                    case ETextureUsage::kRoughness:
                        return kRoughness;
                    case ETextureUsage::kMetallic:
                        return kMetallic;
                        break;
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
        StandardMaterial() = default;
        explicit StandardMaterial(String name);
        void Construct(bool first_time) final;
        ~StandardMaterial();
        void MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use);
        bool IsTextureUsed(ETextureUsage use_info);
        virtual void Bind(u16 pass_index = 0) override;
        virtual void SetTexture(const String &name, Texture *texture);
        virtual void SetTexture(const String &name, const WString &texture_path);
        virtual void SetTexture(const String &name, RTHandle texture);
        void SetTexture(ETextureUsage usage, Texture *tex);
        const Texture *MainTex(ETextureUsage usage) const;
        const ShaderPropertyInfo &MainProperty(ETextureUsage usage);
        const ESurfaceType &SurfaceType() const { return _surface; }
        void SurfaceType(const ESurfaceType &value);
        const EMaterialID &MaterialID() const { return _material_id; }
        void MaterialID(const EMaterialID &value);

    private:
        ESurfaceType _surface = ESurfaceType::kOpaque;
        EMaterialID _material_id = EMaterialID::kStandard;
        u16 _sampler_mask_offset = 0u;
        u16 _material_id_offset = 0u;
    };
}// namespace Ailu

#endif// !MATERIAL_H__
