#pragma once
#ifndef __BLEND_SPACE_ASSET_EDITOR_H__
#define __BLEND_SPACE_ASSET_EDITOR_H__

#include "Animation/BlendSpace.h"
#include "Animation/AnimationClipPreview.h"
#include "Editors/AssetEditor.h"
#include "UI/UIElement.h"

#include <functional>

namespace Ailu
{
    namespace Render
    {
        class SkeletonMesh;
    }

    namespace UI
    {
        class Button;
        class CheckBox;
        class HorizontalBox;
        class Image;
        class InputBlock;
        class ObjectAssetDropdown;
        class Slider;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        class BlendSpaceGraph final : public UI::UIElement
        {
        public:
            using SampleSelectedCallback = std::function<void(i32)>;
            using SampleMovedCallback = std::function<void(i32, Vector2f)>;
            using SampleMoveFinishedCallback = std::function<void(i32)>;
            using EmptyDoubleClickCallback = std::function<void(Vector2f)>;
            using PreviewInputChangedCallback = std::function<void(Vector2f)>;

            BlendSpaceGraph();

            Vector2f MeasureDesiredSize() override;
            void SetAsset(BlendSpaceAsset *asset);
            void SetSelectedSample(i32 sample_index);
            void SetPreviewInput(Vector2f input);
            Vector2f GetDefaultSamplePosition() const;

            void SetSampleSelectedCallback(SampleSelectedCallback callback)
            {
                _on_sample_selected = std::move(callback);
            }
            void SetSampleMovedCallback(SampleMovedCallback callback) { _on_sample_moved = std::move(callback); }
            void SetSampleMoveFinishedCallback(SampleMoveFinishedCallback callback)
            {
                _on_sample_move_finished = std::move(callback);
            }
            void SetPreviewInputChangedCallback(PreviewInputChangedCallback callback)
            {
                _on_preview_input_changed = std::move(callback);
            }
            void SetEmptyDoubleClickCallback(EmptyDoubleClickCallback callback)
            {
                _on_empty_double_click = std::move(callback);
            }

        protected:
            void RenderImpl(UI::UIRenderer &renderer) override;

        private:
            Vector4f GetPlotRect() const;
            Vector2f ValueToScreen(Vector2f value) const;
            Vector2f ScreenToValue(Vector2f screen_position) const;
            bool IsSnapEnabled() const;
            Vector2f SnapToNearestSample(Vector2f position, i32 ignored_sample = -1) const;
            bool HitPreviewInput(Vector2f screen_position) const;
            i32 HitSample(Vector2f screen_position) const;
            void HandleMouseDown(UI::UIEvent &event);
            void HandleMouseUp(UI::UIEvent &event);
            void HandleMouseMove(UI::UIEvent &event);
            void HandleMouseDoubleClick(UI::UIEvent &event);

            BlendSpaceAsset *_asset = nullptr;
            i32 _selected_sample = -1;
            i32 _dragged_sample = -1;
            bool _dragging_preview_input = false;
            Vector2f _preview_input = Vector2f::kZero;
            SampleSelectedCallback _on_sample_selected;
            SampleMovedCallback _on_sample_moved;
            SampleMoveFinishedCallback _on_sample_move_finished;
            EmptyDoubleClickCallback _on_empty_double_click;
            PreviewInputChangedCallback _on_preview_input_changed;
        };

        class BlendSpaceAssetEditor final : public AssetEditor
        {
        public:
            BlendSpaceAssetEditor();
            ~BlendSpaceAssetEditor() override = default;

            void Update(f32 dt) override;

        private:
            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label);
            static UI::InputBlock *AddTextInput(UI::UIElement *parent, const String &label, const String &value,
                                                const std::function<void(String)> &on_changed);
            static UI::InputBlock *AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                 const std::function<void(f32)> &on_changed);

            void AddSampleAt(Vector2f position);
            void RemoveSelectedSample();
            void SelectSample(i32 sample_index);
            void MoveSample(i32 sample_index, Vector2f position);
            void FinishSampleMove(i32 sample_index);
            void UpdateRange(bool is_x_axis, bool is_minimum, f32 value);
            void SetPreviewInput(Vector2f input, bool refresh_inputs = true);
            void SetPreviewTime(f32 time, bool refresh_slider = true);
            f32 GetPreviewDuration() const;
            void RefreshPreviewControls();
            void TogglePreviewPlayback();
            void StepPreview(f32 direction);
            void MarkDirty();
            void RefreshAllUI();
            void RefreshPreview();
            void RefreshSamples();
            void RefreshSampleDetails();
            void SortSamples();
            String GetSampleName(const BlendSpaceSample &sample, u32 index) const;

            bool OnOpen() override;
            void OnClose() override;
            void OnAssetReloaded() override;
            void OnBeforeSave() override;
            void OnAssetSaved() override;

            BlendSpaceAsset *_blend_space = nullptr;
            i32 _selected_sample = -1;
            bool _is_refreshing_ui = false;

            UI::InputBlock *_input_name = nullptr;
            UI::InputBlock *_input_x_min = nullptr;
            UI::InputBlock *_input_x_max = nullptr;
            UI::InputBlock *_input_y_min = nullptr;
            UI::InputBlock *_input_y_max = nullptr;
            UI::CheckBox *_check_2d = nullptr;
            UI::Text *_txt_status = nullptr;
            UI::Text *_txt_preview = nullptr;
            UI::Text *_txt_preview_time = nullptr;
            UI::InputBlock *_input_preview_x = nullptr;
            UI::InputBlock *_input_preview_y = nullptr;
            UI::Slider *_preview_time_slider = nullptr;
            UI::Button *_preview_play_button = nullptr;
            UI::Button *_preview_loop_button = nullptr;
            UI::VerticalBox *_samples_root = nullptr;
            UI::VerticalBox *_sample_details_root = nullptr;
            UI::ObjectAssetDropdown *_sample_clip_dropdown = nullptr;
            UI::InputBlock *_input_sample_x = nullptr;
            UI::InputBlock *_input_sample_y = nullptr;
            BlendSpaceGraph *_graph = nullptr;
            Vector2f _preview_input = Vector2f::kZero;
            Scope<AnimationClipPreview> _animation_preview;
            UI::Image *_preview_image = nullptr;
            Ref<AnimationClip> _preview_clip;
            Ref<Render::SkeletonMesh> _preview_mesh;
            Guid _preview_clip_guid = Guid::EmptyGuid();
            Guid _preview_mesh_guid = Guid::EmptyGuid();
            f32 _preview_time = 0.0f;
            f32 _preview_duration = 1.0f;
            bool _is_preview_playing = false;
            bool _preview_loop = true;
            bool _preview_input_dirty = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __BLEND_SPACE_ASSET_EDITOR_H__
