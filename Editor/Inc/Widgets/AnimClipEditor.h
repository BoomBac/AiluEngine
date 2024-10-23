/*
 * @author    : BoomBac
 * @created   : 2024.10
*/
#pragma once
#ifndef __ANIMCLIP_EDITOR_H__
#define __ANIMCLIP_EDITOR_H__
#include "ImGuiWidget.h"
#include <Animation/Clip.h>
namespace Ailu
{
	namespace Editor
	{
		class AnimClipEditor : public ImGuiWidget
		{
		public:
            AnimClipEditor();
            ~AnimClipEditor();
            void Open(const i32 &handle) final;
            void Close(i32 handle) final;
        private:
            void ShowImpl() final;
        private:
            AnimationClip *_clip;
		};
	}
}

#endif// !ANIMCLIP_EDITOR_H__
