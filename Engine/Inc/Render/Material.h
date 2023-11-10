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


namespace Ailu
{
	enum class ETextureUsage : uint8_t
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

	class Material : public Object
	{
		friend class ResourceMgr;
		DECLARE_REFLECT_FIELD(Material)
		DECLARE_PRIVATE_PROPERTY(origin_path,OriginPath,String)
		DECLARE_PRIVATE_PROPERTY(_b_internal,IsInternal,bool)
	public:
		Material(Ref<Shader> shader,std::string name);
		~Material();
		void MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos,bool b_use);
		bool IsTextureUsed(ETextureUsage use_info);
		void SetVector(const std::string& name, const Vector4f& vector);
		void SetTexture(const std::string& name, Ref<Texture> texture);
		void RemoveTexture(const std::string& name);
		void SetTexture(const std::string& name, const String& texture_path);
		virtual void Bind();
		Shader* GetShader() const;
		List<std::tuple<String, float>> GetAllFloatValue();
		List<std::tuple<String, Vector4f>> GetAllVectorValue();
		//Vector<String> _texture_paths{};
	protected:
		bool _b_internal = false;
	private:
		uint16_t _mat_cbuf_size = 0u;
		//std::set<ShaderBindResourceInfo, ShaderBindResourceInfoComparer> _mat_props{};
		std::unordered_set<ShaderBindResourceInfo,ShaderBindResourceInfoHash,ShaderBindResourceInfoEqual> _mat_props{};
		uint8_t* _p_data;
		//std::map<string, std::tuple<uint8_t, Ref<Texture>>> _textures{};
		std::map<String, std::tuple<uint8_t, Ref<Texture>,String>> _textures{};
		Ref<Shader> _p_shader;
		uint8_t* _p_cbuf = nullptr;
		//每个材质的cbuf大小一致，存储在一个大的buf，index记录其偏移量
		uint32_t _cbuf_index;
		inline static uint32_t s_current_cbuf_offset = 0u;
	};

	class MaterialPool 
	{
	public:
		static Ref<Material> CreateMaterial(Ref<Shader> shader, const std::string& name,const String& asset_path) 
		{
			auto material = CreateMaterial(shader, name);
			material->OriginPath(asset_path);
			return material;
		}

		static Ref<Material> CreateMaterial(Ref<Shader> shader, const std::string& name)
		{
			auto material = MakeRef<Material>(shader, name);
			s_materials[name] = material;
			s_next_material_id++;
			return material;
		}

		static void AddMaterial(Ref<Material> mat, const std::string& id)
		{
			s_materials[id] = mat;
			s_next_material_id++;
		}

		static Ref<Material> GetMaterial(const std::string& name)
		{
			auto it = s_materials.find(name);
			if (it != s_materials.end()) 
			{
				return it->second;
			}
			return nullptr;
		}

		static void ReleaseMaterial(const std::string& name)
		{
			auto it = s_materials.find(name);
			if (it != s_materials.end()) 
			{
				s_materials.erase(it);
			}
		}

		static std::list<Material*> GetAllMaterial()
		{
			std::list<Material*> mats{};
			for (auto& mat : s_materials) mats.emplace_back(mat.second.get());
			return mats;
		}
	private:
		inline static uint32_t s_next_material_id = 0u;
		inline static std::unordered_map<std::string, Ref<Material>> s_materials{};
	};
}

#endif // !MATERIAL_H__
