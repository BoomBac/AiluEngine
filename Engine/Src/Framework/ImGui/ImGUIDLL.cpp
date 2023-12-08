#include "pch.h"
#include "Framework/ImGui/ImGUIDLL.h"


IMGUI_API bool ImGuiBegin(const char* name)
{
	return ImGui::Begin(name);
}

IMGUI_API void ImGuiEnd()
{
	ImGui::End();
}
