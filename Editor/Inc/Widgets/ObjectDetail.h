#pragma once
#ifndef __OBJECTDETAIL_H__
#define __OBJECTDETAIL_H__
#include "ImGuiWidget.h"
#include "Scene/Entity.hpp"
namespace Ailu
{
	class SceneActor;
	class Shader;
	namespace Editor
	{
		class TextureSelector;
		class ObjectDetail : public	ImGuiWidget
		{
		public:
			ObjectDetail();
			~ObjectDetail();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
		private:
			void ShowImpl() final;
			void ShowSceneActorPannel(ECS::Entity entity);
			void ShowShaderPannel(Shader* s);
		private:
			TextureSelector* _p_texture_selector = nullptr;
			u32 _property_handle = 0u;
		};
	}
}

#endif // !OBJECTDETAIL_H__

