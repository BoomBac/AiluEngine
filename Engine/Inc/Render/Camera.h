#pragma warning(disable : 4275)//dll interface warning
#ifndef __CAMERA_H__
#define __CAMERA_H__
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Geometry.h"
#include "Framework/Core/CoreMinimal.h"
#include "Objects/Object.h"
#include "Objects/Serialize.h"
#include "Render/RenderingData.h"
#include "generated/Camera.gen.h"

namespace Ailu
{
    namespace Render
    {
        AENUM()
        enum class EAntiAliasing
        {
            kNone,
            kFXAA,
            kTAA
        };
        class Renderer;
        AENUM()
        enum class ECameraType
        {
            kOrthographic,
            kPerspective
        };
        AENUM()
        enum class ELensProjectionMode : u8
        {
            kLegacyFov,
            kPhysicalFocalLength
        };
        AENUM()
        enum class EExposureMode : u8
        {
            kLegacyManual,
            kManualPhysical,
            kAuto,
            kAperturePriority,
            kShutterPriority,
            kAutoISO
        };

        ASTRUCT()
        struct AILU_API PhysicalCameraSettings
        {
            GENERATED_BODY()

            APROPERTY()
            f32 _sensor_width_mm = 36.0f;
            APROPERTY()
            f32 _sensor_height_mm = 24.0f;
            APROPERTY()
            f32 _focal_length_mm = 50.0f;
            APROPERTY()
            f32 _aperture_f_number = 4.0f;
            APROPERTY()
            f32 _shutter_seconds = 1.0f / 60.0f;
            APROPERTY()
            f32 _iso = 100.0f;
            APROPERTY()
            f32 _focus_distance_m = 10.0f;
            APROPERTY()
            f32 _exposure_compensation_ev = 0.0f;
            APROPERTY()
            ELensProjectionMode _lens_projection_mode = ELensProjectionMode::kLegacyFov;
            APROPERTY()
            EExposureMode _exposure_mode = EExposureMode::kLegacyManual;

            void Serialize(FArchive &archive);
            void Deserialize(FArchive &archive);
            void Sanitize();
        };

        AENUM()
        enum class EUpsamplingFilter : u8
        {
            kBilinear,
            kPoint
        };
        enum class ESuperResolutionMode : u8
        {
            kOff,
            kDlss,
            kFsr
        };
        enum class ESuperResolutionQuality : u8
        {
            kQuality,
            kBalanced,
            kPerformance,
            kUltraPerformance
        };
        enum class EResolvedTemporalMode : u8
        {
            kSpatial,
            kTAA,
            kDlssSR,
            kFsrSR
        };
        const String &CameraTypeToString(ECameraType type);
        ECameraType CameraTypeFromString(const String &str);

        class AILU_API Camera : public Object
        {
            friend struct CCamera;
            public:
                void Type(const ECameraType &value)
                {
                    if (_camera_type == value)
                        return;
                    _camera_type = value;
                    if (_camera_type == ECameraType::kPerspective &&
                        _physical_camera_settings._lens_projection_mode == ELensProjectionMode::kPhysicalFocalLength)
                        PhysicalSettings(_physical_camera_settings);
                    MarkDirty();
                    MarkTemporalHistoryReset();
                }
                const ECameraType &Type() const { return _camera_type; }

            protected:
                ECameraType _camera_type;
            public:
                void Aspect(const float &value) { _aspect = value; }
                const float &Aspect() const { return _aspect; }

            protected:
                float _aspect;
            public:
                void Near(const float &value) { _near_clip = value; }
                const float &Near() const { return _near_clip; }

            protected:
                float _near_clip;
            public:
                void Far(const float &value) { _far_clip = value; }
                const float &Far() const { return _far_clip; }

            protected:
                float _far_clip;
            public:
                void Position(const Vector3f &value) { _position = value; }
                const Vector3f &Position() const { return _position; }

            protected:
                Vector3f _position;
            public:
                void Rotation(const Quaternion &value) { _rotation = value; }
                const Quaternion &Rotation() const { return _rotation; }

            protected:
                Quaternion _rotation;
            public:
                void Forward(const Vector3f &value) { _forward = value; }
                const Vector3f &Forward() const { return _forward; }

            protected:
                Vector3f _forward;
            public:
                void Right(const Vector3f &value) { _right = value; }
                const Vector3f &Right() const { return _right; }

            protected:
                Vector3f _right;
            public:
                void Up(const Vector3f &value) { _up = value; }
                const Vector3f &Up() const { return _up; }

