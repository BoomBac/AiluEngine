#pragma once
#ifndef __IRUNTIME_MODULE_H__
#define __IRUNTIME_MODULE_H__
#include "Inc/GlobalMarco.h"

namespace Ailu
{
	class AILU_API IRuntimeModule
	{
	public:
		virtual ~IRuntimeModule() = default;
		virtual int Initialize() = 0;
		virtual void Finalize() = 0;
		virtual void Tick() = 0;
	};
}

#endif // !__IRUNTIME_MODULE_H__
