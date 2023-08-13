#ifndef __COMMANDLINE_APP_H___
#define __COMMANDLINE_APP_H___
#pragma once
#include "Inc/GlobalMarco.h"
#include "Inc/Framework/Interface/IApplication.h"
#include <iostream>

namespace Ailu
{
    namespace Engine
    {
        class AILU_API CommandLineApp : public IApplication
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

			void SetCommandLineParameters(int argc, char** argv) override {};
			[[nodiscard]] int GetCommandLineArgumentsCount() const override 
			{
				return 0;
			};
			[[nodiscard]] const char* GetCommandLineArgument(int index) const override
			{
				return nullptr;
			}
			[[nodiscard]] const GfxConfiguration& GetConfiguration() const override 
			{
				static GfxConfiguration cfg;
				return cfg;
			};

			void Run() override
			{
				while (true)
				{

				}
			}
        };
    }
}


#endif // !COMMANDLINE_APP_H___

