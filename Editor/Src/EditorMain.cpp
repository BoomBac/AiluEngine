#include "Ailu.h"

#include "Widgets/WorldOutline.h"

class EditorApp : public Ailu::Application
{
public:
	//int Initialize() override
	//{
	//	Ailu::Application::Initialize();
	//	_p_worldoutile = new WorldOutline();
	//	PushLayer(_p_worldoutile);
	//	return 0;
	//};
	//void Tick(const float& delta_time) override
	//{
	//	
	//};
private:
	WorldOutline* _p_worldoutile;
};

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hInstPrev, _In_ PSTR cmdline, _In_ int cmdshow)
{
	EditorApp* app = new EditorApp();
	//Ailu::Application* app = new Ailu::Application();
	app->Initialize();
	app->Tick(16.6f);
	app->Finalize();
	DESTORY_PTR(app)
}