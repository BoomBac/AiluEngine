#pragma once
#ifndef __WINDOWSAPP_H__
#define __WINDOWSAPP_H__
#include "Inc/Framework/Interface/IApplication.h"

namespace Ailu
{
    namespace Engine
    {
        class AILU_API WindowsApp : public IApplication
        {
		public:
			WindowsApp(HINSTANCE hinstance,int argc);
			int Initialize() override;
			void Finalize() override;
			void Tick() override;

			void SetCommandLineParameters(int argc, char** argv) override;
			[[nodiscard]] int GetCommandLineArgumentsCount() const override;
			[[nodiscard]] const char* GetCommandLineArgument(int index) const override;
			[[nodiscard]] const GfxConfiguration& GetConfiguration() const override;

			void Run() override;
			static HWND GetMainWindowHandle();
		protected:
			static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
		private:
			inline static HWND _hwnd = nullptr;
			HINSTANCE _hinstance = nullptr;
			int _argc;
			char** _argv;
        };
    }
}


#endif // !WINDOWSAPP_H__

