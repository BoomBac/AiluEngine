#pragma once
#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include <stdint.h>
#include <map>
#include "GlobalMarco.h"
#include "Framework/Common/LogMgr.h"
#include "AlgFormat.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ThreadPool.h"

#include "Framework/Common/Reflect.h"



namespace Ailu
{
	namespace TextureUtils
	{
		static uint8_t* ExpandImageDataToFourChannel(uint8_t* p_data, size_t size, uint8_t alpha = 255u)
		{
			uint8_t* new_data = new uint8_t[size / 3 * 4];
			auto pixel_num = size / 3;
			for (size_t i = 0; i < pixel_num; i++)
			{
				memcpy(new_data + i * 4, p_data + i * 3, 3);
				new_data[i * 4 + 3] = alpha;
			}
			return new_data;
		}

		static u8* DownSample(u8* image, int width, int height, int channels)
		{
			constexpr static int sub_task_block_size = 128;
			constexpr static bool use_multithread = false;
			int row_size = channels * width;
			int new_width = width >> 1;
			int new_height = height >> 1;
			int new_img_size = new_width * new_height * channels;
			u8* new_image = new u8[new_img_size];
			auto downsample_sub_task = [&](int xbegin, int xend, int ybegin, int yend, int new_width)->bool {
				u8 blend_color[4]{};
				for (int i = ybegin; i < yend; ++i)
				{
					for (int j = xbegin; j < xend; ++j)
					{
						// 计算混合颜色的坐标
						int a_i = i * 2;
						int a_j = j * 2;
						// 获取当前像素及其右侧像素
						u8* a = &image[a_i * row_size + a_j * channels];
						u8* b = &image[a_i * row_size + (a_j + 1) * channels];
						// 计算齐次加权平均值
						for (int k = 0; k < channels; ++k)
							blend_color[k] = static_cast<u8>((a[k] + b[k]) / 2);
						int new_index = i * new_width * channels + j * channels;
						memcpy(new_image + new_index, blend_color, sizeof(blend_color));
					}
				}
				return true;
				};
			if (use_multithread)
			{
				int task_num = new_width / sub_task_block_size;
				task_num = task_num == 0 ? 1 : task_num;
				if (task_num == 1)
					downsample_sub_task(0, new_width, 0, new_height, new_width);
				else
				{
					Vector<std::future<bool>> task_status{};
					for (int i = 0; i < task_num; i++)
					{
						for (int j = 0; j < task_num; j++)
						{
							task_status.emplace_back(g_thread_pool->Enqueue(downsample_sub_task, j * sub_task_block_size, (j + 1) * sub_task_block_size,
								i * sub_task_block_size, (i + 1) * sub_task_block_size, new_width));
							//downsample_sub_task(j * sub_task_block_size, (j + 1) * sub_task_block_size, i * sub_task_block_size, (i + 1) * sub_task_block_size, new_width);
						}
					}
					for (auto& complete : task_status)
					{
						complete.get();
					}
				}
			}
			else
				downsample_sub_task(0, new_width, 0, new_height, new_width);
			return new_image;
		}
	}

	enum class ETextureType
	{
		kTexture2D = 0,kTextureCubeMap,kRenderTexture
	};
	class Texture
	{
	public:
		virtual ~Texture() = default;
		virtual uint8_t* GetCPUNativePtr() = 0;
		virtual void* GetGPUNativePtr() = 0;
		virtual void* GetNativeCPUHandle() = 0;
		virtual const uint16_t& GetWidth() const = 0;
		virtual const uint16_t& GetHeight() const = 0;
		virtual void Release() = 0;
		virtual void Bind(uint8_t slot) = 0;
		virtual void Name(const std::string& name) = 0;
		virtual const std::string& Name() const = 0;
		virtual const ETextureType GetTextureType() const = 0;
		virtual const u8& GetMipmap() const = 0;
		virtual EALGFormat GetFormat() const = 0;
	};

	struct TextureDesc
	{
		u16 _width, _height;
		u8 _mipmap;
		EALGFormat _res_format;
		EALGFormat _srv_format;
		EALGFormat _uav_format;
		bool _read_only;
		TextureDesc() : _width(0),_height(0),_mipmap(1),_res_format(EALGFormat::kALGFormatUnknown),_srv_format(EALGFormat::kALGFormatUnknown),_uav_format(EALGFormat::kALGFormatUnknown), _read_only(false){}
		TextureDesc(u16 w,u16 h,u8 mipmap,EALGFormat res_format,EALGFormat srv_format,EALGFormat uav_format,bool read_only) : _width(w), _height(h), _mipmap(mipmap), 
		_res_format(res_format), _srv_format(srv_format), _uav_format(uav_format), _read_only(read_only) 
		{
		}
	};

	class Texture2D : public Texture
	{
		DECLARE_PROTECTED_PROPERTY(path,AssetPath,String)
		DECLARE_PROTECTED_PROPERTY(channel,Channel,u8)
	public:
		static Ref<Texture2D> Create(const uint16_t& width, const uint16_t& height, EALGFormat res_format = EALGFormat::kALGFormatR8G8B8A8_UNORM,bool read_only = true);
		static Ref<Texture2D> Create(const TextureDesc& desc);
		virtual void FillData(uint8_t* data);
		virtual void FillData(Vector<u8*> datas);
		virtual void Bind(uint8_t slot) override;
		const uint16_t& GetWidth() const final { return _width; };
		const uint16_t& GetHeight() const final { return _height; };
		uint8_t* GetCPUNativePtr() override;
		void* GetNativeCPUHandle() override;
		void* GetGPUNativePtr()override;
		const ETextureType GetTextureType() const final;
		const u8& GetMipmap() const final { return _mipmap_count; };
		EALGFormat GetFormat() const { return _format; };
	public:
		void Name(const std::string& name) override;
		const std::string& Name() const override;
	protected:
		std::string _name;
		Vector<u8*> _p_datas;
		uint16_t _width,_height;
		EALGFormat _format;
		u8 _mipmap_count = 1u;
		bool _read_only;
	};

