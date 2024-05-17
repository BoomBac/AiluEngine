#pragma once
#ifndef __TEXTURE_SELECTOR_H__
#define __TEXTURE_SELECTOR_H__
#include "ImGuiWidget.h"
namespace Ailu
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
}

#endif // !TEXTURE_SELECTOR_H__

