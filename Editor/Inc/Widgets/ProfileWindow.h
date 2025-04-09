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
            void ShowTimeLine(std::thread::id tid,f32& offset_x,f32& scale_factor);
        private:
            Vector<ProfileFrameData> _cached_frame_data;
            u64 _view_frame_index = 0u;
            u32 _view_data_index = 0u;
        };
    }
}
#endif