	class TextureCubeMap : public Texture
	{
		DECLARE_PROTECTED_PROPERTY(path, AssetPath, Vector<String>)
	public:
		static Ref<TextureCubeMap> Create(const uint16_t& width, const uint16_t& height, EALGFormat format = EALGFormat::kALGFormatR8G8B8A8_UNORM);
		virtual void FillData(Vector<u8*>& data);
		virtual void Bind(uint8_t slot) override;
		const uint16_t& GetWidth() const final { return _width; };
		const uint16_t& GetHeight() const final { return _height; };
		uint8_t* GetCPUNativePtr() override;
		void* GetNativeCPUHandle() override;
		void* GetGPUNativePtr()override;
		const ETextureType GetTextureType() const final;
		void Name(const std::string& name) override { _name = name; };
		const std::string& Name() const override { { return _name; } };
		const u8& GetMipmap() const final { return _mipmap_count; };
		EALGFormat GetFormat() const { return _format; };
	protected:
		std::string _name;
		Vector<u8*> _p_datas;
		uint16_t _width, _height;
		uint8_t _channel;
		EALGFormat _format;
		u8 _mipmap_count;
	};

	enum class ETextureResState : u8
	{
		kDefault, kColorTagret, kShaderResource,kDepthTarget
	};

	struct RTHandle
	{
	public:
		u32 _id;
		String _name;
		RTHandle(const String& name) : _name(name)
		{
			_id = s_global_rt_id++;
		};
		RTHandle() : RTHandle("TempRT_" + s_global_rt_id) {};
	private:
		inline static u32 s_global_rt_id = 0u;
	};

	class RenderTexture : public Texture
	{
	public:
		static Ref<RenderTexture> Create(const uint16_t& width, const uint16_t& height, String name,EALGFormat format = EALGFormat::kALGFormatR8G8B8A8_UNORM,bool is_cubemap = false);
		virtual void Bind(uint8_t slot) override;
		const uint16_t& GetWidth() const final { return _width; };
		const uint16_t& GetHeight() const final { return _height; };
		//return rtv handle
		void* GetNativeCPUHandle() override;
		//return rtv handle
		virtual void* GetNativeCPUHandle(u16 index);
		void* GetGPUNativePtr() override;
		const ETextureType GetTextureType() const final;
		virtual void Transition(ETextureResState state);
		const RTHandle& GetRTHandle() const { return _rt_handle; }
		void Name(const std::string& name) override { _name = name; };
		const std::string& Name() const override { return _name; };
		const u8& GetMipmap() const final { return _mipmap_count; };
		EALGFormat GetFormat() const { return _format; };
		ETextureResState GetState() const { return _state; }
		bool operator==(const RenderTexture& other) const { return _rt_handle._id == other._rt_handle._id; }
	protected:
		std::string _name;
		RTHandle _rt_handle;
		ETextureResState _state;
		uint16_t _width, _height;
		bool _is_cubemap;
		uint8_t _channel;
		EALGFormat _format;
		u8 _mipmap_count;
	};

	class TexturePool
	{
	public:
		//name with ext。图片导入时生成一个可以直接使用的纹理，当导入一个同名纹理时，第二个纹理的gpu资源已经创建并且提交到cmd，
		// 但是由于存在同名，第二个纹理不会在此处被添加，那么指针就会被释放，执行cmd时便会报错！运行时一般不会有这个错误。
		// 初始化时，将图片的加载放在最前面
		static Ref<Texture2D> Add(const String& asset_path, Ref<Texture2D> res)
		{
			if (res != nullptr)
			{
				if (s_res_pool.contains(asset_path)) 
					return std::dynamic_pointer_cast<Texture2D>(s_res_pool[asset_path]);
				s_res_pool.insert(std::make_pair(asset_path, res));
				++s_res_num;
				s_is_dirty = true;
				return res;
			}
			else
			{
				g_pLogMgr->LogWarningFormat("Add texture with name {} to pool failed! texture is null!", asset_path);
			}
			return nullptr;
		}

		static Ref<TextureCubeMap> Add(const std::string& name, Ref<TextureCubeMap> res)
		{
			if (res)
			{
				if (s_res_pool.contains(name)) return std::dynamic_pointer_cast<TextureCubeMap>(s_res_pool[name]);
				s_res_pool.insert(std::make_pair(name, res));
				res->Name(PathUtils::GetFileName(name));
				++s_res_num;
				s_is_dirty = true;
				return res;
			}
			else
			{
				g_pLogMgr->LogWarningFormat("Add texture with name {} to pool failed! texture is null!", name);
			}
			return nullptr;
		}

		static Ref<Texture> Get(const std::string& name);
		static Ref<Texture2D> GetTexture2D(const std::string& name);
		static Ref<TextureCubeMap> GetCubemap(const std::string& name);

		static bool Contain(const String& asset_path)
		{
			return s_res_pool.contains(asset_path);
		}

		static Ref<Texture2D> GetDefaultWhite()
		{
			auto it = s_res_pool.find("Runtime/default_white");
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

