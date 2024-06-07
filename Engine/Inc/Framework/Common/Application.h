#pragma once
#ifndef __APPLICATION_H__
#define __APPLICATION_H__
#include <thread>
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/Window.h"
#include "Framework/Events/Event.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/Events/LayerStack.h"
#include "Framework/ImGui/ImGuiLayer.h"

namespace Ailu
{
    DECLARE_ENUM(EApplicationState,
        EApplicationState_None,
        EApplicationState_Running,
        EApplicationState_Pause,
        EApplicationState_Exit
    )
    struct AILU_API ApplicationDesc
    {
        u32 _window_width,_window_height;
        u32 _gameview_width, _gameview_height;
    };

    class AILU_API Application : public IRuntimeModule
    {
    public:
#ifdef COMPANY_ENV
        static constexpr double kTargetFrameRate = 60;
#else
        static constexpr double kTargetFrameRate = 170;
#endif // COMPLY
        static constexpr double kMsPerUpdate = 16.66;
        static constexpr double kMsPerRender = 1000.0 / kTargetFrameRate;
        inline static double s_target_framecount = kTargetFrameRate; 
        inline static double s_target_lag = kMsPerRender;
        //**\\**\\**\\*.exe
        static WString GetWorkingPath();
        int Initialize() override;
        int Initialize(ApplicationDesc desc);
        void Finalize() override;
        void Tick(const float& delta_time) override;

        void PushLayer(Layer* layer);
        void PushOverLayer(Layer* layer);

        const Window& GetWindow() const { return *_p_window; }
        Window* GetWindowPtr() { return _p_window; }

        static Application* GetInstance();
        inline static u64 s_frame_count = 0u;
    protected:
        virtual bool OnWindowClose(WindowCloseEvent& e);
        virtual bool OnGetFoucus(WindowFocusEvent& e);
        virtual bool OnLostFoucus(WindowLostFocusEvent& e);
        virtual bool OnWindowMinimize(WindowMinimizeEvent& e);
        virtual bool OnWindowResize(WindowResizeEvent& e);
        virtual bool OnWindowMove(WindowMovedEvent& e);
        virtual bool OnDragFile(DragFileEvent& e);
        virtual void OnEvent(Event& e);
    protected:
        LayerStack* _layer_stack;
        ImGUILayer* _p_imgui_layer;
        Window* _p_window = nullptr;
        std::atomic<bool> _is_handling_event;
        std::thread* _p_event_handle_thread;
        EApplicationState::EApplicationState _state = EApplicationState::EApplicationState_None; 
        inline static Application* sp_instance = nullptr;
        double _render_lag = 0.0;
        double _update_lag = 0.0;
    };
}

#endif // !APPLICATION_H__

