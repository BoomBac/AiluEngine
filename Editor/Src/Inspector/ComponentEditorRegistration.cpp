#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditors/TransformComponentEditor.h"
#include "Inspector/ComponentEditors/LightComponentEditor.h"
#include "Inspector/ComponentEditors/ScriptComponentEditor.h"
#include "Inspector/ComponentEditors/StaticMeshComponentEditor.h"
#include "Inspector/ComponentEditors/CameraComponentEditor.h"
#include "Inspector/ComponentEditors/SpriteRendererComponentEditor.h"
#include "Inspector/ComponentEditors/LightProbeComponentEditor.h"
#include "Inspector/ComponentEditors/RigidBodyComponentEditor.h"
#include "Inspector/ComponentEditors/ColliderComponentEditor.h"
#include "Scene/Component.h"

namespace Ailu
{
    namespace Editor
    {
        void RegisterComponentEditors()
        {
            auto &registry = ComponentEditorRegistry::Get();

            registry.RegisterCustom<ECS::TransformComponent, TransformComponentEditor>("Transform", "Core", 0, false, false);

            registry.RegisterCustom<ECS::ScriptComponent, ScriptComponentEditor>("Script", "Scripting", 100);

            registry.RegisterCustom<ECS::LightComponent, LightComponentEditor>("Light", "Rendering", 200);

            registry.RegisterCustom<ECS::StaticMeshComponent, StaticMeshComponentEditor>("Static Mesh", "Rendering", 210);

            registry.RegisterCustom<ECS::CLightProbe, LightProbeComponentEditor>("Light Probe", "Rendering", 220);

            registry.RegisterCustom<ECS::CCamera, CameraComponentEditor>("Camera", "Rendering", 230);

            registry.RegisterCustom<ECS::SpriteRendererComponent, SpriteRendererComponentEditor>("Sprite Renderer", "Rendering", 240);

            registry.Register<ECS::CSkeletonMesh>("Skeleton Mesh", "Rendering", 250);

            registry.RegisterCustom<ECS::CRigidBody, RigidBodyComponentEditor>("Rigid Body", "Physics", 300);

            registry.RegisterCustom<ECS::CCollider, ColliderComponentEditor>("Collider", "Physics", 310);

            registry.Register<ECS::CVXGI>("VXGI", "Rendering", 260);

            registry.Register<ECS::AudioSourceComponent>("Audio Source", "Audio", 400);

            registry.Register<ECS::AudioListenerComponent>("Audio Listener", "Audio", 410);

            registry.Register<ECS::TagComponent>("Tag", "Core", 10, false, false);

            registry.Register<ECS::PersistentIdComponent>("Persistent ID", "Core", 20, false, false);

            registry.Register<ECS::CHierarchy>("Hierarchy", "Core", 30, false, false);
        }
    }// namespace Editor
}// namespace Ailu
