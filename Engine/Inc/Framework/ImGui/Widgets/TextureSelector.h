#pragma once
#ifndef __TEXTURE_SELECTOR_H__
#define __TEXTURE_SELECTOR_H__
#include "ImGuiWidget.h"
namespace Ailu
{
	class TextureSelector : public ImGuiWidget
	{
	public:
		TextureSelector();
		~TextureSelector();
		void Open(const i32& handle) final;
		void Close() final;
		const WString& GetSelectedTexture(i32 handle) const;
	private:
		void ShowImpl() final;
		i32 _selected_img_index = -1;
		WString _cur_selected_texture_path = kNull;
	};
}

#endif // !TEXTURE_SELECTOR_H__

