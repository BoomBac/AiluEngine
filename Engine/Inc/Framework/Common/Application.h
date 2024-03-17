#pragma once
#ifndef __APPLICATION_H__
#define __APPLICATION_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Window.h"
#include "Framework/Events/Event.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/Events/LayerStack.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Framework/Events/InputLayer.h"
#include "Framework/Events/SceneLayer.h"

namespace Ailu
{
    DECLARE_ENUM(EApplicationState,
        EApplicationState_None,
        EApplicationState_Running,
        EApplicationState_Pause,
        EApplicationState_Exit
    )
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
        bool OnGetFoucus(WindowFocusEvent& e);
        bool OnLostFoucus(WindowLostFocusEvent& e);
        bool OnWindowMinimize(WindowMinimizeEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        bool OnDragFile(DragFileEvent& e);
    private:
        LayerStack* _layer_stack;
        ImGUILayer* _p_imgui_layer;
        InputLayer* _p_input_layer;
        SceneLayer* _p_scene_layer;
        Window* _p_window = nullptr;
        EApplicationState::EApplicationState _state = EApplicationState::EApplicationState_None; 
        void OnEvent(Event& e);
        inline static Application* sp_instance = nullptr;
    };
}

#endif // !APPLICATION_H__

