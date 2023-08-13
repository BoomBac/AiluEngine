#pragma once
#ifndef __ENTRY_POINT_H__
#define __ENTRY_POINT_H__
#include "Inc/Framework/Common/CommandLineApp.h"
#include "Inc/Framework/Common/WindowsApp.h"

//#define __COMMANDLINE_APP_


#ifdef __COMMANDLINE_APP_

int main(int argc, char** argv)
{
	auto app = new Ailu::Engine::CommandLineApp();
	app->Initialize();
	app->Run();
	app->Finalize();
	delete app;
}
#else 
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hInstPrev, _In_ PSTR cmdline, _In_ int cmdshow)
{
	auto app = new Ailu::Engine::WindowsApp(hInst,cmdshow);
	app->Initialize();
	app->Run();
	app->Finalize();
	delete app;
}
#endif

#endif // !ENTRY_POINT_H__
