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
    float a = 0.5;
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

    int cur_object_index = 0u;
    const char* labels[] = { "aa","bb" };
    static bool expand_child = false;

    static uint32_t cur_tree_node_index = 0u;

    void DrawTreeNode(Ref<Ailu::Actor>& actor)
    {
        ImGui::PushID(cur_object_index);
        if (ImGui::TreeNode((void*)(intptr_t)cur_tree_node_index, actor->Name().c_str()))
        {
            cur_tree_node_index++;
            for (auto node : actor->GetAllChildren())
            {
                ImGui::PushID(cur_tree_node_index); // PushID 需要在进入子节点前
                cur_tree_node_index++;
                if (ImGui::TreeNode((void*)(intptr_t)cur_tree_node_index, node->Name().c_str()))
                {
                    ImGui::Text("占位符");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("button")) {}
                    ImGui::TreePop(); // TreePop 需要在退出子节点后
                }
                ImGui::PopID(); // PopID 需要与对应的 PushID 配对
            }
            ImGui::TreePop(); // TreePop 需要在退出当前节点后
        }
        ImGui::PopID(); // PopID 需要与对应的 PushID 配对
    }

    void ShowObjectDetail(Ref<Ailu::Actor>& actor)
    {
        
        ImGui::Begin("WorldOutline");

        ImGui::End();
    }


    void Ailu::ImGUILayer::ShowWorldOutline()
    {
        ImGui::Begin("WorldOutline");                          // Create a window called "Hello, world!" and append into it.
        auto object_list = Actor::s_global_actors;
        uint32_t index = 0;
        //ImGui::Checkbox("Show Child", &expand_child);
        DrawTreeNode(object_list[0]);
        //if (ImGui::TreeNode("Basic trees"))
        //{
        //    for (int i = 0; i < 5; i++)
        //    {
        //        ImGui::PushID(cur_object_index);
        //        // Use SetNextItemOpen() so set the default state of a node to be open. We could
        //        // also use TreeNodeEx() with the ImGuiTreeNodeFlags_DefaultOpen flag to achieve the same thing!
        //        //if (i == 0)
        //        //    ImGui::SetNextItemOpen(true, ImGuiCond_Once);

        //        if (ImGui::TreeNode((void*)(intptr_t)i, "Child %d", i))
        //        {
        //            ImGui::Text("blah blah");
        //            ImGui::SameLine();
        //            if (ImGui::SmallButton("button")) {}
        //            ImGui::TreePop();
        //        }
        //        ImGui::PopID();
        //    }
        //    ImGui::TreePop();
        //}
        cur_tree_node_index = 0;
        auto transform = object_list[0]->GetComponent<Ailu::TransformComponent>();
        Vector3f new_pos = transform->Position();
        ImGui::DragFloat3("Position", new_pos);
        transform->Position(new_pos);

        ImGui::End();
    }
}


