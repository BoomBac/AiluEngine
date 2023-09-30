#include "pch.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/RenderingData.h"

#include "Objects/Actor.h"
#include "Objects/TransformComponent.h"
#include "Framework/Common/SceneMgr.h"

namespace ImguiTree
{
    struct TreeNode 
    {
        std::string name;
        std::vector<TreeNode> children;
    };

    // 递归绘制树节点
    void DrawTreeNode(TreeNode& node) 
    {
        ImGui::PushID(&node);

        bool nodeOpen = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow);

        //// 处理拖拽行为
        //if (ImGui::BeginDragDropSource()) {
        //    ImGui::SetDragDropPayload("DND_TREENODE", &node, sizeof(TreeNode));
        //    ImGui::Text("Drag %s", node.name.c_str());
        //    ImGui::EndDragDropSource();
        //}

        //if (ImGui::BeginDragDropTarget()) {
        //    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_TREENODE")) {
        //        // 在此处处理拖拽的节点
        //        TreeNode* draggedNode = (TreeNode*)payload->Data;
        //        node.children.push_back(*draggedNode);
        //    }
        //    ImGui::EndDragDropTarget();
        //}

        if (nodeOpen) {
            for (TreeNode& child : node.children) {
                DrawTreeNode(child);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

namespace Ailu
{
    static Ref<SceneActor> s_cur_selected_actor = nullptr;
    static int cur_object_index = 0u;
    static uint32_t cur_tree_node_index = 0u;

    namespace TreeStats
    {
        static ImGuiTreeNodeFlags s_base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        static int s_selected_mask = 0;
        static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
        static int s_node_clicked = -1;
        static uint32_t s_pre_frame_selected_actor_id = 10;
        static uint32_t s_cur_frame_selected_actor_id = 0;
    }


    static void DrawProperty(SerializableProperty& prop)
    {
        if (prop._type_name == "Vector3f")
        {
            ImGui::DragFloat3(prop._name.c_str(), static_cast<Vector3f*>(prop._value_ptr)->data);
        }
        else if(prop._type_name == "Color")
        {
            ImGui::ColorEdit3(prop._name.c_str(), static_cast<Vector4f*>(prop._value_ptr)->data);
        }
        else if (prop._type_name == "float")
        {
            ImGui::DragFloat(prop._name.c_str(), static_cast<float*>(prop._value_ptr));
        }
        else if (prop._type_name == "bool")
        {
            ImGui::Checkbox(prop._name.c_str(), static_cast<bool*>(prop._value_ptr));
        }
        else
        {
            LOG_WARNING("prop value type: {} havn't be handle!", prop._type_name)
        }
    }
    Ailu::ImGUILayer::ImGUILayer()
    {
    }

    Ailu::ImGUILayer::~ImGUILayer()
    {
        // Cleanup
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    static ImFont* font0 = nullptr;

    void Ailu::ImGUILayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        auto context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        //io.MouseDrawCursor = true;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
        font0 = io.Fonts->AddFontFromFileTTF(GET_ENGINE_FULL_PATH(Res/Fonts/VictorMono-Regular.ttf), 13.0f);
        io.Fonts->Build();
        ImGuiStyle& style = ImGui::GetStyle();
        // Setup Dear ImGui style
        ImGui::StyleColorsDark(&style);
        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 10.0f;
            style.ChildRounding = 10.0f;
            style.FrameRounding = 10.0f;
            style.Colors[ImGuiCol_WindowBg].w = 0.8f;
        }

        ImGui_ImplWin32_Init(Application::GetInstance()->GetWindow().GetNativeWindowPtr());
    }

    void Ailu::ImGUILayer::OnDetach()
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void Ailu::ImGUILayer::OnEvent(Event& e)
    {

    }
    static bool show = false;

    void Ailu::ImGUILayer::OnImguiRender()
    {
        ImGui::PushFont(font0);
        ImGui::Begin("Performace Statics");                          // Create a window called "Hello, world!" and append into it.
        ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
        ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
        ImGui::Text("Draw Call: %d", RenderingStates::s_draw_call);
        ImGui::Text("VertCount: %d", RenderingStates::s_vertex_num);
        ImGui::Text("TriCount: %d", RenderingStates::s_triangle_num);

        static const char* items[] = { "Shadering", "WireFrame", "ShaderingWireFrame"};
        static int item_current_idx = 0; // Here we store our selection data as an index.
        const char* combo_preview_value = items[item_current_idx];  // Pass in the preview value visible before opening the combo (it could be anything)
        if (ImGui::BeginCombo("Shadering Mode", combo_preview_value, 0))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                const bool is_selected = (item_current_idx == n);
                if (ImGui::Selectable(items[n], is_selected))
                    item_current_idx = n;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (item_current_idx == 0) RenderingStates::s_shadering_mode = EShaderingMode::kShader;
        else if (item_current_idx == 1) RenderingStates::s_shadering_mode = EShaderingMode::kWireFrame;
        else RenderingStates::s_shadering_mode = EShaderingMode::kShaderedWireFrame;

        ImGui::SliderFloat("Gizmo Alpha:", &Gizmo::s_color.a, 0.2f, 1.0f,"%.2f");
        ImGui::SliderFloat("Game Time Scale:", &TimeMgr::TimeScale, 0.0f, 0.2f,"%.2f");

        ImGui::Checkbox("Expand", &show);
        ImGui::End();
        ImGui::PopFont();

        if (show) ImGui::ShowDemoWindow(&show);
        ShowWorldOutline();
        ShowObjectDetail();
    }

    void Ailu::ImGUILayer::Begin()
    {
        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    void Ailu::ImGUILayer::End()
    {
        const Window& window = Application::GetInstance()->GetWindow();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));

        ImGui::EndFrame();
        ImGui::Render();
    }



    void DrawTreeNode(Ref<SceneActor>& actor)
    {
        using namespace TreeStats;
        ImGuiTreeNodeFlags node_flags = s_base_flags;
        s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
        ImGui::PushID(cur_object_index);
        cur_tree_node_index++;
        if (s_b_selected) node_flags |= ImGuiTreeNodeFlags_Selected;
        if (cur_tree_node_index - 1 == 0) node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
        //绘制根节点
        bool b_root_node_open = ImGui::TreeNodeEx((void*)(intptr_t)cur_tree_node_index,node_flags,
            std::format("Root node id:{},current selected: {},node mask: {}", cur_tree_node_index - 1, s_node_clicked,s_selected_mask).c_str());
        if (ImGui::IsItemClicked())
            s_node_clicked = cur_tree_node_index - 1;
        if (b_root_node_open)
        {
            s_cur_frame_selected_actor_id = actor->Id();
            for (auto node : actor->GetAllChildren())
            {
                if (node->GetChildNum() > 0)
                {
                    auto scene_node = std::static_pointer_cast<SceneActor>(node);
                    DrawTreeNode(scene_node);
                }
                else
                {
                    node_flags = s_base_flags;
                    node_flags |= ImGuiTreeNodeFlags_Leaf;// | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
                    if (s_b_selected)
                        node_flags |= ImGuiTreeNodeFlags_Selected;
                    ImGui::PushID(cur_tree_node_index); // PushID 需要在进入子节点前
                    cur_tree_node_index++;
                    if (ImGui::TreeNodeEx((void*)(intptr_t)cur_tree_node_index, node_flags, std::format("node id:{},actor name: {}", cur_tree_node_index - 1,node->Name()).c_str()))
                    {
                        if (ImGui::IsItemClicked())
                            s_node_clicked = cur_tree_node_index - 1;
                        s_cur_frame_selected_actor_id = node->Id();
                        //ImGui::Text("占位符");
                        //ImGui::SameLine();
                        //if (ImGui::SmallButton("button")) {}
                        s_selected_mask = (1 << s_node_clicked);
                        ImGui::TreePop(); // TreePop 需要在退出子节点后
                    }
                    ImGui::PopID(); // PopID 需要与对应的 PushID 配对
                }
            }
            ImGui::TreePop(); // TreePop 需要在退出当前节点后
        }
        ImGui::PopID(); // PopID 需要与对应的 PushID 配对
    }

    void ImGUILayer::ShowWorldOutline()
    {
        using namespace TreeStats;
        ImGui::Begin("WorldOutline");                          // Create a window called "Hello, world!" and append into it.
        auto object_list = Actor::s_global_actors;
        uint32_t index = 0;

        DrawTreeNode(g_pSceneMgr->_p_current->GetSceneRoot());
        s_cur_frame_selected_actor_id = s_node_clicked;
        if (s_pre_frame_selected_actor_id != s_cur_frame_selected_actor_id)
        {
            s_cur_selected_actor = s_cur_frame_selected_actor_id == 0? nullptr : g_pSceneMgr->_p_current->GetSceneActorByIndex(s_cur_frame_selected_actor_id);
        }
        s_pre_frame_selected_actor_id = s_cur_frame_selected_actor_id;

        cur_tree_node_index = 0;
        ImGui::End();
    }

    void ImGUILayer::ShowObjectDetail()
    {
        ImGui::Begin("ObjectDetail");
        if (s_cur_selected_actor != nullptr)
        {
            ImGui::Text(s_cur_selected_actor->Name().c_str());
            uint32_t comp_index = 0;
            for (auto& comp : s_cur_selected_actor->GetAllComponent())
            {
                ImGui::PushID(comp_index);
                bool b_active = comp->Active();
                ImGui::Checkbox(" ", &b_active);
                ImGui::SameLine();
                ImGui::Text(comp->GetTypeName().c_str());
                ImGui::PopID();
                comp->Active(b_active);
                if (comp->GetTypeName() == "LightComponent")
                {
                    for (auto& prop : comp->GetAllProperties())
                    {
                        DrawProperty(prop.second);
                    }
                    auto light = static_cast<LightComponent*>(comp.get());
                    if (light->_light_type == ELightType::kDirectional) continue;
                    else if (light->_light_type == ELightType::kPoint)
                    {
                        auto& light_data = light->_light;
                        ImGui::DragFloat("Radius", &light_data._light_param.x);
                    }
                    else
                    {
                        auto& light_data = light->_light;
                        ImGui::DragFloat("Radius", &light_data._light_param.x);
                        ImGui::DragFloat("InnerAngle", &light_data._light_param.y);
                        ImGui::DragFloat("OuterAngle", &light_data._light_param.z);
                    }
                }
                else
                {
                    for (auto& prop : comp->GetAllProperties())
                    {
                        DrawProperty(prop.second);
                    }
                }
                ++comp_index;
            }
        }
        ImGui::End();
    }
}


