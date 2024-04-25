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
#include "Framework/Interface/IAssetable.h"


namespace Ailu
{
	enum class ETextureUsage : u8
	{
		kAlbedo = 0,kNormal,kEmssive,kRoughness,kSpecular,kMetallic
	};

	struct InternalStandardMaterialTexture
	{
		inline static String kAlbedo = "Albedo";
		inline static String kMetallic = "Metallic";
		inline static String kSpecular = "Specular";
		inline static String kEmssive = "Emssive";
		inline static String kRoughness = "Roughness";
		inline static String kNormal = "Normal";
	};

	class Material : public Object,public IAssetable
	{
		friend class ResourceMgr;
		DECLARE_REFLECT_FIELD(Material)
		DECLARE_PRIVATE_PROPERTY(origin_path,OriginPath,String)
		DECLARE_PRIVATE_PROPERTY(b_internal,IsInternal,bool)
	public:
		Material(Ref<Shader> shader,String name);
		~Material();
		void ChangeShader(Ref<Shader> shader);
		void MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos,bool b_use);
		bool IsTextureUsed(ETextureUsage use_info);
		void SetFloat(const String& name, const float& f);
		void SetUint(const String& name, const u32& value);
		void SetVector(const String& name, const Vector4f& vector);
		float GetFloat(const String& name);
		Vector4f GetVector(const String& name);
		void RemoveTexture(const String& name);
		void SetTexture(const String& name, Ref<Texture> texture);
		void SetTexture(const String& name, const WString& texture_path);
		void EnableKeyword(const String& keyword);
		void DisableKeyword(const String& keyword);
		virtual void Bind();
		Shader* GetShader() const;
		//int type is as float,may cause some question
		List<std::tuple<String, float>> GetAllFloatValue();
		List<std::tuple<String, Vector4f>> GetAllVectorValue();
		List<std::tuple<String, u32>> GetAllUintValue();
		//List<std::tuple<String, Texture*>> GetAllTexture();
		//为了持久化
		const Guid& GetGuid() const final;
		void AttachToAsset(Asset* owner) final;
	protected:
		Asset* _p_asset_owned_this;
	private:
		void Construct(bool first_time);
	private:
		u16 _sampler_mask_offset = 0u;
		uint16_t _mat_cbuf_size = 0u;
		Ref<Shader> _p_shader;
		//u8* _p_cbuf_cpu;
		//u8* _p_cbuf = nullptr;
		////每个材质的cbuf大小一致，存储在一个大的buf，index记录其偏移量
		//u32 _cbuf_index;
		inline static u32 s_current_cbuf_offset = 0u;
		Scope<ConstantBuffer> _p_cbuf;
		//std::unordered_set<ShaderBindResourceInfo, ShaderBindResourceInfoHash, ShaderBindResourceInfoEqual> _mat_props{};
		//value_name : <bind_slot,texture>
		std::map<String, std::tuple<u8, Ref<Texture>>> _textures{};
	};

	class StandardMaterial : public Material
	{
	public:
		StandardMaterial(Ref<Shader> shader, String name);
		~StandardMaterial();
	private:
	};

	class MaterialLibrary 
	{
	public:
		static Ref<Material> CreateMaterial(Ref<Shader> shader, const String& name,const String& asset_path) 
		{
			auto material = CreateMaterial(shader, name);
			material->OriginPath(asset_path);
			return material;
		}

		static Ref<Material> CreateMaterial(Ref<Shader> shader, const String& name)
		{
			auto material = MakeRef<Material>(shader, name);
			s_materials[name] = material;
			s_it_materials.emplace_back(material);
			s_next_material_id++;
			return material;
		}

		static void AddMaterial(Ref<Material> mat, const String& id)
		{
			s_materials[id] = mat;
			s_it_materials.emplace_back(mat);
			s_next_material_id++;
		}
		static bool Contain(const String& nameid)
		{
			return s_materials.contains(nameid);
		}
		static Ref<Material> GetMaterial(const String& name)
		{
			auto it = s_materials.find(name);
			if (it != s_materials.end()) 
			{
				return it->second;
			}
			return nullptr;
		}
		static void ReleaseMaterial(const String& name)
		{
			auto it = s_materials.find(name);
			if (it != s_materials.end()) 
			{
				s_materials.erase(it);
			}
		}
		static auto Begin() 
		{
			return s_it_materials.begin();
		}
		static auto End()
		{
			return s_it_materials.end();
		}
	private:
		inline static u32 s_next_material_id = 0u;
		inline static std::unordered_map<String, Ref<Material>> s_materials{};
		inline static Vector<Ref<Material>> s_it_materials{};
	};
}

#endif // !MATERIAL_H__
