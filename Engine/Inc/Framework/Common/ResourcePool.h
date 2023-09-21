#pragma once
#ifndef __RES_POOL_H__
#define __RES_POOL_H__

#include <string>
#include <map>
#include "GlobalMarco.h"

namespace Ailu
{
	template<class T>
	class ResourcePool
	{
	public:
		static void Add(const std::string& name, Ref<T> res)
		{
			if (s_res_pool.insert(std::make_pair(name, res)).second) ++s_res_num;
		}
		static Ref<T> Get(const std::string& name)
		{
			auto it = s_res_pool.find(name);
			if (it != s_res_pool.end()) return it->second;
			else return nullptr;
		}
		static uint32_t GetResNum() 
		{
			return s_res_num;
		}
	protected:
		inline static std::map<std::string,Ref<T>> s_res_pool{};
		inline static uint32_t s_res_num = 0u;
	};

	static inline std::string GetResPath(const std::string& sub_path)
	{
		return STR2(RES_PATH) + sub_path;
	}

	static std::string GetFileName(const std::string_view& filePath)
	{
		size_t found = filePath.find_last_of("/\\");
		if (found != std::string::npos) 
		{
			return filePath.substr(found + 1).data();
		}
		else 
		{
			return filePath.data();
		}
	}
}


#endif // !RES_POOL_H__

