#pragma once
#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include <stdint.h>
#include <map>
#include "GlobalMarco.h"
#include "AlgFormat.h"
#include "Framework/Common/Path.h"


namespace Ailu
{
	enum class ETextureType
	{
		kTexture2D = 0,kTextureCubeMap
	};
	class Texture
	{
	public:
		virtual ~Texture() = default;
		virtual uint8_t* GetNativePtr() = 0;
		virtual const uint16_t& GetWidth() const = 0;
		virtual const uint16_t& GetHeight() const = 0;
		virtual void Release() = 0;
		virtual void Bind(uint8_t slot) const = 0;
		virtual void Name(const std::string& name) = 0;
		virtual const std::string& Name() const = 0;
		virtual const ETextureType& GetTextureType() const = 0;
	};

	class Texture2D : public Texture
	{
	public:
		static Ref<Texture2D> Create(const uint16_t& width, const uint16_t& height,EALGFormat format = EALGFormat::kALGFormatRGB32_FLOAT);
		virtual void FillData(uint8_t* data);
		virtual void Bind(uint8_t slot) const override;
		const uint16_t& GetWidth() const final { return _width; };
		const uint16_t& GetHeight() const final { return _height; };
		uint8_t* GetNativePtr() override;
		const ETextureType& GetTextureType() const final;
	public:
		void Name(const std::string& name) override;
		const std::string& Name() const override;
	protected:
		std::string _name;
		uint8_t* _p_data;
		uint16_t _width,_height;
		uint8_t _channel;
		EALGFormat _format;
	};

	class TextureCubeMap : public Texture
	{
	public:
		static Ref<TextureCubeMap> Create(const uint16_t& width, const uint16_t& height, EALGFormat format = EALGFormat::kALGFormatRGB32_FLOAT);
		virtual void FillData(Vector<u8*>& data);
		virtual void Bind(u8 slot) const override;
		const uint16_t& GetWidth() const final { return _width; };
		const uint16_t& GetHeight() const final { return _height; };
		uint8_t* GetNativePtr() override;
		const ETextureType& GetTextureType() const final;
		void Name(const std::string& name) override { _name = name; };
		const std::string& Name() const override { { return _name; } };
	protected:
		std::string _name;
		Vector<u8*> _p_datas;
		uint16_t _width, _height;
		uint8_t _channel;
		EALGFormat _format;
	};

	class TexturePool
	{
	public:
		//name with ext。图片导入时生成一个可以直接使用的纹理，当导入一个同名纹理时，第二个纹理的gpu资源已经创建并且提交到cmd，
		// 但是由于存在同名，第二个纹理不会在此处被添加，那么指针就会被释放，执行cmd时便会报错！运行时一般不会有这个错误。
		// 初始化时，将图片的加载放在最前面
		static Ref<Texture2D> Add(const std::string& name, Ref<Texture2D> res)
		{
			if (s_res_pool.contains(name)) return std::dynamic_pointer_cast<Texture2D>(s_res_pool[name]);
			s_res_pool.insert(std::make_pair(name, res));
			res->Name(GetFileName(name));
			++s_res_num;
			s_is_dirty = true;
			return res;
		}

		static Ref<TextureCubeMap> Add(const std::string& name, Ref<TextureCubeMap> res)
		{
			if (s_res_pool.contains(name)) return std::dynamic_pointer_cast<TextureCubeMap>(s_res_pool[name]);
			s_res_pool.insert(std::make_pair(name, res));
			res->Name(GetFileName(name));
			++s_res_num;
			s_is_dirty = true;
			return res;
		}

		static Ref<Texture> Get(const std::string& name);
		static Ref<Texture2D> GetTexture2D(const std::string& name);
		static Ref<TextureCubeMap> GetCubemap(const std::string& name);

		static Ref<Texture2D> GetDefaultWhite()
		{
			auto it = s_res_pool.find(EnginePath::kEngineTexturePath + "MyImage01.jpg");
			if (it != s_res_pool.end()) return std::dynamic_pointer_cast<Texture2D>(it->second);
			else return nullptr;
		}

		static uint32_t GetResNum()
		{
			return s_res_num;
		}
		static std::map<std::string, Ref<Texture>>& GetAllResourceMap() { return s_res_pool; };
	protected:
		inline static std::map<std::string, Ref<Texture>> s_res_pool{};
		inline static bool s_is_dirty = true;
		inline static uint32_t s_res_num = 0u;
	};
}

#endif // !TEXTURE_H__

