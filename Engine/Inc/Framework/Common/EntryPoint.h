#pragma once
#ifndef __ENTRY_POINT_H__
#define __ENTRY_POINT_H__
#include "pch.h"
#include "Framework/Common/CommandLineApp.h"
#include "Platform/WinWindow.h"

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
	Ailu::WinWindow::_sp_hinstance = hInst;
	Ailu::WinWindow::_s_argc = cmdshow;
	auto window = new Ailu::WinWindow(Ailu::WindowProps());
	window->OnUpdate();
	delete window;
}
#endif

#endif // !ENTRY_POINT_H__
