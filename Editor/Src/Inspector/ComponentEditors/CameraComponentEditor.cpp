#include "Inspector/ComponentEditors/CameraComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Scene.h"

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        void CameraComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::CCamera>();
            if (comp == nullptr || context._content == nullptr)
                return;

            auto items = Vector<String>{"Orthographic", "Perspective"};
            auto type_dropdown = Editor::AddDropdownRow(context._content, "Type", items);
            type_dropdown->SetSelectedIndex(static_cast<i32>(comp->_camera.Type()), false);
            type_dropdown->_on_selected_changed += [comp, context](i32 idx)
            {
                comp->_camera.Type(static_cast<Render::ECameraType>(idx));
                context.MarkSceneDirty();
            };

            Editor::AddFloatInputRow(context._content, "Near", std::format("{}", comp->_camera.Near()),
                [comp, context](f32 v)
            {
                comp->_camera.Near(v);
                context.MarkSceneDirty();
            });

            Editor::AddFloatInputRow(context._content, "Far", std::format("{}", comp->_camera.Far()),
                [comp, context](f32 v)
            {
                comp->_camera.Far(v);
                context.MarkSceneDirty();
            });

            Editor::AddFloatInputRow(context._content, "Aspect", std::format("{}", comp->_camera.Aspect()),
                [comp, context](f32 v)
            {
                comp->_camera.Aspect(v);
                context.MarkSceneDirty();
            });

            Editor::AddFloatInputRow(context._content, "Render Scale",
                std::format("{:.2f}", comp->_camera.RenderScale()), [comp, context](f32 value)
            {
                comp->_camera.RenderScale(value);
                context.MarkSceneDirty();
            });

            auto upsampling_dropdown = Editor::AddDropdownRow(
                context._content, "Upsampling", Vector<String>{"Bilinear", "Point"});
            upsampling_dropdown->SetSelectedIndex(static_cast<i32>(comp->_camera.UpsamplingFilter()));
            upsampling_dropdown->_on_selected_changed += [comp, context](i32 index)
            {
                if (index < 0 || index > 1) return;
                comp->_camera.UpsamplingFilter(static_cast<Render::EUpsamplingFilter>(index));
                context.MarkSceneDirty();
            };

            if (comp->_camera.Type() == Render::ECameraType::kOrthographic)
            {
                Editor::AddFloatInputRow(context._content, "Size", std::format("{}", comp->_camera.Size()),
                    [comp, context](f32 v)
                {
                    comp->_camera.Size(v);
                    context.MarkSceneDirty();
                });
            }

            const auto update_physical_settings = [comp, context](
                const std::function<void(Render::PhysicalCameraSettings &)> &update)
            {
                Render::PhysicalCameraSettings settings = comp->_camera.PhysicalSettings();
                update(settings);
                comp->_camera.PhysicalSettings(settings);
                context.MarkSceneDirty();
            };

            if (comp->_camera.Type() == Render::ECameraType::kPerspective)
            {
                Render::PhysicalCameraSettings settings = comp->_camera.PhysicalSettings();
                auto lens_mode = Editor::AddDropdownRow(
                    context._content, "Lens Projection", {"Legacy FOV", "Physical Focal Length"});
                lens_mode->SetSelectedIndex(static_cast<i32>(settings._lens_projection_mode), false);
                lens_mode->_on_selected_changed += [update_physical_settings](i32 index)
                {
                    if (index < 0 || index > 1)
                        return;
                    update_physical_settings([index](Render::PhysicalCameraSettings &value)
                    {
                        value._lens_projection_mode = static_cast<Render::ELensProjectionMode>(index);
                    });
                };

                if (settings._lens_projection_mode == Render::ELensProjectionMode::kPhysicalFocalLength)
                {
                    Editor::AddFloatInputRow(context._content, "Sensor Width (mm)",
                        std::format("{:.2f}", settings._sensor_width_mm),
                        [update_physical_settings](f32 value)
                        {
                            update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                            {
                                physical._sensor_width_mm = value;
                            });
                        });
                    Editor::AddFloatInputRow(context._content, "Sensor Height (mm)",
                        std::format("{:.2f}", settings._sensor_height_mm),
                        [update_physical_settings](f32 value)
                        {
                            update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                            {
                                physical._sensor_height_mm = value;
                            });
                        });
                    Editor::AddFloatInputRow(context._content, "Focal Length (mm)",
                        std::format("{:.2f}", settings._focal_length_mm),
                        [update_physical_settings](f32 value)
                        {
                            update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                            {
                                physical._focal_length_mm = value;
                            });
                        });
                    const auto add_read_only_row = [context](const String &label, const String &value)
                    {
                        UI::HorizontalBox *value_box = nullptr;
                        Editor::AddPropertyRow(context._content, label, &value_box);
                        value_box->AddChild<UI::Text>(value);
                    };
                    add_read_only_row("Vertical FOV", std::format("{:.2f} deg", comp->_camera.FovH()));
                }
                else
                {
                    Editor::AddFloatInputRow(context._content, "Vertical FOV (deg)",
                        std::format("{:.2f}", comp->_camera.FovH()), [comp, context](f32 value)
                        {
                            comp->_camera.FovH(value);
                            context.MarkSceneDirty();
                        });
                }
            }

            const Vector<String> exposure_modes = {
                "Legacy Manual", "Manual Physical", "Auto", "Aperture Priority", "Shutter Priority", "Auto ISO"};
            auto exposure_mode = Editor::AddDropdownRow(context._content, "Exposure Mode", exposure_modes);
            exposure_mode->SetSelectedIndex(static_cast<i32>(comp->_camera.PhysicalSettings()._exposure_mode), false);
            exposure_mode->_on_selected_changed += [update_physical_settings](i32 index)
            {
                if (index < 0 || index > static_cast<i32>(Render::EExposureMode::kAutoISO))
                    return;
                update_physical_settings([index](Render::PhysicalCameraSettings &value)
                {
                    value._exposure_mode = static_cast<Render::EExposureMode>(index);
                });
            };

            const Render::PhysicalCameraSettings settings = comp->_camera.PhysicalSettings();
            Editor::AddFloatInputRow(context._content, "Focus Distance (m; used by DOF)",
                std::format("{:.2f}", settings._focus_distance_m), [update_physical_settings](f32 value)
                {
                    update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                    {
                        physical._focus_distance_m = value;
                    });
                });
            Editor::AddFloatInputRow(context._content, "Aperture (f/; used by DOF)",
                std::format("{:.2f}", settings._aperture_f_number), [update_physical_settings](f32 value)
                {
                    update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                    {
                        physical._aperture_f_number = value;
                    });
                });
            if (settings._exposure_mode != Render::EExposureMode::kLegacyManual)
            {
                Editor::AddFloatInputRow(context._content, "Shutter (seconds)",
                    std::format("{:.6f}", settings._shutter_seconds), [update_physical_settings](f32 value)
                    {
                        update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                        {
                            physical._shutter_seconds = value;
                        });
                    });
                Editor::AddFloatInputRow(context._content, "ISO", std::format("{:.0f}", settings._iso),
                    [update_physical_settings](f32 value)
                    {
                        update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                        {
                            physical._iso = value;
                        });
                    });
                Editor::AddFloatInputRow(context._content, "Exposure Compensation (EV)",
                    std::format("{:.2f}", settings._exposure_compensation_ev),
                    [update_physical_settings](f32 value)
                    {
                        update_physical_settings([value](Render::PhysicalCameraSettings &physical)
                        {
                            physical._exposure_compensation_ev = value;
                        });
                    });
                UI::HorizontalBox *ev_box = nullptr;
                Editor::AddPropertyRow(context._content, "Camera EV100 (settings)", &ev_box);
                ev_box->AddChild<UI::Text>(std::format("{:.2f}", comp->_camera.ComputeEv100()));
                UI::HorizontalBox *exposure_box = nullptr;
                Editor::AddPropertyRow(context._content, "Reference Exposure Multiplier", &exposure_box);
                exposure_box->AddChild<UI::Text>(
                    std::format("{:.5f}", comp->_camera.ComputeExposureMultiplier()));
            }
            else
            {
                UI::HorizontalBox *legacy_box = nullptr;
                Editor::AddPropertyRow(context._content, "Legacy exposure", &legacy_box);
                legacy_box->AddChild<UI::Text>("Controlled by Postprocess Volume");
            }

            _cached_type = static_cast<i32>(comp->_camera.Type());
            _cached_lens_mode = static_cast<i32>(comp->_camera.PhysicalSettings()._lens_projection_mode);
            _cached_exposure_mode = static_cast<i32>(comp->_camera.PhysicalSettings()._exposure_mode);
        }

        bool CameraComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
        {
            const auto *comp = context.GetComponent<ECS::CCamera>();
            return comp == nullptr || _cached_type != static_cast<i32>(comp->_camera.Type()) ||
                   _cached_lens_mode != static_cast<i32>(comp->_camera.PhysicalSettings()._lens_projection_mode) ||
                   _cached_exposure_mode != static_cast<i32>(comp->_camera.PhysicalSettings()._exposure_mode);
        }
    }// namespace Editor
}// namespace Ailu