            protected:
                Vector3f _up;
            public:
                void FovH(const float &value) { SetVerticalFov(value); }
                const float &FovH() const { return _fov_h; }
                void VerticalFov(f32 value) { SetVerticalFov(value); }
                f32 VerticalFov() const { return _fov_h; }

                const PhysicalCameraSettings &PhysicalSettings() const { return _physical_camera_settings; }
                void PhysicalSettings(const PhysicalCameraSettings &settings);
                void LensProjectionMode(ELensProjectionMode value);
                void ExposureMode(EExposureMode value);
                void SetFocalLengthMm(f32 value);
                f32 ComputeEv100() const;
                f32 ComputeExposureMultiplier() const;
                static f32 ComputeVerticalFov(f32 sensor_height_mm, f32 focal_length_mm);
                static f32 ComputeFocalLength(f32 sensor_height_mm, f32 vertical_fov_degrees);

            protected:
                float _fov_h;
                PhysicalCameraSettings _physical_camera_settings;
            //Orthographic only
            public:
                void Size(const float &value) { _size = value; }
                const float &Size() const { return _size; }

            protected:
                float _size = 10.0f;
        public:
            inline static Camera *sCurrent = nullptr;
            inline static Camera *sMain = nullptr;
            inline static Camera *sScene = nullptr;
            inline static Camera *sSelected = nullptr;
            static Camera *GetDefaultCamera();
            static void DrawGizmo(const Camera *p_camera, Color c);
            static Matrix4x4f GetDefaultOrthogonalViewProj(f32 w, f32 h, f32 near  = 1.f, f32 far  = 200.f);
            //when the eye_camera turns, the AABB is recalculated, resulting in shadow jitter
            static AABB GetBoundingAABB(const Camera &eye_cam, f32 start = 0.0f, f32 end = -1.0f);
            static Camera &GetCubemapGenCamera(const Camera &base_cam, ECubemapFace face);

        public:
            static void CalculateZBUfferAndProjParams(const Camera &cam, Vector4f &zb, Vector4f &proj_params);
            Camera();
            Camera(float aspect, float near_clip = 10.0f, float far_clip = 10000.0f, ECameraType camera_type = ECameraType::kPerspective);
            void RecalculateMatrix(bool force = false);
            // Advance history once per rendered frame after render synchronization, then refresh projection jitter.
            void PrepareRenderFrame(u64 frame_index, f32 manual_texture_mip_bias = 0.0f);
            // Align previous-frame temporal state to the current camera after a cut or teleport.
            void MarkTemporalHistoryReset()
            {
                _temporal_history_reset_pending = true;
                ++_temporal_history_reset_revision;
            }
            u64 GetTemporalHistoryResetRevision() const { return _temporal_history_reset_revision; }
            void SetLens(float fovh, float aspect, float nz, float fz);
            void SetLocalViewAxes(const Vector3f& forward, const Vector3f& up)
            {
                _local_forward = Normalize(forward);
                _local_up = Normalize(up);
                MarkDirty();
            }
            const Vector3f& LocalForward() const { return _local_forward; }
            const Vector3f& LocalUp() const { return _local_up; }
            void ProjectionOffset(const Vector2f& value) { _projection_offset = value; MarkDirty(); }
            const Vector2f& ProjectionOffset() const { return _projection_offset; }
            void ReverseProjectionX(bool value) { _reverse_projection_x = value; MarkDirty(); }
            bool ReverseProjectionX() const { return _reverse_projection_x; }
            void LockAspect(bool value) { _lock_aspect = value; }
            bool LockAspect() const { return _lock_aspect; }
            void RenderScale(f32 value) { _render_scale = std::clamp(value, 0.1f, 2.0f); }
            f32 RenderScale() const { return _render_scale; }
            void SuperResolutionMode(ESuperResolutionMode value)
            {
                if (_super_resolution_mode == value)
                    return;
                _super_resolution_mode = value;
                if (value == ESuperResolutionMode::kDlss &&
                    _super_resolution_quality == ESuperResolutionQuality::kUltraPerformance)
                    _super_resolution_quality = ESuperResolutionQuality::kPerformance;
                MarkTemporalHistoryReset();
            }
            ESuperResolutionMode SuperResolutionMode() const { return _super_resolution_mode; }
            void SuperResolutionQuality(ESuperResolutionQuality value)
            {
                if (_super_resolution_mode == ESuperResolutionMode::kDlss &&
                    value == ESuperResolutionQuality::kUltraPerformance)
                    value = ESuperResolutionQuality::kPerformance;
                if (_super_resolution_quality == value)
                    return;
                _super_resolution_quality = value;
                MarkTemporalHistoryReset();
            }
            ESuperResolutionQuality SuperResolutionQuality() const { return _super_resolution_quality; }
            void SetAutoTextureMipBiasEnabled(bool enabled)
            {
                if (_auto_texture_mip_bias == enabled) return;
                _auto_texture_mip_bias = enabled;
                MarkTemporalHistoryReset();
            }
            bool IsAutoTextureMipBiasEnabled() const { return _auto_texture_mip_bias; }
            f32 GetAutoTextureMipBias() const;
            bool IsDlssAvailable() const { return _dlss_available; }
            bool IsFsrAvailable() const { return _fsr_available; }
            EResolvedTemporalMode ResolvedTemporalMode() const { return _resolved_temporal_mode; }
            bool IsDlssSRActive() const { return _resolved_temporal_mode == EResolvedTemporalMode::kDlssSR; }
            bool IsFsrSRActive() const { return _resolved_temporal_mode == EResolvedTemporalMode::kFsrSR; }
            bool IsTemporalUpscalingActive() const { return IsDlssSRActive() || IsFsrSRActive(); }
            void ResolveTemporalMode(EResolvedTemporalMode mode, Vector2D<u32> render_size, bool dlss_available,
                                     bool fsr_available = false)
            {
                if (_resolved_temporal_mode != mode || _resolved_render_size.x != render_size.x ||
                    _resolved_render_size.y != render_size.y || _dlss_available != dlss_available ||
                    _fsr_available != fsr_available)
                    MarkTemporalHistoryReset();
                _resolved_temporal_mode = mode;
                _resolved_render_size = render_size;
                _dlss_available = dlss_available;
                _fsr_available = fsr_available;
            }
            void UpsamplingFilter(EUpsamplingFilter value) { _upsampling_filter = value; }
            EUpsamplingFilter UpsamplingFilter() const { return _upsampling_filter; }
            Vector2D<u32> ConfiguredRenderSize() const
            {
                return Vector2D<u32>{std::max<u32>(1u, static_cast<u32>(std::round(_pixel_width * _render_scale))),
                                     std::max<u32>(1u, static_cast<u32>(std::round(_pixel_height * _render_scale)))};
            }
            Vector2D<u32> RenderSize() const
            {
                return IsTemporalUpscalingActive() ? _resolved_render_size : ConfiguredRenderSize();
            }

