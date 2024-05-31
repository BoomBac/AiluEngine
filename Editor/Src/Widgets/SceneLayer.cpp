#include "Widgets/SceneLayer.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/Gizmo.h"
#include "Framework/Common/SceneMgr.h"
#include "Framework/Common/Input.h"

namespace Ailu
{
	namespace Editor
	{
		SceneLayer::SceneLayer()
		{
		}
		SceneLayer::SceneLayer(const String& name)
		{
		}
		SceneLayer::~SceneLayer()
		{
		}
		void SceneLayer::OnEvent(Event& e)
		{
			if (e.GetEventType() == EEventType::kMouseButtonPressed)
			{
				//MouseButtonPressedEvent& event = static_cast<MouseButtonPressedEvent&>(e);
				//static std::chrono::seconds duration(5);
				//if (event.GetButton() == 1)
				//{			
				//	auto inv_vp = Camera::sCurrent->GetView() * Camera::sCurrent->GetProjection();
				//	MatrixInverse(inv_vp);
				//	auto p = Camera::sCurrent->GetProjection();
				//	auto m_pos = Input::GetMousePos();
				//	float vx = (2.0f * m_pos.x / 1600 - 1.0f) / p[0][0];
				//	float vy = (-2.0f * m_pos.y / 900 + 1.0f) / p[1][1];
				//	auto view = Camera::sCurrent->GetView();
				//	MatrixInverse(view);
				//	Vector3f ray_dir = Vector3f(vx, vy, 1.0f);
				//	LOG_INFO("mouse pos {},{}", vx, vy);
				//	Vector3f start = Camera::sCurrent->Position();
				//	Vector3f end = start + TransformNormal(ray_dir, view) * 10000.0f;
				//	Gizmo::DrawLine(start, end, duration, Colors::kRed);
				//	for (auto& actor : g_pSceneMgr->_p_current->GetAllActor())
				//	{
				//		auto comp = actor->GetComponent<StaticMeshComponent>();
				//		if (comp)
				//		{
				//			auto [aabb_min,aabb_max] = comp->GetAABB();
				//			float dis = AABB::DistanceFromRayToAABB(start,Camera::sCurrent->Forward(), aabb_min, aabb_max);
				//			LOG_INFO("object {},distance {}",actor->Name(),dis);
				//		}
				//	}
				//}
			}

		}
		//void SceneLayer::OnUpdate(float delta_time)
		//{
		//}
	}
}
