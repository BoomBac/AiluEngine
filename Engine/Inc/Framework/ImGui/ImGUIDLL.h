#pragma once
#ifndef __IMGUI_DLL_H__
#define __IMGUI_DLL_H__

#ifdef AILU_BUILD_DLL
#define IMGUI_API __declspec(dllexport)
#else
#define IMGUI_API __declspec(dllimport)
#endif

#include "Ext/imgui/imgui.h"

extern "C" {
	IMGUI_API bool ImGuiBegin(const char* name);
	IMGUI_API void ImGuiEnd();
};


#endif // !IMGUI_DLL_H__

