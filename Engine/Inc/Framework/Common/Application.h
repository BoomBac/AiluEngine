#pragma once
#ifndef __APPLICATION_H__
#define __APPLICATION_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Window.h"
#include "Framework/Events/Event.h"
#include "Render/Renderer.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/Events/LayerStack.h"

namespace Ailu
{
    class AILU_API Application : public IRuntimeModule
    {
    public:
        int Initialize() override;
        void Finalize() override;
        void Tick() override;

        void PushLayer(Layer* layer);
        void PushOverLayer(Layer* layer);
    private:
        bool OnWindowClose(WindowCloseEvent& e);
    private:
        LayerStack _layer_stack;
        Window* _p_window = nullptr;
        Renderer* _p_renderer = nullptr;
        void OnEvent(Event& e);
        bool _b_running;
    };
}

#endif // !APPLICATION_H__

