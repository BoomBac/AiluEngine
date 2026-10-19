#include "Render/Camera.h"
#include "Render/RenderingData.h"
#include "Assets/AssetDocument.h"
#include "Objects/JsonArchive.h"
#include "Objects/SerializeSpecializations.h"
#include "Render/PostprocessSettings.h"
#include "Scene/LightUnits.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

using namespace Ailu;
using namespace Ailu::Render;

namespace
{
    bool Equal(const Matrix4x4f &left, const Matrix4x4f &right)
    {
        for (u32 row = 0; row < 4u; ++row)
            for (u32 column = 0; column < 4u; ++column)
                if (left[row][column] != right[row][column])
                    return false;
        return true;
    }

    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    f32 ComputeMeterExposure(f32 luminance_cd_m2, f32 compensation_ev)
    {
        constexpr f32 kMeterCalibration = 1.44f;
        constexpr f32 kReflectedMeterConstant = 12.5f;
        const f32 meter_ev = std::log2(luminance_cd_m2 * 100.0f / kReflectedMeterConstant);
        return kMeterCalibration * std::exp2(-meter_ev + compensation_ev);
    }

    void CheckPhysicalCameraOracles()
    {
        constexpr f32 kTolerance = 0.0001f;
        Camera exposure_camera(16.0f / 9.0f, 0.1f, 100.0f, ECameraType::kPerspective);
        PhysicalCameraSettings exposure_settings = exposure_camera.PhysicalSettings();
        exposure_settings._exposure_mode = EExposureMode::kManualPhysical;
        exposure_settings._aperture_f_number = 4.0f;
        exposure_settings._shutter_seconds = 1.0f / 60.0f;
        exposure_settings._iso = 100.0f;
        exposure_camera.PhysicalSettings(exposure_settings);
        const f32 base_ev = exposure_camera.ComputeEv100();
        const f32 base_exposure = exposure_camera.ComputeExposureMultiplier();
        Require(std::abs(base_ev - std::log2(960.0f)) < kTolerance,
                "f/4, 1/60 s, ISO 100 must have EV100 log2(960)");

        exposure_settings._shutter_seconds = 1.0f / 30.0f;
        exposure_camera.PhysicalSettings(exposure_settings);
        Require(std::abs(exposure_camera.ComputeExposureMultiplier() / base_exposure - 2.0f) < kTolerance,
                "doubling shutter time must double physical exposure");
        exposure_settings._aperture_f_number = 4.0f * std::sqrt(2.0f);
        exposure_camera.PhysicalSettings(exposure_settings);
        Require(std::abs(exposure_camera.ComputeEv100() - base_ev) < kTolerance,
                "f/5.656854 at 1/30 s must match f/4 at 1/60 s");
        exposure_settings._aperture_f_number = 4.0f;
        exposure_settings._shutter_seconds = 1.0f / 60.0f;
        exposure_settings._iso = 200.0f;
        exposure_camera.PhysicalSettings(exposure_settings);
        Require(std::abs(exposure_camera.ComputeExposureMultiplier() / base_exposure - 2.0f) < kTolerance,
                "doubling ISO must double physical exposure");
        exposure_settings._iso = 100.0f;
        exposure_settings._exposure_compensation_ev = 1.0f;
        exposure_camera.PhysicalSettings(exposure_settings);
        Require(std::abs(exposure_camera.ComputeExposureMultiplier() / base_exposure - 2.0f) < kTolerance,
                "positive one-stop compensation must double physical exposure");

        const f32 metered_exposure = ComputeMeterExposure(1.0f, 0.0f);
        Require(std::abs(metered_exposure - 0.18f) < kTolerance,
                "K=12.5 calibration must map 1 cd/m2 to 0.18 gray");
        Require(std::abs(ComputeMeterExposure(1.0f, 1.0f) / metered_exposure - 2.0f) < kTolerance,
                "positive one-stop compensation must double metered exposure");

        constexpr f32 kFovDegrees = 26.991467f;
        constexpr f32 kAspectRatios[] = {16.0f / 9.0f, 1.0f, 9.0f / 16.0f};
        for (const f32 aspect : kAspectRatios)
        {
            Camera camera(aspect, 0.1f, 100.0f, ECameraType::kPerspective);
            PhysicalCameraSettings lens_settings = camera.PhysicalSettings();
            lens_settings._lens_projection_mode = ELensProjectionMode::kPhysicalFocalLength;
            lens_settings._sensor_height_mm = 24.0f;
            lens_settings._focal_length_mm = 50.0f;
            camera.PhysicalSettings(lens_settings);
            camera.RecalculateMatrix(true);
            const Matrix4x4f &projection = camera.GetProjNoJitter();
            const f32 vertical_fov = 2.0f * std::atan(1.0f / projection[1][1]) * Math::kRadToDeg;
            Require(std::abs(vertical_fov - kFovDegrees) < kTolerance,
                    "24 mm sensor and 50 mm focal length must produce 26.99 degree vertical FOV");
            Require(std::abs(projection[0][0] * aspect - projection[1][1]) < kTolerance,
                    "projection matrix must derive horizontal scale from vertical FOV and aspect ratio");
        }

        PhysicalCameraSettings lens_settings = exposure_camera.PhysicalSettings();
        lens_settings._lens_projection_mode = ELensProjectionMode::kPhysicalFocalLength;
        lens_settings._sensor_height_mm = 24.0f;
        lens_settings._focal_length_mm = 50.0f;
        exposure_camera.PhysicalSettings(lens_settings);
        const f32 exposure_before_lens_edit = exposure_camera.ComputeExposureMultiplier();
        lens_settings._focal_length_mm = 85.0f;
        exposure_camera.PhysicalSettings(lens_settings);
        Require(exposure_camera.ComputeExposureMultiplier() == exposure_before_lens_edit,
                "changing focal length must not change exposure");
        lens_settings._focal_length_mm = 50.0f;
        exposure_camera.PhysicalSettings(lens_settings);
        const u64 before_exposure_edit = exposure_camera.GetTemporalHistoryResetRevision();
        lens_settings._shutter_seconds = 1.0f / 30.0f;
        exposure_camera.PhysicalSettings(lens_settings);
        Require(exposure_camera.GetTemporalHistoryResetRevision() == before_exposure_edit,
                "exposure-only setting changes must not reset projection history");
        exposure_camera.VerticalFov(60.0f);
        Require(std::abs(exposure_camera.PhysicalSettings()._focal_length_mm -
                         Camera::ComputeFocalLength(24.0f, 60.0f)) < kTolerance,
                "editing vertical FOV in physical mode must update focal length");

        lens_settings = exposure_camera.PhysicalSettings();
        lens_settings._sensor_height_mm = std::numeric_limits<f32>::quiet_NaN();
        lens_settings._focal_length_mm = 0.0f;
        lens_settings._aperture_f_number = std::numeric_limits<f32>::infinity();
        lens_settings._shutter_seconds = -1.0f;
        lens_settings._iso = std::numeric_limits<f32>::quiet_NaN();
        lens_settings._focus_distance_m = std::numeric_limits<f32>::infinity();
        lens_settings._exposure_compensation_ev = std::numeric_limits<f32>::infinity();
        lens_settings._exposure_mode = static_cast<EExposureMode>(255u);
        exposure_camera.PhysicalSettings(lens_settings);
        const PhysicalCameraSettings &sanitized = exposure_camera.PhysicalSettings();
        Require(std::isfinite(sanitized._sensor_height_mm) && std::isfinite(sanitized._focal_length_mm) &&
                    std::isfinite(sanitized._aperture_f_number) && std::isfinite(sanitized._shutter_seconds) &&
                    std::isfinite(sanitized._iso) && std::isfinite(sanitized._focus_distance_m) &&
                    std::isfinite(sanitized._exposure_compensation_ev) &&
                    std::isfinite(exposure_camera.ComputeEv100()) &&
                    std::isfinite(exposure_camera.ComputeExposureMultiplier()),
                "physical camera NaN and infinity inputs must be sanitized");
        Require(sanitized._exposure_mode == EExposureMode::kLegacyManual,
                "unknown exposure mode values must fall back to LegacyManual");

        SceneCameraComponentDocument authored;
        authored._fov_h = 41.25f;
        authored._physical_camera_settings = sanitized;
        authored._physical_camera_settings._exposure_mode = EExposureMode::kManualPhysical;
        authored._physical_camera_settings._lens_projection_mode = ELensProjectionMode::kPhysicalFocalLength;
        authored._physical_camera_settings._focal_length_mm = 85.0f;
        const String name = "camera";
        JsonArchive writer;
        SerializerWrapper<SceneCameraComponentDocument>::Serialize(&authored, writer, &name);
        JsonArchive reader;
        Require(reader.LoadFromString(writer.SaveToString()), "physical camera document JSON parse failed");
        SceneCameraComponentDocument loaded;
        SerializerWrapper<SceneCameraComponentDocument>::Deserialize(&loaded, reader, &name);
        Require(loaded._fov_h == authored._fov_h &&
                    loaded._physical_camera_settings._lens_projection_mode ==
                        ELensProjectionMode::kPhysicalFocalLength &&
                    loaded._physical_camera_settings._exposure_mode == EExposureMode::kManualPhysical &&
                    loaded._physical_camera_settings._focal_length_mm == 85.0f,
                "physical camera settings must roundtrip through scene camera documents");

        JsonArchive legacy_reader;
        Require(legacy_reader.LoadFromString(R"({"camera":{"_fov_h":41.25}})"),
                "legacy camera document JSON parse failed");
        SceneCameraComponentDocument legacy;
        SerializerWrapper<SceneCameraComponentDocument>::Deserialize(&legacy, legacy_reader, &name);
        Camera legacy_camera(16.0f / 9.0f, 0.1f, 100.0f, ECameraType::kPerspective);
        legacy_camera.FovH(legacy._fov_h);
        legacy_camera.PhysicalSettings(legacy._physical_camera_settings);
        Require(legacy_camera.FovH() == 41.25f &&
                    legacy._physical_camera_settings._lens_projection_mode == ELensProjectionMode::kLegacyFov &&
                    legacy._physical_camera_settings._exposure_mode == EExposureMode::kLegacyManual,
                "legacy camera documents must keep their FOV and default new modes to legacy");
    }

