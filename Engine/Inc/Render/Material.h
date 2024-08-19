#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include "Buffer.h"
#include "Framework/Common/Reflect.h"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Shader.h"
#include "Texture.h"
#include <map>
#include <unordered_set>


namespace Ailu
{
    class AILU_API Material : public Object
    {
        friend class ResourceMgr;
        DECLARE_REFLECT_FIELD(Material)
        struct PassVariantInfo
        {
            String _pass_name;
            ShaderVariantHash _variant_hash;
            std::set<String> _keywords;
        };

    public:
        Material(Shader *shader, String name);
        Material(const Material &other);
        Material(Material &&other) noexcept;
        Material &operator=(const Material &other);
        Material &operator=(Material &&other) noexcept;
        ~Material();
        void ChangeShader(Shader *shader);
        void SetFloat(const String &name, const float &f);
        void SetUint(const String &name, const u32 &value);
        void SetVector(const String &name, const Vector4f &vector);
        void SetMatrix(const String &name, const Matrix4x4f &matrix);
        float GetFloat(const String &name);
        ShaderVariantHash ActiveVariantHash(u16 pass_index) const;
        std::set<String> ActiveKeywords(u16 pass_index) const;
        u16 RenderQueue() const { return _render_queue; };
        void RenderQueue(u16 new_queue) { _render_queue = new_queue; };
        u32 GetUint(const String &name);
        Vector4f GetVector(const String &name);
        void RemoveTexture(const String &name);
        virtual void SetTexture(const String &name, Texture *texture);
        virtual void SetTexture(const String &name, const WString &texture_path);
        virtual void SetTexture(const String &name, RTHandle texture);
        void EnableKeyword(const String &keyword);
        void DisableKeyword(const String &keyword);
        virtual void Bind(u16 pass_index = 0);
        Shader *GetShader() const { return _p_shader; };
        bool IsReadyForDraw() const;
        List<std::tuple<String, float>> GetAllFloatValue();
        List<std::tuple<String, Vector4f>> GetAllVectorValue();
        List<std::tuple<String, u32>> GetAllUintValue();

    private:
        void Construct(bool first_time);
        void ConstructKeywords(Shader *shader);
        void UpdateBindTexture(u16 pass_index, ShaderVariantHash new_hash);

    protected:
        inline static u32 s_total_material_num = 0u;
        inline static u32 s_current_cbuf_offset = 0u;
        u16 _standard_pass_index = -1;
        u16 _render_queue = 2000;
        Vector<u16> _mat_cbuf_per_pass_size;
        Shader *_p_shader;
        Shader *_p_active_shader;
        //运行时每个pass的关键字信息
        Vector<PassVariantInfo> _pass_variants;
        //跟随材质持久化的关键字
        std::set<String> _all_keywords;
        Vector<Scope<IConstantBuffer>> _p_cbufs;
        Vector<Map<String, std::tuple<u8, Texture *>>> _textures_all_passes{};
        //非shader使用的变量
        Map<String, u32> _common_uint_property;
    };

    enum class ETextureUsage : u8
    {
        kAlbedo = 0,
        kNormal,
        kEmission,
        kRoughness,
        kMetallic,
        kSpecular
    };

    DECLARE_ENUM(EMaterialID, kStandard, kSubsurface)
    DECLARE_ENUM(ESurfaceType, kOpaque, kTransparent, kAlphaTest)

    class AILU_API StandardMaterial : public Material
    {
        DECLARE_PRIVATE_PROPERTY(material_id, MaterialID, EMaterialID::EMaterialID)
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
                        break;
                }
            }
        };
        explicit StandardMaterial(String name);
        ~StandardMaterial();
        void MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use);
        bool IsTextureUsed(ETextureUsage use_info);
        virtual void Bind(u16 pass_index = 0) override;
        virtual void SetTexture(const String &name, Texture *texture);
        virtual void SetTexture(const String &name, const WString &texture_path);
        virtual void SetTexture(const String &name, RTHandle texture);
        void SetTexture(ETextureUsage usage, Texture *tex);
        const Texture *MainTex(ETextureUsage usage) const;
        const SerializableProperty &MainProperty(ETextureUsage usage);
        const ESurfaceType::ESurfaceType &SurfaceType() const { return _surface; }
        void SurfaceType(const ESurfaceType::ESurfaceType &value);

    private:
        ESurfaceType::ESurfaceType _surface;
        u16 _sampler_mask_offset = 0u;
        u16 _material_id_offset = 0u;
    };
}// namespace Ailu

#endif// !MATERIAL_H__
