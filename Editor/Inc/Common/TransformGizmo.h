#include "Framework/Core/Delegate.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Math/Color.h"
#include "Framework/Math/Matrix.hpp"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/Transform.h"
#include "Scene/Entity.h"
#include <optional>

namespace Ailu
{
    using Math::Color;
    using Math::Matrix4x4f;
    using Math::Quaternion;

    namespace ECS
    {
        struct TransformComponent;
    };
    struct OBB;
    namespace SceneManagement
    {
        class Scene;
    }
    namespace Render
    {
        class Material;
        class Camera;
    }
    namespace Editor
    {
        enum class EGizmoMode
        {
            kTranslate,
            kRotate,
            kScale
        };
        enum class EGizmoSpace
        {
            kWorld,
            kLocal
        };

        class TransformGizmo
        {
        public:
            TransformGizmo();
            ~TransformGizmo();
            void SetTarget(SceneManagement::Scene *scene, ECS::Entity target)
            {
                _hover_axis = -1;
                _target_scene = scene;
                _target_entity = target;
            }
            void ClearTarget();
            void SetMode(EGizmoMode mode) { _mode = mode; }
            void SetSpace(EGizmoSpace space) { _space = space; }
            void ToggleSpace();
            EGizmoSpace Space() const { return _space; }

            void Update(f32 dt, Vector2f mouse_pos,Render::Camera* cam);
            void Draw();
            bool IsHover(Vector2f pos, Render::Camera *cam, u32 *hover_axis = nullptr) const;
            // 新增：开始与结束拖拽
            void BeginDrag(Vector2f mouse_pos);
            void EndDrag();
            bool IsDragging() const { return _is_dragging; }
            void SetSnapEnabled(bool enabled) { _snap_enabled = enabled; }
            bool IsSnapEnabled() const { return _snap_enabled; }
        private:
            ECS::TransformComponent *Target() const;
            u32 PickAxis(Vector3f start, Vector3f dir) const;
            void SyncTargetFromSelection();
            void RefreshAxisDirections();
            ECS::TransformComponent *ParentTarget() const;
            Vector3f WorldPositionToTargetLocal(const Vector3f &world_position) const;
            Quaternion WorldRotationToTargetLocal(const Quaternion &world_rotation) const;
            void SetTargetWorldPosition(ECS::TransformComponent *target, const Vector3f &world_position) const;
            void SetTargetWorldRotation(ECS::TransformComponent *target, const Quaternion &world_rotation) const;

            // 轴方向（根据世界/本地空间）
            Vector3f GetAxisDirWorld(int axisId) const;

            // 计算当前鼠标射线下，沿轴的参数s值 * kVirualRayLen,也就是世界空间的长度
            float ComputeAxisParamS(Vector2f mouse_pos, const Vector3f &origin, const Vector3f &axisDir) const;

        private:
            SceneManagement::Scene *_target_scene = nullptr;
            ECS::Entity _target_entity = ECS::kInvalidEntity;
            std::optional<Delegate<>::Handle> _selection_changed_handle;
            EGizmoMode _mode = EGizmoMode::kTranslate;
            EGizmoSpace _space = EGizmoSpace::kWorld;
            Render::Camera *_cam;
            u32 _hover_axis = 0u;
            Vector2f _mouse_pos;
            f32 _axis_length = 1.5f, _axis_radius = 0.03f;
            f32 _scaled_axis_length = 1.5f, _scaled_axis_radius = 0.03f;
            f32 _axis_quad_width_scale = 0.2f;//translate模式，轴之间的四边形宽度比例相对于轴长度

            // 拖拽状态
            bool _is_dragging = false;
            u32 _drag_axis = 0u;
            Vector3f _drag_origin;     // 轴起点（通常是target世界位置）
            struct DragAxisCtx
            {
                Vector3f _drag_axis_dir;   // 轴方向（世界空间单位向量）
                f32 _drag_start_s = 0.0f;// 按下时沿轴的参数s
            };
            Array<DragAxisCtx, 3> _drag_axis_ctx;
            //位移模式时：双轴拖拽起始点；旋转模式：鼠标按下时在旋转平面上的投影点，长度固定为其半径
            Vector3f _drag_start_hit;
            Vector3f _drag_current_hit;
            u32 _drag_axis_num = 0u;
            Vector3f _drag_start_pos;// 按下时target初始位置
            Quaternion _drag_start_rot;// 按下时target初始旋转
            Vector3f _drag_start_scale;// 按下时target初始缩放
            Transform _drag_start_local_transform;
            bool _has_drag_start_transform = false;
            Vector3f _drag_scale_factor = Vector3f::kOne;//scale模式下当前缩放，用于临时修改缩放模式下gizmo的轴长度
            f32 _dis_scale = 1.0f;
            bool _snap_enabled = false;
            f32 _translate_snap_step = 1.0f;
            f32 _rotate_snap_degrees = 15.0f;
            f32 _scale_snap_step = 0.1f;
            Vector3f _cur_target_pos;
            Vector2f _drag_start_mouse_pos;
            Vector2f _drag_start_target_delta;//scale模式按下三轴时，鼠标位置 - target屏幕位置
            inline static const Color kHoverColor = Colors::kYellow;
            inline static const Color kDragingColor = Colors::kYellow;
            inline static const Color kNormalColors[] = {Colors::kRed, Colors::kGreen,Colors::kBlue};
            inline static const Color kInactiveColor = {0.5f,0.5f,0.5f,0.15f};
            enum EAxis
            {
                kInvalidAxis = 0,
                kAxisX = 1,
                kAxisY = 2,
                kAxisZ = 4,
                kAxisXY = kAxisX | kAxisY,
                kAxisXZ = kAxisX | kAxisZ,
                kAxisYZ = kAxisY | kAxisZ,
                kAxisXYZ = kAxisX | kAxisY | kAxisZ
            };
            bool Is2DMode() const;
            u32 Get2DHiddenAxisMask() const;
            u32 Get2DVisibleAxisMask() const;
            bool IsAxisMaskAvailable(u32 axis_mask) const;
            bool IsSnapActive() const;
            f32 SnapFloat(f32 value, f32 step) const;
            Vector3f SnapVector(const Vector3f &value, f32 step) const;

            struct TranslateAxis
            {
                Vector3f _dir;
                Matrix4x4f _matrix;
                Ref<Render::Material> _mat;
                u16 _index;
                u16 _axis;//flag kAxisX/Y/Z
                Vector3f _default_dir;
            };
            Array<TranslateAxis, 3> _translate_axis;
            Array<Scope<OBB>,3> _plane_obbs;
            Scope<OBB> _scale_center_obb;
            Array<Ref<Render::Material>, 3> _rotate_rings;
            Quaternion _drag_rot;//rotate模式下当前旋转，用于临时修改旋转模式下gizmo的轴方向
        };
    }// namespace Editor
}// namespace Ailu
