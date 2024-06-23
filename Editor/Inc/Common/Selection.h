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
				if (std::find(s_selections.begin(), s_selections.end(), obj) == s_selections.end())
				{
					s_selections.emplace_back(obj);
				}
			}
			static void AddAndRemovePreSelection(Object* obj)
			{
				if(!s_selections.empty())
					s_selections.pop_back();
				AddSelection(obj);
			}
			static void RemoveSlection(Object* obj)
			{
				std::remove_if(s_selections.begin(), s_selections.end(), [&](auto o) {
					return o == obj;
				});
			}
			static List<Object*>& Selections() { return s_selections; }
			template<typename T>
			static T* First();
		private:
			inline static List<Object*> s_selections{};
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