    void CheckPhotometricCalibration()
    {
        constexpr f32 kLuminousEfficacy = ECS::kLuminousEfficacyLmPerWatt;
        constexpr f32 kLightIntensityLux = 1000.0f;
        constexpr f32 kLambertianAlbedo = 0.18f;
        constexpr f32 kPi = 3.14159265358979323846f;
        constexpr f32 kTolerance = 0.01f;

        ECS::LightComponent light{};
        light._type = ECS::ELightType::kDirectional;
        light._light._intensity = kLightIntensityLux;
        light._light._intensity_unit = ECS::ELightIntensityUnit::kLux;
        const f32 radiometric_irradiance = ECS::GetLightRadiometricScale(light);
        const f32 scene_luminance = kLambertianAlbedo * radiometric_irradiance / kPi;
        light._light._intensity *= 2.0f;
        Require(std::abs(ECS::GetLightRadiometricScale(light) / radiometric_irradiance - 2.0f) < kTolerance,
                "doubling directional lux must double the radiometric shader input");
        AutoExposureSettings exposure_settings{};
        const f32 calibrated_luminance = scene_luminance * ResolveSceneLuminanceToCdM2(exposure_settings);
        const f32 expected_gray_card_luminance = kLambertianAlbedo * kLightIntensityLux / kPi;
        Require(std::abs(calibrated_luminance - expected_gray_card_luminance) < kTolerance,
                "1000 lux on a 0.18 Lambertian card must map to approximately 57.30 cd/m2");

        const f32 auto_exposure = ComputeMeterExposure(calibrated_luminance, 0.0f) *
                                  ResolveSceneLuminanceToCdM2(exposure_settings);
        const f32 expected_ev100 = std::log2(calibrated_luminance * 8.0f);
        Camera camera(16.0f / 9.0f, 0.1f, 100.0f, ECameraType::kPerspective);
        PhysicalCameraSettings camera_settings = camera.PhysicalSettings();
        camera_settings._exposure_mode = EExposureMode::kManualPhysical;
        camera_settings._aperture_f_number = std::sqrt(std::exp2(expected_ev100));
        camera_settings._shutter_seconds = 1.0f;
        camera_settings._iso = 100.0f;
        camera.PhysicalSettings(camera_settings);
        const f32 manual_exposure = camera.ComputeExposureMultiplier() *
                                     ResolveSceneLuminanceToCdM2(exposure_settings);
        Require(std::abs(scene_luminance * manual_exposure - 0.18f) < kTolerance &&
                    std::abs(manual_exposure - auto_exposure) < kTolerance,
                "Manual Physical and calibrated Auto must map the gray card to the same 0.18 scene value");

        ECS::LightComponent point_light{};
        point_light._type = ECS::ELightType::kPoint;
        point_light._light._intensity = 100.0f;
        point_light._light._intensity_unit = ECS::ELightIntensityUnit::kCandela;
        const f32 point_scale = ECS::GetLightRadiometricScale(point_light);
        Require(std::abs(point_scale * kLuminousEfficacy - point_light._light._intensity) < kTolerance,
                "candela point light must roundtrip through the RGB radiometric conversion");
        point_light._light._intensity_unit = ECS::ELightIntensityUnit::kLumen;
        point_light._light._intensity = ECS::kFourPi * kLuminousEfficacy;
        Require(std::abs(ECS::GetLightRadiometricScale(point_light) - 1.0f) < kTolerance,
                "point lumens must convert through the isotropic 4 pi solid angle");
        point_light._light._intensity_unit = ECS::ELightIntensityUnit::kWatt;
        point_light._light._intensity = ECS::kFourPi;
        Require(std::abs(ECS::GetLightRadiometricScale(point_light) - 1.0f) < kTolerance,
                "radiometric point watts must convert through the isotropic 4 pi solid angle");

        exposure_settings._scene_luminance_to_cd_m2 = 0.0f;
        Require(ResolveSceneLuminanceToCdM2(exposure_settings) == kLuminousEfficacy,
                "invalid photometric scale must fall back to the documented efficacy approximation");
    }

