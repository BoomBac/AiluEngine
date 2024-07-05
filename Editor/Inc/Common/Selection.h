#pragma once
#ifndef __SELECTION_H__
#define __SELECTION_H__
#include "Objects/SceneActor.h"
namespace Ailu
{
	namespace Editor
	{
		class Selection
		{
		public:
			static void AddSelection(Object* obj)
			{
                if(!s_can_select)
                    return ;
				if (std::find(s_selections.begin(), s_selections.end(), obj) == s_selections.end())
				{
					s_selections.emplace_back(obj);
				}
			}
			static void AddAndRemovePreSelection(Object* obj)
			{
                if(!s_can_select)
                    return ;
				if(!s_selections.empty())
					s_selections.pop_back();
				AddSelection(obj);
			}
			static void RemoveSlection(Object* obj)
			{
				std::erase_if(s_selections, [&](auto o)->bool {
					return o == obj;
				});
			}
			static List<Object*>& Selections() { return s_selections; }
			template<typename T>
			static T* First();
            static void Active(bool can_select) {s_can_select = can_select;}
            static bool IsActive() {return s_can_select;}
		private:
			inline static List<Object*> s_selections{};
            inline static bool s_can_select = true;
		};
		template<typename T>
		inline T* Selection::First()
		{
			for (auto obj : s_selections)
			{
				auto ptr = dynamic_cast<T*>(obj);
				if (ptr)
				{
					return ptr;
				}
			}
			return nullptr;
		}
	}
}

#endif // !SELECTION_H__

