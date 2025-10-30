#pragma once
#ifndef __APPLICATION_H__
#define __APPLICATION_H__
#include "Framework/Common/Window.h"
#include "Framework/Events/Event.h"
#include "Framework/Events/LayerStack.h"
#include "Framework/Events/WindowEvent.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Framework/Interface/IRuntimeModule.h"
#include "Render/RenderPipeline.h"
#include <thread>
#include "Framework/Common/Container.hpp"
#include "Framework/Common/RawEventQueue.h"

namespace Ailu
{
    using Render::RenderPipeline;

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

    enum class ECursorType
    {
        kArrow,
        kSizeNS,  // 上下
        kSizeEW,  // 左右
        kSizeNESW,// ↘↖ 对角
        kSizeNWSE,// ↗↙ 对角
        kHand
    };

    struct ObjectLayer
    {
        String _name;
        u32    _value;
        u32    _bit_pos;
    };

    class MainThreadDispatcher
    {
    public:
        template<typename Func>
        auto Enqueue(Func &&func) -> std::future<typename std::invoke_result_t<Func>>
        {
            using R = typename std::invoke_result_t<Func>;

            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Func>(func));
            std::future<R> fut = task->get_future();

            {
                std::lock_guard<std::mutex> lock(_mutex);
                _tasks.push([task]()
                            { (*task)(); });
            }

            return fut;
        }

        void PumpTasks()
        {
            std::queue<std::function<void()>> localQueue;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                std::swap(localQueue, _tasks);
            }

            while (!localQueue.empty())
            {
                localQueue.front()();
                localQueue.pop();
            }
        }

    private:
        std::mutex _mutex;
        std::queue<std::function<void()>> _tasks;
    };

    class AILU_API Application : public IRuntimeModule
    {
        DECLARE_DELEGATE(before_update);
        DECLARE_DELEGATE(after_update);
        DECLARE_DELEGATE(on_window_resize,Window*,Vector2f);
    public:
        static Window *FocusedWindow() { return s_focus_window; };
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
        static Application& Get();
        int Initialize() override;
        int Initialize(ApplicationDesc desc);
        void Finalize() override;
        void Tick(f32 delta_time) override;

        void PushLayer(Layer *layer);
        void PushOverLayer(Layer *layer);

        void WaitForRender();
        void WaitForMain();
        void NotifyMain();
        void NotifyRender();

        void SetCursor(ECursorType type);

        [[nodiscard]] Window &GetWindow() { return *_p_window; }
        Window *GetWindowPtr() { return _p_window.get(); }
        /// @brief 返回逻辑帧
        /// @return 
        u64 GetFrameCount() const {return _frame_count;}
        const Array<ObjectLayer,32>& GetObjectLayers() const {return _object_layers;}
        const ObjectLayer& NameToLayer(const String& name);
        const EApplicationState::EApplicationState State() const {return _state;}
        LayerStack &GetLayerStack() { return *_layer_stack; }
        bool _is_playing_mode = false;
        bool _is_simulate_mode = false;
        bool _is_multi_thread_rendering = false;
        MainThreadDispatcher _dispatcher;
    protected:
        virtual bool OnWindowClose(WindowCloseEvent &e);
        virtual bool OnGetFocus(WindowFocusEvent &e);
        virtual bool OnLostFocus(WindowLostFocusEvent &e);
        virtual bool OnWindowMinimize(WindowMinimizeEvent &e);
        virtual bool OnWindowResize(WindowResizeEvent &e);
        virtual bool OnWindowMove(WindowMovedEvent &e);
        virtual bool OnDragFile(DragFileEvent &e);
        virtual bool OnMouseDown(MouseButtonPressedEvent& e);
        virtual bool OnMouseUp(MouseButtonReleasedEvent& e);
        virtual bool OnMouseMove(MouseMovedEvent& e);
        virtual bool OnMouseScroll(MouseScrollEvent& e);
        virtual bool OnKeyDown(KeyPressedEvent& e);
        virtual bool OnKeyUp(KeyReleasedEvent& e);
        virtual bool OnSetCursor(MouseSetCursorEvent &e);

        virtual void OnEvent(Event &e);

        void LogicLoop();
        void SetCursorInternal();
    protected:
        inline static Application *sp_instance = nullptr;
        inline static Window *s_focus_window = nullptr;

        LayerStack *_layer_stack;
        ImGUILayer *_p_imgui_layer;
        Scope<Window> _p_window = nullptr;
        std::atomic<bool> _is_handling_event;
        std::thread *_p_event_handle_thread;
        Scope<RenderPipeline> _pipeline;
        EApplicationState::EApplicationState _state = EApplicationState::EApplicationState_None;
        double _render_lag = 0.0;
        double _update_lag = 0.0;
        WString _engin_config_path;
        Array<ObjectLayer,32> _object_layers;
        ECursorType _cursor_type = ECursorType::kArrow;
    private:
        std::mutex _mutex;
        std::condition_variable _main_wait;
        std::condition_variable _render_wait;
        bool _render_finished = true;
        bool _main_finished = false;
        u64 _frame_count = 0u;
        Scope<Core::RawEventQueue> _raw_event_queue;
        //maybe editor
        std::atomic<bool> _has_drop_files = false;
        std::mutex _drop_files_mtx;
        Vector<WString> _drop_files;
    };
}// namespace Ailu

#endif// !APPLICATION_H__
