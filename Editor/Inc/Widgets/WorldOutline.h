#pragma once
#ifndef __WORLDOUTLINE_H__
#define __WORLDOUTLINE_H__
#include "ImGuiWidget.h"
#include "Scene/Entity.hpp"
namespace Ailu
{
	namespace Editor
	{
		class WorldOutline : public	ImGuiWidget
		{
		public:
			WorldOutline();
			~WorldOutline();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
            void OnEvent(Event& e) final;
		private:
			void ShowImpl() final;
            void DrawTreeNode(ECS::Entity entity);
		private:
            std::set<ECS::Entity> _drawed_entity;
            ECS::Entity _selected_entity = ECS::kInvalidEntity;
            void OnOutlineDoubleClicked(ECS::Entity entity);
            ECS::Register *_scene_register = nullptr;
		};
	}
}

#endif // !__WORLDOUTLINE_H__

