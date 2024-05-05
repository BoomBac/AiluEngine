#pragma once
#ifndef __OBJECTDETAIL_H__
#define __OBJECTDETAIL_H__
#include "ImGuiWidget.h"
namespace Ailu
{
	class SceneActor;
	class TextureSelector;
	class ObjectDetail : public	ImGuiWidget
	{
	public:
		ObjectDetail(SceneActor** pp_selected_actor);
		~ObjectDetail();
		void Open(const i32& handle) final;
		void Close(i32 handle) final;
	private:
		void ShowImpl() final;
	private:
		SceneActor** _pp_actor = nullptr;
		TextureSelector* _p_texture_selector = nullptr;
		u32 _property_handle = 0u;
	};
}

#endif // !OBJECTDETAIL_H__

