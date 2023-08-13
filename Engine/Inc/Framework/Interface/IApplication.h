#ifndef __IAPPLICATION_H__
#define __IAPPLICATION_H__
#pragma once
#include "IRuntimeModule.h"
#include "Inc/Framework/Common/GfxConfiguration.h"

namespace Ailu
{
	namespace Engine
	{
		class AILU_API IApplication : public IRuntimeModule
		{
		public:
			int Initialize() override = 0;
			void Finalize() override = 0;
			void Tick() override = 0;

			virtual void SetCommandLineParameters(int argc, char** argv) = 0;
			[[nodiscard]] virtual int GetCommandLineArgumentsCount() const = 0;
			[[nodiscard]] virtual const char* GetCommandLineArgument(int index) const = 0;
			[[nodiscard]] virtual const GfxConfiguration& GetConfiguration() const = 0;

			virtual void Run() = 0;
		protected:
			bool _b_exit = false;
		};
	}
}


#endif // !IAPPLICATION_H__

