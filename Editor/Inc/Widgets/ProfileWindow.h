#ifndef __PROFILE_WINDOW_H__
#define __PROFILE_WINDOW_H__

#include "ImGuiWidget.h"
#include "Framework/Common/Profiler.h"

namespace Ailu
{
    namespace Editor
    {
        class ProfileWindow : public ImGuiWidget
        {
        public:
            ProfileWindow();
            ~ProfileWindow();
            void Open(const i32& handle) final;
            void Close(i32 handle) final;
            void OnEvent(Event& e) final;
        private:
            void ShowImpl() final;
        private:
            ProfileFrameData _cur_frame_data;
        };
    }
}
#endif