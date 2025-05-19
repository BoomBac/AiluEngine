#pragma once
#ifndef __TEXTURE_SELECTOR_H__
#define __TEXTURE_SELECTOR_H__
#include "ImGuiWidget.h"
namespace Ailu
{
	namespace Render
	{
		class Texture;
	}
	namespace Editor
	{
#undef max
		class TextureSelector : public ImGuiWidget
		{
		public:
			inline static constexpr u32 kInvalidTextureID = std::numeric_limits<u32>::max();
			static bool IsValidTextureID(u32 id) { return id != kInvalidTextureID; }
			TextureSelector();
			~TextureSelector();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
			u32 GetSelectedTexture(i32 handle) const;
		private:
			void ShowImpl() final;
			i32 _selected_img_index = -1;
			u32 _cur_selected_texture_id = kInvalidTextureID;
		};

		class RenderTextureView : public ImGuiWidget
		{
		public:
			RenderTextureView();
			~RenderTextureView();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
		private:
			void ShowImpl() final;
		};

		class TextureDetailView : public ImGuiWidget
		{
		public:
			TextureDetailView();
			~TextureDetailView();
			void Open(const i32& handle) final;
			void Open(const i32& handle, Render::Texture* tex);
			void Close(i32 handle) final;
		private:
			void ShowImpl() final;
		private:
			inline static const char* s_cubeface_str[] = { "+Y", "-X", "+Z", "+X","-Z","-Y" };
			inline static const char* s_mipmap_str[] = { "mipmap0","mipmap1", "mipmap2", "mipmap3", "mipmap4", "mipmap5", "mipmap6", "mipmap7", "mipmap8", "mipmap9", "mipmap10", "mipmap11" };
			Render::Texture* _p_tex;
			i32 _cur_mipmap_level = 0;
		};
	}
}

#endif // !TEXTURE_SELECTOR_H__

