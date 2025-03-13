#pragma once
#ifndef __APPLICATION_H__
#define __APPLICATION_H__
#include "Framework/Common/Window.h"
#include "Framework/Events/Event.h"
#include "Framework/Events/LayerStack.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Framework/Interface/IRuntimeModule.h"
#include "Render/RenderPipeline.h"
#include <thread>
#include "Framework/Common/Container.hpp"

namespace Ailu
{
    DECLARE_ENUM(EApplicationState,
                 EApplicationState_None,
                 EApplicationState_Running,
                 EApplicationState_Pause,
                 EApplicationState_Exit)
    struct AILU_API ApplicationDesc
    {
        u32 _window_width, _window_height;
        u32 _gameview_width, _gameview_height;
    };

    struct ObjectLayer
    {
        String _name;
        u32    _value;
        u32    _bit_pos;
    };

    class AILU_API Application : public IRuntimeModule
    {
    public:
#ifdef COMPANY_ENV
        static constexpr double kTargetFrameRate = 60;
#else
        static constexpr double kTargetFrameRate = 60;
#endif// COMPLY
        static constexpr double kMsPerUpdate = 16.66;
        static constexpr double kMsPerRender = 1000.0 / kTargetFrameRate;
        inline static double s_target_framecount;
        inline static double s_target_lag = kMsPerRender;
        /// @brief 返回当前exe所在目录
        /// @return 目录 c:/xxx/xxx/
        static WString GetWorkingPath();
        /// @brief 返回当前应用程序缓存目录
        /// @return 目录 working_path/cache/
        static WString GetAppCachePath();
        /// @brief 获取用户目录，c:/UserName/
        /// @return 用户目录
        static WString GetUseHomePath();
        static Application *Get();
        int Initialize() override;
        int Initialize(ApplicationDesc desc);
        void Finalize() override;
        void Tick(f32 delta_time) override;

        void PushLayer(Layer *layer);
        void PushOverLayer(Layer *layer);

        [[nodiscard]] const Window &GetWindow() const { return *_p_window; }
        Window *GetWindowPtr() { return _p_window; }

        inline static u64 s_frame_count = 0u;
        bool _is_playing_mode = false;
        bool _is_simulate_mode = false;

        const Array<ObjectLayer,32>& GetObjectLayers() const {return _object_layers;}
        const ObjectLayer& NameToLayer(const String& name);

    protected:
        virtual bool OnWindowClose(WindowCloseEvent &e);
        virtual bool OnGetFocus(WindowFocusEvent &e);
        virtual bool OnLostFocus(WindowLostFocusEvent &e);
        virtual bool OnWindowMinimize(WindowMinimizeEvent &e);
        virtual bool OnWindowResize(WindowResizeEvent &e);
        virtual bool OnWindowMove(WindowMovedEvent &e);
        virtual bool OnDragFile(DragFileEvent &e);
        virtual void OnEvent(Event &e);

    protected:
        inline static Application *sp_instance = nullptr;
        LayerStack *_layer_stack;
        ImGUILayer *_p_imgui_layer;
        Window *_p_window = nullptr;
        std::atomic<bool> _is_handling_event;
        std::thread *_p_event_handle_thread;
        Scope<RenderPipeline> _pipeline;
        EApplicationState::EApplicationState _state = EApplicationState::EApplicationState_None;
        double _render_lag = 0.0;
        double _update_lag = 0.0;
        WString _engin_config_path;
        Array<ObjectLayer,32> _object_layers;
    };
}// namespace Ailu

#endif// !APPLICATION_H__
