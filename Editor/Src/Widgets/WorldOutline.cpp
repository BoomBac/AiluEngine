#include "Widgets/WorldOutline.h"
#include "Widgets/Common.h"
#include "Inc/Framework/Common/SceneMgr.h"
#include "Framework/ImGui/ImGUIDLL.h"


WorldOutline::WorldOutline()
{
}

WorldOutline::WorldOutline(const String& name)
{
}

WorldOutline::~WorldOutline()
{
}

void WorldOutline::OnAttach()
{
}

void WorldOutline::OnDetach()
{
}

void WorldOutline::OnEvent(Ailu::Event& e)
{
}

using Ailu::SceneActor;
static SceneActor* s_cur_selected_actor = nullptr;
static int cur_tree_node_index;
static int s_selected_mask = 0;
static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
static int s_node_clicked = -1;
static u32 s_pre_frame_selected_actor_id = 10;
static u32 s_cur_frame_selected_actor_id = 0;

void WorldOutline::OnImguiRender()
{
#define DEAR_IMGUI
#ifdef DEAR_IMGUI

	ImGuiBegin("Editor: WorldOutline");
	//ImGui::Begin("WorldOutline");                          // Create a window called "Hello, world!" and append into it.
	u32 index = 0;

	//DrawTreeNode(Ailu::g_pSceneMgr->_p_current->GetSceneRoot());
	s_cur_frame_selected_actor_id = s_node_clicked;
	if (s_pre_frame_selected_actor_id != s_cur_frame_selected_actor_id)
	{
		s_cur_selected_actor = s_cur_frame_selected_actor_id == 0 ? nullptr : Ailu::g_pSceneMgr->_p_current->GetSceneActorByIndex(s_cur_frame_selected_actor_id);
	}
	s_pre_frame_selected_actor_id = s_cur_frame_selected_actor_id;

	cur_tree_node_index = 0;
	ImGuiEnd();
	//ImGui::End();
#endif // DEAR_IMGUI


}