            void OutputSize(u16 w, u16 h)
            {
                _pixel_width = w;
                _pixel_height = h;
                if (!_lock_aspect && h != 0u) _aspect = (f32) _pixel_width / (f32) _pixel_height;
            };
            Vector2D<u32> OutputSize() const { return Vector2D<u32>{_pixel_width, _pixel_height}; }
            //包括抖动的透视矩阵
            const Matrix4x4f &GetProj() const { return _proj_matrix; }

            const Matrix4x4f &GetProjNoJitter() const { return _proj_matrix_no_jitter; }
            Vector3f ScreenToWorld(const Vector2f &screen_pos, f32 depth = 0.0f) const;
            Vector2f WorldToScreen(const Vector3f& world_pos) const;

            const Matrix4x4f &GetView() const { return _view_matrix; }
            /// @brief 返回当前帧稳定的 no-jitter VP 矩阵；保留旧调用点语义。
            const Matrix4x4f &GetViewProj() const { return _no_jitter_view_proj_matrix; }
            const Matrix4x4f &GetViewProjNoJitter() const { return _no_jitter_view_proj_matrix; }
            const Matrix4x4f &GetViewProjJittered() const { return _jittered_view_proj_matrix; }
            /// @brief 返回上一帧的透视投影矩阵（不包括抖动）
            const Matrix4x4f &GetPrevViewProj() const { return _prev_view_proj_matrix; }
            /// @brief 返回当前帧抖动
            /// @return xy: jitter(-1~+1) matrix,zw: jitter(-0.5~0.5) uv
            const Vector4f &GetJitter() const { return _jitter; }
            void LookTo(const Vector3f &direction, const Vector3f &up);
            bool IsInFrustum(const Vector3f &pos);
            bool IsInFrustum(const AABB &bounds) const;
            void MarkDirty() { _is_dirty = true; }
            const ViewFrustum &GetViewFrustum() const { return _vf; }
            void SetRenderer(Renderer *r);
            RenderTexture *TargetTexture() const { return _target_texture; }
            void TargetTexture(RenderTexture *output) { _target_texture = output; }
            void SetViewAndProj(const Matrix4x4f &view, const Matrix4x4f &proj);
            void SetPixelSize(u16 w, u16 h);
            /// @brief 获取视椎体世界坐标系的四个边指向远平面方向（归一化）
            /// @param lt 左上
            /// @param lb 左下
            /// @param rt 右上
            /// @param rb 右下
            void GetCornerInWorld(Vector3f &lt, Vector3f &lb, Vector3f &rt, Vector3f &rb) const;
            //temp
            bool IsCustomVP() const { return _is_custom_vp; }
            friend Archive &operator<<(Archive &ar, const Camera &c);
            friend Archive &operator>>(Archive &ar, Camera &c);
            Vector4f GetZBufferParams() const { return _zbuffer_params; }
            Vector4f GetProjParams() const { return _proj_params; }
            void EnableTemporalDebugFrames(u32 frame_count = 8u) const
            {
                _temporal_debug_frames_remaining = frame_count;
            }
            bool ConsumeTemporalDebugFrame() const
            {
                if (_temporal_debug_frames_remaining == 0u)
                    return false;
                --_temporal_debug_frames_remaining;
                return true;
            }

