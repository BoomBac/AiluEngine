#ifndef __COMMANDLINE_APP_H___
#define __COMMANDLINE_APP_H___
#pragma once
#include "Inc/GlobalMarco.h"
#include "Framework/Interface/IRuntimeModule.h"
#include <iostream>

namespace Ailu
{
	class AILU_API CommandLineApp : public IRuntimeModule
	{
	public:
		int Initialize() override
		{
			std::cout << "Ailu Engine Initialize" << std::endl;
			return 0;
		};
		void Finalize() override
		{
			std::cout << "Ailu Engine Finalize" << std::endl;
		};
		void Tick() override {};
	};
}


#endif // !COMMANDLINE_APP_H___