    void CheckHistory(ECameraType type)
    {
        Camera camera(16.0f / 9.0f, 0.1f, 100.0f, type);
        camera.PrepareRenderFrame(10u);
        const Matrix4x4f first = camera.GetViewProjNoJitter();
        Require(Equal(camera.GetPrevViewProj(), first), "first rendered frame must have zero camera motion");

        camera.Position(Vector3f(1.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        camera.Position(Vector3f(2.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        Require(Equal(camera.GetPrevViewProj(), first), "controller rebuild must not advance rendered history");
        camera.PrepareRenderFrame(12u);
        const Matrix4x4f second = camera.GetViewProjNoJitter();
        Require(!Equal(first, second), "test camera must actually move");
        Require(Equal(camera.GetPrevViewProj(), first), "skipped frame must retain last rendered VP");
        camera.PrepareRenderFrame(12u);
        Require(Equal(camera.GetPrevViewProj(), first), "duplicate preparation must not replace previous VP");

        camera.Position(Vector3f(3.0f, 0.0f, 0.0f));
        camera.LookTo(Vector3f(0.2f, 0.0f, 1.0f), Vector3f::kUp);
        camera.PrepareRenderFrame(13u);
        Require(Equal(camera.GetPrevViewProj(), second), "LookTo must retain the previous rendered VP");

        camera.MarkTemporalHistoryReset();
        camera.Position(Vector3f(30.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        camera.PrepareRenderFrame(14u);
        const Matrix4x4f reset = camera.GetViewProjNoJitter();
        Require(Equal(camera.GetPrevViewProj(), reset), "reset must align previous VP on the next rendered view");
        camera.Position(Vector3f(31.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        camera.PrepareRenderFrame(15u);
        Require(Equal(camera.GetPrevViewProj(), reset), "motion must resume after reset");

        Matrix4x4f custom_view = camera.GetView();
        const Matrix4x4f custom_projection = camera.GetProjNoJitter();
        const Matrix4x4f before_custom = camera.GetViewProjNoJitter();
        custom_view[3][0] += 1.0f;
        camera.SetViewAndProj(custom_view, custom_projection);
        camera.PrepareRenderFrame(16u);
        Require(Equal(camera.GetPrevViewProj(), before_custom), "custom VP must retain previous rendered state");
        Require(Equal(camera.GetViewProjJittered(), camera.GetViewProjNoJitter()), "custom VP must remain jitter-free");
    }

    void CheckFirstFrameHandles()
    {
        // Reused heap storage must not select a temporal output before its first producer is registered.
        alignas(RenderingData) u8 storage[sizeof(RenderingData)];
        std::memset(storage, 0xCD, sizeof(storage));
        auto *data = new (storage) RenderingData;
        const bool valid = !data->_rg_handles._upscaled_color.IsValid() && !data->_rg_handles._taa_color.IsValid() &&
                           !data->_rg_handles._final_color.IsValid() && !data->_rg_handles._final_depth.IsValid();
        data->~RenderingData();
        Require(valid, "first-frame temporal/final handles must be invalid even in reused heap storage");
    }
}

int main()
{
    try
    {
        CheckFirstFrameHandles();
        CheckPhysicalCameraOracles();
        CheckPhotometricCalibration();
        CheckHistory(ECameraType::kPerspective);
        CheckHistory(ECameraType::kOrthographic);
        std::cout << "CameraTemporalHistorySmoke PASS: EV/FOV, photometric gray card, and temporal history\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "CameraTemporalHistorySmoke FAIL: " << error.what() << '\n';
        return 1;
    }
}