        public:
            u32 _layer_mask = (u32) -1;
            bool _is_scene_camera = false;
            bool _is_render_sky_box = true;
            bool _postprocess_enabled = true;
            bool _is_gen_voxel = false;
            bool _is_enable = false;
            EAntiAliasing _anti_aliasing = EAntiAliasing::kNone;
            f32 _jitter_scale = 1.0f;

        private:
            Vector3f _local_forward = Vector3f::kForward;
            Vector3f _local_up = Vector3f::kUp;
            Vector2f _projection_offset = Vector2f::kZero;
            bool _reverse_projection_x = false;
            bool _lock_aspect = false;
            f32 _render_scale = 1.0f;
            f32 _last_texture_mip_bias = 0.0f;
            bool _has_texture_mip_bias = false;
            bool _auto_texture_mip_bias = true;
            ESuperResolutionMode _super_resolution_mode = ESuperResolutionMode::kOff;
            ESuperResolutionQuality _super_resolution_quality = ESuperResolutionQuality::kQuality;
            EResolvedTemporalMode _resolved_temporal_mode = EResolvedTemporalMode::kSpatial;
            Vector2D<u32> _resolved_render_size = {1u, 1u};
            bool _dlss_available = false;
            bool _fsr_available = false;
            EUpsamplingFilter _upsampling_filter = EUpsamplingFilter::kBilinear;
            void RebuildCameraMatrices();
            void RefreshRenderMatrices();
            void CalculateCullingFrustum();
            void CalculateRenderRayCorners();
            void ProcessJitter();
            void SetVerticalFov(f32 value);

        private:
            bool _is_custom_vp = false;
            bool _has_previous_vp_update_frame = false;
            bool _temporal_history_reset_pending = false;
            u64 _temporal_history_reset_revision = 0u;
            ViewFrustum _vf;
            bool _is_dirty = true;
            Matrix4x4f _view_matrix{};
            Matrix4x4f _proj_matrix{};
            Matrix4x4f _proj_matrix_no_jitter{};
            Matrix4x4f _no_jitter_view_proj_matrix{};
            Matrix4x4f _jittered_view_proj_matrix{};
            Matrix4x4f _prev_view_proj_matrix{};
            Matrix4x4f _last_rendered_view_proj_matrix{};
            Vector4f _zbuffer_params;
            Vector4f _proj_params;
            Vector3f _near_top_left;
            Vector3f _near_top_right;
            Vector3f _near_bottom_left;
            Vector3f _near_bottom_right;
            Vector3f _far_top_left;
            Vector3f _far_top_right;
            Vector3f _far_bottom_left;
            Vector3f _far_bottom_right;
            Vector3f _render_far_top_left;
            Vector3f _render_far_top_right;
            Vector3f _render_far_bottom_left;
            Vector3f _render_far_bottom_right;
            Vector4f _jitter;
            u16 _pixel_width = 800u, _pixel_height = 450u;
            Renderer *_renderer = nullptr;
            RenderTexture *_target_texture = nullptr;
            u64 _prev_vp_matrix_update_frame = 0u;
            mutable u32 _temporal_debug_frames_remaining = 0u;
        };

        Archive &operator<<(Archive &ar, const Camera &c);
        Archive &operator>>(Archive &ar, Camera &c);
    }
}// namespace Ailu

#endif// !__CAMERA_H__
