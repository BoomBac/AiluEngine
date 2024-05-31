#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include <map>
#include "Objects/Object.h"
#include <unordered_set>
#include "Shader.h"
#include "GlobalMarco.h"
#include "Texture.h"
#include "Framework/Common/Reflect.h"
#include "Buffer.h"


namespace Ailu
{
	enum class ETextureUsage : u8
	{
		kAlbedo = 0,kNormal,kEmssive,kRoughness,kSpecular,kMetallic
	};

	DECLARE_ENUM(EMaterialID, kStandard, kSubsurface)

	struct InternalStandardMaterialTexture
	{
		inline static String kAlbedo = "Albedo";
		inline static String kMetallic = "Metallic";
		inline static String kSpecular = "Specular";
		inline static String kEmssive = "Emssive";
		inline static String kRoughness = "Roughness";
		inline static String kNormal = "Normal";
	};

	class AILU_API Material : public Object
	{
		friend class ResourceMgr;
		DECLARE_REFLECT_FIELD(Material)
		DECLARE_PRIVATE_PROPERTY(b_internal,IsInternal,bool)
		DECLARE_PRIVATE_PROPERTY(material_id,MaterialID, EMaterialID::EMaterialID)
	public:
		DISALLOW_COPY_AND_ASSIGN(Material)
		Material(Shader* shader,String name);
		~Material();
		void ChangeShader(Shader* shader);
		void MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos,bool b_use);
		bool IsTextureUsed(ETextureUsage use_info);
		void SetFloat(const String& name, const float& f);
		void SetUint(const String& name, const u32& value);
		void SetVector(const String& name, const Vector4f& vector);
		float GetFloat(const String& name);
		Vector4f GetVector(const String& name);
		void RemoveTexture(const String& name);
		void SetTexture(const String& name, Texture* texture);
		void SetTexture(const String& name, const WString& texture_path);
		void SetTexture(const String& name, RTHandle texture);
		void EnableKeyword(const String& keyword);
		void DisableKeyword(const String& keyword);
		void Bind(u16 pass_index = 0);
		Shader* GetShader() const {return _p_shader;};
		//int type is as float,may cause some question
		List<std::tuple<String, float>> GetAllFloatValue();
		List<std::tuple<String, Vector4f>> GetAllVectorValue();
		List<std::tuple<String, u32>> GetAllUintValue();
		//List<std::tuple<String, Texture*>> GetAllTexture();
	protected:
		Asset* _p_asset_owned_this;
	private:
		void Construct(bool first_time);
	private:
		u16 _sampler_mask_offset = 0u;
		u16 _material_id_offset = 0u;
		//标准pass才会使用上面两个变量
		u16 _standard_pass_index = -1;
		Vector<u16> _mat_cbuf_per_pass_size;
		Shader* _p_shader;
		//u8* _p_cbuf_cpu;
		//u8* _p_cbuf = nullptr;
		////每个材质的cbuf大小一致，存储在一个大的buf，index记录其偏移量
		//u32 _cbuf_index;
		inline static u32 s_current_cbuf_offset = 0u;
		Vector<Scope<ConstantBuffer>> _p_cbufs;
		//std::unordered_set<ShaderBindResourceInfo, ShaderBindResourceInfoHash, ShaderBindResourceInfoEqual> _mat_props{};
		//value_name : <bind_slot,texture>
		Vector<Map<String, std::tuple<u8, Texture*>>> _textures_all_passes{};
	};

	class StandardMaterial : public Material
	{
	public:
		StandardMaterial(Shader* shader, String name);
		~StandardMaterial();
	private:
	};
}

#endif // !MATERIAL_H__
