#ifndef __DOCK_MGR_H__
#define __DOCK_MGR_H__
#include "DockWindow.h"
#include "generated/DockManager.gen.h"

namespace Ailu
{
    class Window;
    namespace Editor
    {
        AENUM()
        enum class EDockArea
        {
            kFloat,
            kCenter,
            kLeft,
            kRight,
            kTop,
            kBottom
        };

        ASTRUCT()
        struct DockNodeData
        {
            GENERATED_BODY()
            APROPERTY()
            String _type_id;
            APROPERTY()
            u32 _id;       // 节点唯一 ID
            APROPERTY()
            u32 _parent_id;// 父节点 ID (根节点 parent_id = UINT32_MAX)
            APROPERTY()
            u32 _type;     // EType::kLeaf / kSplit
            APROPERTY()
            bool _is_vertical_split;
            APROPERTY()
            f32 _split_ratio;
            APROPERTY()
            String _title;
            APROPERTY()
            u32 _window_id;
            APROPERTY()
            String _tab_id;   // Tab 唯一名字或 ID
            APROPERTY()
            Vector2f _window_position;
            APROPERTY()
            Vector2f _window_size; 
            APROPERTY()
            Vector2f _position;
            APROPERTY()
            Vector2f _size;
            APROPERTY()
            u32 _flags;
        };

        ASTRUCT()
        struct DockNodeDataArray
        {
            GENERATED_BODY()
            APROPERTY()
            Vector<DockNodeData> _node_data;
        };

        
        struct DockNode;
        class DockManager
        {
        public:
            static void Init();
            static void Shutdown();
            static DockManager &Get();
            using NodeTravelFunc = bool(DockNode *);
        public:
            DockManager();
            ~DockManager();
            void AddDock(Ref<DockWindow> dock);
            void RemoveDock(DockWindow *dock);
            void Update(f32 dt);

            void BeginFloatWindow(DockWindow* w);
            void BeginFloatNode(DockNode* w);
            void EndFloatWindow(Vector2f drop_pos);
            void MarkDeleteNode(DockNode* node);
        private:
            void OnWindowFloat();
            void DrawPreviewDockArea(Vector2f pos, Vector2f size, Vector2f start_pos = Vector2f::kZero);
            void UpdateDockNode(DockNode *node);
            //处理节点move/resize/split size
            void HandleNodeResize();
            void TryAddFloatNode(Ref<DockNode>& n);
            void TryRemoveFloatNode(DockNode* n);
            void DetachFromTree(DockNode* n);
            void WindowEventHandler(Event &e);
            //将docknode 保存为docknodedata用于序列化
            void WriteNodeData(DockNode *n);
            Window *CreateNewWindow(String title,u16 w,u16 h,bool is_sync = false);
        public:
            Vector<DockNode*> _roots;
        private:
            inline static const f32 kWindowDetachThreshold = 30.0f;
            DockWindow* _floating_preview_window;
            bool _is_any_floating = false;

            HashMap<Window *, Vector<Ref<DockNode>>> _float_nodes;
            bool _is_any_docked = false;//标记起始状态，false表示所有窗口都在浮动状态 
            Color _dock_quad_normal_color = {1.0f,1.0f,1.0f,0.2f};
            Color _dock_quad_hover_color = {1.0f,1.0f,1.0f,0.8f};
            EDockArea _dock_quad_hover_area = EDockArea::kFloat;
            DockNode *_dock_quad_hover_node = nullptr;
            DockNode* _floating_preview_node = nullptr;
            DockNode *_resizing_node = nullptr;
            DockNode *_adj_split_node = nullptr;
            DockNode *_drag_move_node = nullptr;
            Vector2f _drag_start_offset;//拖拽开始时节点位置与鼠标位置的偏移
            Vector2f _float_node_start_pos;//记录按下标题栏的鼠标位置，移动一定距离后才显示预览矩形
            bool _can_draw_float_preview = false;
            u32 _resizing_edge_dir = 0;
            bool _is_float_on_cancel_area = false;
            bool _is_any_float_node_invalid = false;
            bool _is_float_node_external_window = false;//当前浮动拖拽的节点是否属于子窗口
            Vector<Scope<Window>> _float_windows;
            DockNodeDataArray _node_data_array;
            WString _dock_layout_path;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__DOCK_MGR_H__
