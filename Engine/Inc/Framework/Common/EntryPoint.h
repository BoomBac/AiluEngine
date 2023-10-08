#pragma once
#ifndef __ENTRY_POINT_H__
#define __ENTRY_POINT_H__
#include "pch.h"
#include "Framework/Common/CommandLineApp.h"
#include "Platform/WinWindow.h"
#include "Framework/Common/Application.h"

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
	Ailu::Application* app = new Ailu::Application();
	app->Initialize();
	app->Tick(16.6f);
	app->Finalize();
	DESTORY_PTR(app)
}
#endif

#endif // !ENTRY_POINT_H__
