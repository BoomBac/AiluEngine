#ifndef __MISC_PASSES_H__
#define __MISC_PASSES_H__
#pragma once
#include "RenderFeature.h"
#include "generated/MiscPasses.gen.h"

namespace Ailu
{
    namespace Render
    {
        ACLASS()
        class AILU_API VolumeTexturePreviewPass : public RenderPass
        {
            GENERATED_BODY()
        public:
            using OnTargetReadyFunc = std::function<void(RenderTexture* rt)>;
            VolumeTexturePreviewPass();
            ~VolumeTexturePreviewPass();
            void Execute(GraphicsContext *context, RenderingData &rendering_data) final;
            void BeginPass(GraphicsContext *context) final;
        private:
            void EnsureTarget();
            void OnPropertyChanged(const PropertyInfo& prop) final;
        public:
            Vector3Int _slice = Vector3Int::kZero;
            // Slice position in normalized texture space [0,1]
            APROPERTY()
            f32 _slice_x = 0.5f;
            APROPERTY()
            f32 _slice_y = 0.5f;
            APROPERTY()
            f32 _slice_z = 0.5f;
            Texture3D *_src_tex = nullptr;
            Vector2Int _view_size = Vector2Int{400, 400};
            APROPERTY()
            Vector3f _camera_pos = Vector3f(0.0f,2.0f,-2.0f);
            Ref<RenderTexture> _color_buffer = nullptr;
            Ref<RenderTexture> _depth_buffer = nullptr;
            Ref<Material> _slice_mat;
            CBufferPerCameraData _per_camera_cbuf;
            APROPERTY()
            bool _is_slice_mode = true;
            OnTargetReadyFunc _on_target_ready = nullptr;
        private:
            const Color kBackgroundColor = Colors::kDarkGray;
        };
    } // namespace Render
    
} // namespace Ailu

#endif// __MISC_PASSES_H__