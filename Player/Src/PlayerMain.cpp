#include "PlayerApp.h"

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hInstPrev, _In_ PSTR cmdline, _In_ int cmdshow)
{
    Ailu::PlayerApp app;
    if (app.Initialize() != 0)
        return 1;
    app.Tick(16.6f);
    app.Finalize();
    return 0;
}