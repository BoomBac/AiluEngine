#pragma once
#ifndef __APPLICATION_H__
#define __APPLICATION_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Window.h"
#include "Framework/Events/Event.h"
#include "Render/Renderer.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/Events/LayerStack.h"
#include "Framework/ImGui/ImGuiLayer.h"

namespace Ailu
{
    class AILU_API Application : public IRuntimeModule
    {
    public:
        int Initialize() override;
        void Finalize() override;
        void Tick(const float& delta_time) override;

        void PushLayer(Layer* layer);
        void PushOverLayer(Layer* layer);

        const Window& GetWindow() const { return *_p_window; }
        Window* GetWindowPtr() { return _p_window; }

        static Application* GetInstance();
    private:
        bool OnWindowClose(WindowCloseEvent& e);
    private:
        LayerStack _layer_stack;
        ImGUILayer* _p_imgui_layer;
        Window* _p_window = nullptr;
        Renderer* _p_renderer = nullptr;
        void OnEvent(Event& e);
        bool _b_running;
        inline static Application* sp_instance = nullptr;
    };
}

#endif // !APPLICATION_H__

