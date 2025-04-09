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
        struct PassVariantInfo
        {
            String _pass_name;
            ShaderVariantHash _variant_hash;
            std::set<String> _keywords;
        };

    public:
        inline static std::weak_ptr<Material> s_standard_lit;
        inline static std::weak_ptr<Material> s_checker;

        Material(Shader *shader, String name);

        Material(const Material &other);
        Material &operator=(const Material &other);
        Material &operator=(Material &&other) noexcept;
        Material(Material &&other) noexcept;

        ~Material();
        void ChangeShader(Shader *shader);
        void SetFloat(const String &name, const float &f);
        void SetUint(const String &name, const u32 &value);
        void SetVector(const String &name, const Vector4f &vector);
        void SetVector(const String &name, const Vector4Int &vector);
        void SetMatrix(const String &name, const Matrix4x4f &matrix);
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
        /// 构造 drawcmd时调用，将当前状态推入队列
        void PushState(u16 pass_index = 0u);
    protected:
        virtual void Construct(bool first_time);

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
        Vector<Scope<ConstantBuffer>> _p_cbufs;
        Vector<Map<String, std::tuple<u8, Texture *>>> _textures_all_passes{};
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

    DECLARE_ENUM(EMaterialID, kStandard, kSubsurface, kChecker)
    DECLARE_ENUM(ESurfaceType, kOpaque, kTransparent, kAlphaTest)

    class AILU_API StandardMaterial : public Material
    {
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
            }
        };
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
        const ESurfaceType::ESurfaceType &SurfaceType() const { return _surface; }
        void SurfaceType(const ESurfaceType::ESurfaceType &value);
        const EMaterialID::EMaterialID &MaterialID() const { return _material_id; }
        void MaterialID(const EMaterialID::EMaterialID &value);

    private:
        ESurfaceType::ESurfaceType _surface = ESurfaceType::kOpaque;
        EMaterialID::EMaterialID _material_id = EMaterialID::kStandard;
        u16 _sampler_mask_offset = 0u;
        u16 _material_id_offset = 0u;
    };
}// namespace Ailu

#endif// !MATERIAL_H__
