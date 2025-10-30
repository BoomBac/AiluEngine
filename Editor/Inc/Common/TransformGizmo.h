#include "Framework/Math/Transform.h"

namespace Ailu
{
    struct OBB;
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
            void SetTarget(Transform *target)
            {
                _hover_axis = -1;
                _target = target;
            }
            void SetMode(EGizmoMode mode) { _mode = mode; }
            void SetSpace(EGizmoSpace space) { _space = space; }

            void Update(f32 dt, Vector2f mouse_pos,Render::Camera* cam);
            void Draw();
            bool IsHover(Vector2f pos, Render::Camera *cam, u32 *hover_axis = nullptr) const;
            // 新增：开始与结束拖拽
            void BeginDrag(Vector2f mouse_pos);
            void EndDrag();
            bool IsDragging() const { return _is_dragging; }
        private:
            u32 PickAxis(Vector3f start, Vector3f dir) const;

            // 轴方向（根据世界/本地空间）
            Vector3f GetAxisDirWorld(int axisId) const;

            // 计算当前鼠标射线下，沿轴的参数s值 * kVirualRayLen,也就是世界空间的长度
            float ComputeAxisParamS(Vector2f mouse_pos, const Vector3f &origin, const Vector3f &axisDir) const;

        private:
            Transform *_target = nullptr;
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
            Vector3f _drag_scale_factor = Vector3f::kOne;//scale模式下当前缩放，用于临时修改缩放模式下gizmo的轴长度
            f32 _dis_scale = 1.0f;
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