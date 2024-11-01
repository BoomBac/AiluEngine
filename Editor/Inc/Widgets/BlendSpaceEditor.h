#pragma once
#ifndef __BLENDER_SPACE_EDITOR_H__
#define __BLENDER_SPACE_EDITOR_H__
#include <UI/Canvas.h>
#include "ImGuiWidget.h"
#include "Animation/BlendSpace.h"

namespace Ailu
{
    namespace Editor
    {
        class BlendSpaceEditor : public ImGuiWidget
        {
        public:
            BlendSpaceEditor();
            ~BlendSpaceEditor();
            void Open(const i32 &handle) final;
            void Close(i32 handle) final;
            void SetTarget(BlendSpace *target);
            void OnEvent(Event &event) final;
        private:
            void ShowImpl() final;
            BlendSpace *_target = nullptr;
            AnimationClip* _selected_clip = nullptr;
            UI::Canvas _canvas;
        };
    }// namespace Editor
}// namespace Ailu


#endif// !ASSET_TABLE_H__
