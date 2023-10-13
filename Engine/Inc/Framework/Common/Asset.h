#pragma once
#ifndef __ASSET_H__
#define __ASSET_H__
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
	class Asset
	{
	public:
		inline static uint32_t s_instance_num = 0u;
	public:
		Asset();
		Asset(string asset_path,string system_path);
		virtual ~Asset();
		string _asset_path;
		string _system_path;
		string _name;
		uint32_t _instance_id;
		Guid _guid;
	};
}

#endif // !ASSET_H__

