#pragma once
#include "Common/EditorProgressService.h"
#include "UI/Widget.h"
#include "UI/Basic.h"
#include <array>

namespace Ailu::Editor
{
    class EditorProgressPanel
    {
    public:
        explicit EditorProgressPanel(EditorProgressService &service);
        ~EditorProgressPanel();
        void Update(f32 dt, Vector2f window_size, f32 toolbar_height, f32 status_height);
        const String &Summary() const { return _summary; }
    private:
        struct Row
        {
            u64 _id = 0;
            UI::Border *_root = nullptr;
            UI::Text *_text = nullptr;
            UI::Border *_fill = nullptr;
            UI::Button *_button = nullptr;
            bool _dismiss = false;
        };
        EditorProgressService &_service;
        Ref<UI::Widget> _widget;
        std::array<Row, 3> _rows;
        UI::Text *_overflow = nullptr;
        Vector<ProgressSnapshot> _snapshots;
        String _summary;
        f32 _animation_time = 0;
        bool _registered = false;
        Vector2f _size = Vector2f::kZero;
        Vector2f _position = Vector2f::kZero;
    };
}
