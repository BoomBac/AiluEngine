#pragma once
#ifndef __WORLDOUTLINE_H__
#define __WORLDOUTLINE_H__
#include "ImGuiWidget.h"
namespace Ailu
{
	class SceneActor;
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
			void DrawTreeNode(SceneActor* actor,bool is_root);
		private:
			SceneActor* _selected_actor = nullptr;
            void OnOutlineDoubleClicked(SceneActor* actor);
		};
	}
}

#endif // !__WORLDOUTLINE_H__

