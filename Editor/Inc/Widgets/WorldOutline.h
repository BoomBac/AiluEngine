#include "Framework/ImGui/ImGuiLayer.h"


class WorldOutline : public Ailu::ImGUILayer
{
public:
	WorldOutline();
	WorldOutline(const String& name);
	~WorldOutline();

	void OnAttach() override;
	void OnDetach() override;
	void OnEvent(Ailu::Event& e) override;
	void OnImguiRender() override;
};
