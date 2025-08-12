#include "Widgets/RenderGraphDebugger.h"
#include "Ext/imnodes/imnodes.h"
#include <Render/RenderPipeline.h>
#include <Render/RenderGraph/RenderGraph.h>
#include <Framework/Events/MouseEvent.h>

namespace Ailu::Editor
{
    RenderGraphDebugger::RenderGraphDebugger() : ImGuiWidget("RenderGraphDebugger")
    {
        _allow_close = true;
    }
    RenderGraphDebugger::~RenderGraphDebugger()
    {
    }
    void RenderGraphDebugger::Open(const i32 &handle)
    {
        ImGuiWidget::Open(handle);
    }
    void RenderGraphDebugger::Close(i32 handle)
    {
        ImGuiWidget::Close(handle);
    }
    static f32 s_zoom = 1.0f;
    void RenderGraphDebugger::OnEvent(Event &e)
    {
        if (e.GetEventType() == EEventType::kMouseScrolled)
        {
            auto &mouse_event = static_cast<MouseScrollEvent &>(e);
            if (mouse_event.GetOffsetY() > 0)
                s_zoom += 0.1f;
            else
                s_zoom -= 0.1f;
            Math::Clamp(s_zoom, 0.0f, 1.0f);
            ImNodes::EditorContextSetZoom(s_zoom, ImGui::GetIO().MousePos);
        }
    }
    u32 GetDepth(Render::RDG::RenderPass *node, HashMap<Render::RDG::RenderPass *, u32> &depth_map, HashMap<Render::RDG::RenderPass *, Vector<Render::RDG::RenderPass *>> &deps)
    {
        if (depth_map.count(node)) return depth_map[node];
        u32 max_depth = 0;
        for (auto *input: deps[node])
        {
            max_depth = std::max(max_depth, GetDepth(input, depth_map,deps) + 1);
        }
        return depth_map[node] = max_depth;
    }

    void RenderGraphDebugger::ShowImpl()
    {
        static Render::RDG::RenderGraph *graph = nullptr;
        static bool s_is_first = true;
        static f32 s_depth_height = 200.0f;
        static f32 s_layer_width = 120.0f;
        if (ImGui::Button("Capture"))
        {
            graph = &Render::GraphicsContext::Get().GetPipeline()->GetRenderer()->GetRenderGraph();
            graph->_is_debug = true;
            s_is_first = true;
        }
        ImGui::SameLine();
        ImGui::DragFloat("Zoom", &s_zoom);
        ImGui::SliderFloat("DepthHeight", &s_depth_height,0.0f, 300.0f);
        ImGui::SliderFloat("LayerWidth", &s_layer_width, 0.0f, 200.0f);
        if (graph && graph->_debug_passes.size() > 0)
        {
            HashMap<String, int> output_slot_ids;
            HashMap<String, int> input_slot_ids;
            ImNodes::BeginNodeEditor();
            {
                HashMap<Render::RDG::RGHandle, Vector<Render::RDG::RenderPass *>> _res_producers;
                for (Render::RDG::RenderPass& pass: graph->_debug_passes)
                {
                    for (auto &output: pass._output_handles)
                    {
                        _res_producers[output].push_back(&pass);
                    }
                }
                HashMap<Render::RDG::RenderPass *, Vector<Render::RDG::RenderPass *>> pass_dependencies;// producer -> consumers
                for (auto& consumer: graph->_debug_passes)
                {
                    for (Render::RDG::RGHandle &input: consumer._input_handles)
                    {
                        auto it = _res_producers.find(input);
                        if (it != _res_producers.end())
                        {
                            for (Render::RDG::RenderPass* producer: it->second)
                            {
                                if (producer != &consumer)
                                {
                                    pass_dependencies[producer].push_back(&consumer);
                                }
                            }
                        }
                    }
                }
                HashMap<Render::RDG::RenderPass *, u32> depth_map;
                for (auto &consumer: graph->_debug_passes)
                {
                    depth_map[&consumer] = GetDepth(&consumer, depth_map, pass_dependencies);
                }

                static std::hash<String> hash_fn; 
                for (u64 i = 0; i < graph->_debug_passes.size(); i++)
                {
                    auto &pass = graph->_debug_passes[i];
                    ImVec2 pos = ImNodes::GetCanvasOrigin();
                    pos.x += i * s_layer_width;
                    pos.y -= s_depth_height * depth_map[&pass];
                    int node_id = static_cast<i32>(hash_fn(pass._name + "pass"));
                    u32 title_color = pass._type == Render::RDG::EPassType::kGraphics ? IM_COL32(0, 120, 0, 255) : IM_COL32(0, 0, 255, 255);
                    ImNodes::PushColorStyle(ImNodesCol_TitleBar, title_color);
                    ImNodes::BeginNode(node_id);
                    if (s_is_first)
                        ImNodes::SetNodeEditorSpacePos(node_id, pos);
                    ImNodes::BeginNodeTitleBar();
                    ImGui::TextUnformatted(pass._name.c_str());
                    ImNodes::EndNodeTitleBar();

                    ImGui::Dummy(ImVec2(80.0f, 45.0f));
                    // Input attribute
                    {
                        for (const auto &input: pass._input_handles)
                        {
                            String id = std::format("{}_input_{}", input._name, i);
                            ImNodes::BeginInputAttribute(static_cast<i32>(hash_fn(id)));
                            ImGui::Text("%s", std::format("{}({})", input._name,input._version).c_str());
                            //ImGui::Text("%d", static_cast<i32>(hash_fn(id)));
                            ImNodes::EndInputAttribute();
                            input_slot_ids[input.UniqueName() + pass._name] = static_cast<i32>(hash_fn(id));
                        }
                    }
                    // OUTPUT attribute
                    {
                        for (const auto &output: pass._output_handles)
                        {
                            String id = std::format("{}_oupput_{}", output._name, i);
                            ImNodes::BeginOutputAttribute(static_cast<i32>(hash_fn(id)));
                            ImGui::Text("%s", std::format("{}({})", output._name, output._version).c_str());
                            //ImGui::Text("%d", static_cast<i32>(hash_fn(id)));
                            ImNodes::EndOutputAttribute();
                            output_slot_ids[output.UniqueName()] = static_cast<i32>(hash_fn(id));
                        }
                    }
                    ImNodes::EndNode();
                    ImNodes::PopColorStyle();
                }
                int link_id = 0;
                for (u64 i = 0; i < graph->_debug_passes.size(); i++)
                {
                    const auto &pass = graph->_debug_passes[i];
                    for (const auto &input: pass._input_handles)
                    {
                        auto id = input.UniqueName();
                        if (output_slot_ids.contains(id))
                        {
                            ImNodes::Link(link_id++, input_slot_ids[id + pass._name], output_slot_ids[id]);
                        }
                    }
                }
            }
            ImNodes::MiniMap();
            //ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_TopRight, mini_map_node_hovering_callback);
            ImNodes::EndNodeEditor();
            s_is_first = false;
        }


    }
}

