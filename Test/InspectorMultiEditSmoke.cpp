#include "Inspector/TransformPropertyCommand.h"
#include "Scene/EntitySerializer.h"
#include "Assets/AssetDocument.h"
#include "Inspector/ComponentEditors/TransformComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Inspector/MultiComponentEditor.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Project/ProjectManager.h"
#include "Render/Material.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include <iostream>
#include <limits>

using namespace Ailu;
using namespace Ailu::Editor;

namespace Ailu::Editor { CommandManager *g_pCommandMgr = nullptr; }

static bool CheckPropertyPanel()
{
    ResourceMgr resource_manager;
    auto scene = MakeRef<SceneManagement::Scene>("InspectorPanelSmoke", false);
    const auto first = scene->AddObject("First");
    const auto second = scene->AddObject("Second");
    scene->GetRegister().GetComponent<ECS::TransformComponent>(first)->SetLocalPosition({1, 2, 3});
    scene->GetRegister().GetComponent<ECS::TransformComponent>(second)->SetLocalPosition({5, 2, 8});
    UI::VerticalBox panel;
    ComponentEditorContext context;
    context._scene = scene.get();
    context._entity = first;
    context._entities = {first, second};
    context._content = &panel;
    TransformComponentEditor editor;
    editor.Build(context);
    Vector<UI::InputBlock *> inputs;
    const auto collect = [&](auto &&self, UI::UIElement *element) -> void
    {
        if (auto *input = dynamic_cast<UI::InputBlock *>(element))
            inputs.push_back(input);
        for (const auto &child : element->GetChildren())
            self(self, child.get());
    };
    collect(collect, &panel);
    if (inputs.size() != 9)
        return false;
    const auto *property = UI::InputBlock::StaticType()->FindPropertyByName("_content");
    const auto text = [&](u32 index) { return property->Get<String>(inputs[index]); };
    if (text(0) != "--" || text(1) != "2.00" || text(2) != "--" || text(6) != "1.00")
        return false;
    TransformPropertyCommand change(scene, {first, second}, ETransformProperty::kPosition, 0);
    change.Preview(10);
    editor.Refresh(context);
    if (text(0) != "10.00" || text(2) != "--")
        return false;
    UI::UIEvent mouse_down{};
    mouse_down._type = UI::UIEvent::EType::kMouseDown;
    mouse_down._key_code = EKey::kRBUTTON;
    inputs[0]->OnEvent(mouse_down);
    inputs[0]->SetContent("unfinished input", false);
    editor.Refresh(context);
    if (text(0) != "unfinished input")
        return false;
    change.Undo();
    editor.Refresh(context);
    if (text(1) != "2.00" || text(2) != "--")
        return false;
    // Runtime SceneMgr copies have no shared ownership, but still use the same per-axis setters.
    ECS::TransformComponent runtime_component;
    runtime_component.SetLocalPosition({4, 5, 6});
    return TransformPropertyCommand::WriteValue(runtime_component, ETransformProperty::kPosition, 1, 20) &&
        runtime_component.GetLocalPosition() == Vector3f(4, 20, 6);
}

static bool CheckMultiEdit()
{
    ResourceMgr resource_manager;
    auto scene = MakeRef<SceneManagement::Scene>("InspectorMultiEditSmoke", false);
    const auto first = scene->AddObject("First");
    const auto second = scene->AddObject("Second");
    const auto get = [&](ECS::Entity entity)
        { return scene->GetRegister().GetComponent<ECS::TransformComponent>(entity); };
    get(first)->SetLocalPosition({1, 2, 3});
    get(second)->SetLocalPosition({5, 2, 8});
    const auto revision = scene->EditRevision();
    auto command = MakeScope<TransformPropertyCommand>(scene, Vector<ECS::Entity>{first, second},
        ETransformProperty::kPosition, 0);
    command->Preview(9);
    command->Preview(10);
    if (get(first)->GetLocalPosition() != Vector3f(10, 2, 3) ||
        get(second)->GetLocalPosition() != Vector3f(10, 2, 8) || scene->EditRevision() <= revision)
        return false;
    CommandManager manager;
    manager.ExecuteCommand(std::move(command));
    if (manager.UndoViews().size() != 1)
        return false;
    manager.Undo();
    if (get(first)->GetLocalPosition() != Vector3f(1, 2, 3) ||
        get(second)->GetLocalPosition() != Vector3f(5, 2, 8))
        return false;
    manager.Redo();
    if (get(first)->GetLocalPosition() != Vector3f(10, 2, 3) ||
        get(second)->GetLocalPosition() != Vector3f(10, 2, 8))
        return false;
    auto document = SceneManagement::EntitySerializer::BuildEntityDocument(*scene, second);
    JsonArchive writer;
    const String name = "entity";
    SerializerWrapper<SceneEntityDocument>::Serialize(&document, writer, &name);
    JsonArchive reader;
    if (!reader.LoadFromString(writer.SaveToString()))
        return false;
    SceneEntityDocument loaded;
    SerializerWrapper<SceneEntityDocument>::Deserialize(&loaded, reader, &name);
    if (loaded._transform_component._position != Vector3f(10, 2, 8))
        return false;
    TransformPropertyCommand single(scene, {first}, ETransformProperty::kScale, 1);
    single.Preview(4);
    if (get(first)->GetLocalScale() != Vector3f(1, 4, 1) || get(second)->GetLocalScale() != Vector3f(1, 1, 1))
        return false;
    single.Undo();
    if (get(first)->GetLocalScale() != Vector3f(1, 1, 1))
        return false;
    TransformPropertyCommand no_op(scene, {first}, ETransformProperty::kPosition, 0);
    no_op.Preview(std::numeric_limits<f32>::infinity());
    no_op.Preview(10);
    if (no_op.HasChanges())
        return false;
    get(first)->SetLocalRotation(Quaternion::EulerAngles({10, 20, 30}));
    get(second)->SetLocalRotation(Quaternion::EulerAngles({40, 50, 60}));
    const auto old_first = get(first)->_local_transform._rotation;
    const auto old_second = get(second)->_local_transform._rotation;
    TransformPropertyCommand rotation(scene, {first, second}, ETransformProperty::kRotation, 0);
    rotation.Preview(15);
    const auto first_euler = TransformPropertyCommand::ReadValue(*get(first), ETransformProperty::kRotation);
    const auto second_euler = TransformPropertyCommand::ReadValue(*get(second), ETransformProperty::kRotation);
    if (std::abs(first_euler.x - 15) > .001f || std::abs(first_euler.y - 20) > .001f ||
        std::abs(second_euler.x - 15) > .001f || std::abs(second_euler.z - 60) > .001f)
        return false;
    rotation.Undo();
    if (get(first)->_local_transform._rotation != old_first || get(second)->_local_transform._rotation != old_second)
        return false;
    scene->RemoveObject(second);
    const auto replacement = scene->AddObject("Replacement");
    get(replacement)->SetLocalPosition({99, 98, 97});
    manager.Undo();
    manager.Redo();
    if (get(replacement)->GetLocalPosition() != Vector3f(99, 98, 97))
        return false;
    CommandManager pending_manager;
    auto pending = MakeScope<TransformPropertyCommand>(scene, Vector<ECS::Entity>{first},
        ETransformProperty::kPosition, 0);
    pending->Preview(25);
    const auto subscription = pending_manager.BeforeHistoryChange().Subscribe([&]()
    {
        if (pending != nullptr)
            pending_manager.ExecuteCommand(std::move(pending));
    });
    pending_manager.Undo();
    pending_manager.BeforeHistoryChange().Unsubscribe(subscription);
    if (get(first)->GetLocalPosition().x != 10 || !pending_manager.UndoViews().empty())
        return false;
    pending_manager.Redo();
    if (get(first)->GetLocalPosition().x != 25)
        return false;
    scene->GetRegister().RemoveComponent<ECS::TransformComponent>(first);
    manager.Undo();
    manager.Redo();
    scene.reset();
    manager.Undo();
    manager.Redo();
    return true;
}

static void RegisterCoverageComponents()
{
    auto &registry = ComponentEditorRegistry::Get();
#define REGISTER(component) registry.Register<ECS::component>(#component)
    registry.RegisterCustom<ECS::TransformComponent, TransformComponentEditor>("Transform", "Core", 0,
        false, false, false);
    REGISTER(ScriptComponent);
    REGISTER(LightComponent);
    REGISTER(StaticMeshComponent);
    REGISTER(CLightProbe);
    REGISTER(CLightProbeGroup);
    REGISTER(DDGIVolumeComponent);
    REGISTER(SurfelGIComponent);
    REGISTER(EnvironmentComponent);
    REGISTER(CCamera);
    REGISTER(PostprocessVolumeComponent);
    REGISTER(SpriteRendererComponent);
    REGISTER(AnimatorComponent);
    REGISTER(CSkeletonMesh);
    REGISTER(CRigidBody);
    REGISTER(CCollider);
    REGISTER(RigidBody2DComponent);
    REGISTER(Collider2DComponent);
    REGISTER(LocalFogVolumeComponent);
    REGISTER(CVXGI);
    REGISTER(AudioSourceComponent);
    REGISTER(AudioListenerComponent);
    registry.Register<ECS::TagComponent>("Tag", "Core", 10, false, false, false);
    registry.Register<ECS::PersistentIdComponent>("Persistent ID", "Core", 20, false, false, false);
    registry.Register<ECS::CHierarchy>("Hierarchy", "Core", 30, false, false, false);
#undef REGISTER
}

static bool CheckComponentCoverage()
{
    ResourceMgr resource_manager;
    RegisterCoverageComponents();
    u32 checked_fields = 0;
    for (const auto &info : ComponentEditorRegistry::Get().Components())
    {
        if (info._component_type == ECS::CLightProbe::StaticComponentTypeId())
        {
            const auto editor = ComponentEditorRegistry::Get().CreateEditor(info, true);
            if (!editor || !editor->SupportsMultiEdit()) return false;
            std::cout << "PASS factory: CLightProbe (GPU allocation requires runtime validation)\n";
            continue;
        }
        auto scene = MakeRef<InspectorScene>("ComponentCoverage", false);
        const auto first = scene->AddObject("First");
        const auto second = scene->AddObject("Second");
        for (auto entity : {first, second})
            if (!info._has_component(scene->GetRegister(), entity))
                info._add_component(scene->GetRegister(), entity);
        UI::VerticalBox panel;
        ComponentEditorContext context;
        context._scene = scene.get();
        context._entity = first;
        context._entities = {first, second};
        context._component_info = &info;
        context._content = &panel;
        auto editor = ComponentEditorRegistry::Get().CreateEditor(info, true);
        if (!editor || !editor->SupportsMultiEdit())
            return false;
        editor->Build(context);
        const Vector<Guid> guids = {*scene->FindEntityGuid(first), *scene->FindEntityGuid(second)};
        for (const auto &field : BuildComponentInspectorFields(context))
        {
            const auto aggregate = ReadInspectorAggregate(field, *scene, guids);
            if (!aggregate._available || !aggregate._value || field._action || field._asset_type)
                continue;
            const auto original = *aggregate._value;
            const auto second_original = field._read(*scene, second);
            InspectorValue sample = original;
            if (auto *value = std::get_if<bool>(&sample)) *value = !*value;
            if (auto *value = std::get_if<double>(&sample)) *value += .125;
            if (auto *value = std::get_if<i64>(&sample))
            {
                if (field._choices.empty()) ++*value;
                else if (field._choice_values.empty()) *value = (*value + 1) % field._choices.size();
                else *value = field._choice_values.back();
            }
            if (auto *value = std::get_if<String>(&sample)) *value = "CoverageValue";
            if (auto *value = std::get_if<Vector4f>(&sample)) value->x += .125f;
            // Resource/relationship identity is tested separately; never load fabricated assets.
            if (std::holds_alternative<Guid>(sample))
                continue;
            InspectorPropertyCommand command(scene, guids, field);
            command.Preview(sample);
            const auto edited = field._read(*scene, first);
            if (field._read_only && edited != aggregate._value)
                return false;
            if (field._read(*scene, second) != edited)
                return false;
            command.Execute();
            command.Undo();
            if (field._read(*scene, first) != aggregate._value || field._read(*scene, second) != second_original)
            {
                std::cout << "FAIL restore: " << info._display_name << "/" << field._label << '\n';
                return false;
            }
            command.Execute();
            if (field._read(*scene, first) != edited || field._read(*scene, second) != edited)
                return false;
            command.Undo();
            ++checked_fields;
        }
        std::cout << "PASS component: " << info._display_name << '\n';
    }
    std::cout << "PASS coverage: 25 registered components, " << checked_fields << " field undo/redo checks\n";
    return ComponentEditorRegistry::Get().Components().size() == 25 && checked_fields > 150;
}

static bool CheckDynamicAndCoupledFields()
{
    ResourceMgr resource_manager;
    auto scene = MakeRef<InspectorScene>("DynamicFields", false);
    const auto first = scene->AddObject("First");
    const auto second = scene->AddObject("Second");
    auto &registry = scene->GetRegister();
    const Vector<Guid> guids = {*scene->FindEntityGuid(first), *scene->FindEntityGuid(second)};
    ComponentEditorContext context;
    context._scene = scene.get();
    context._entity = first;
    context._entities = {first, second};
    const auto fields_for = [&](ECS::ComponentTypeId type)
    {
        context._component_info = ComponentEditorRegistry::Get().Find(type);
        for (auto entity : {first, second})
            if (!context._component_info->_has_component(registry, entity))
                context._component_info->_add_component(registry, entity);
        return BuildComponentInspectorFields(context);
    };
    const auto find = [](const Vector<InspectorField> &fields, const String &key) -> InspectorField
    {
        for (const auto &field : fields)
            if (field._key == key) return field;
        return {};
    };
    auto fields = fields_for(ECS::AnimatorComponent::StaticComponentTypeId());
    const Guid first_controller = Guid::Generate(), second_controller = Guid::Generate(), clip = Guid::Generate();
    registry.GetComponent<ECS::AnimatorComponent>(first)->_controller = first_controller;
    registry.GetComponent<ECS::AnimatorComponent>(second)->_controller = second_controller;
    InspectorPropertyCommand animator(scene, guids, find(fields, "_clip"));
    if (!animator.Preview(clip)) return false;
    animator.Execute();
    for (auto entity : {first, second})
        if (!registry.GetComponent<ECS::AnimatorComponent>(entity)->_controller.IsEmpty()) return false;
    animator.Undo();
    if (registry.GetComponent<ECS::AnimatorComponent>(first)->_controller != first_controller ||
        registry.GetComponent<ECS::AnimatorComponent>(second)->_controller != second_controller) return false;

    fields = fields_for(ECS::Collider2DComponent::StaticComponentTypeId());
    InspectorPropertyCommand add(scene, guids, find(fields, "add_shape"));
    add.Preview(true);
    add.Execute();
    fields = BuildComponentInspectorFields(context);
    const auto shape_key = fields.back()._key;
    InspectorPropertyCommand remove(scene, guids, find(fields, shape_key));
    remove.Preview(true);
    remove.Execute();
    if (!registry.GetComponent<ECS::Collider2DComponent>(first)->_shapes.empty()) return false;
    remove.Undo();
    if (registry.GetComponent<ECS::Collider2DComponent>(second)->_shapes.size() != 1) return false;
    remove.Execute();
    remove.Undo();
    add.Undo();
    if (!registry.GetComponent<ECS::Collider2DComponent>(second)->_shapes.empty()) return false;
    auto *collider = registry.GetComponent<ECS::Collider2DComponent>(first);
    collider->_preset = ECS::ECollisionPreset2D::kPlayer;
    ECS::ApplyCollisionPreset2D(collider->_collision_profile, collider->_preset);
    const auto previous_channel = collider->_collision_profile._object_type;
    InspectorPropertyCommand preset(scene, guids, find(fields, "object_type"));
    preset.Preview(i64(ECS::ECollisionChannel2D::kWorldStatic));
    preset.Execute();
    if (collider->_preset != ECS::ECollisionPreset2D::kCustom) return false;
    preset.Undo();
    if (collider->_preset != ECS::ECollisionPreset2D::kPlayer ||
        collider->_collision_profile._object_type != previous_channel) return false;

    fields = fields_for(ECS::StaticMeshComponent::StaticComponentTypeId());
    registry.GetComponent<ECS::StaticMeshComponent>(second)->_material_guids.resize(2);
    InspectorPropertyCommand add_slot(scene, guids, find(fields, "add_material"));
    add_slot.Preview(true);
    add_slot.Execute();
    if (registry.GetComponent<ECS::StaticMeshComponent>(first)->_p_mats.size() != 1 ||
        registry.GetComponent<ECS::StaticMeshComponent>(second)->_p_mats.size() != 3) return false;
    add_slot.Undo();
    if (!registry.GetComponent<ECS::StaticMeshComponent>(second)->_p_mats.empty() ||
        registry.GetComponent<ECS::StaticMeshComponent>(second)->_material_guids.size() != 2) return false;

    auto material = MakeRef<Render::Material>();
    material->SetCullMode(Render::ECullMode::kBack);
    for (auto entity : {first, second})
        registry.GetComponent<ECS::StaticMeshComponent>(entity)->_p_mats = {material};
    fields = BuildComponentInspectorFields(context);
    InspectorPropertyCommand shared_material(scene, guids, find(fields, "material:0:cull"));
    if (!shared_material.Preview(i64(Render::ECullMode::kFront))) return false;
    shared_material.Execute();
    shared_material.Undo();
    if (material->GetCullMode() != Render::ECullMode::kBack) return false;
    shared_material.Execute();
    if (material->GetCullMode() != Render::ECullMode::kFront) return false;
    auto replacement_material = MakeRef<Render::Material>();
    replacement_material->SetCullMode(Render::ECullMode::kNone);
    registry.GetComponent<ECS::StaticMeshComponent>(first)->_p_mats = {replacement_material};
    shared_material.Undo();
    if (material->GetCullMode() != Render::ECullMode::kBack ||
        replacement_material->GetCullMode() != Render::ECullMode::kNone) return false;

    fields = fields_for(ECS::ScriptComponent::StaticComponentTypeId());
    ECS::ScriptPropertyData script_property;
    script_property._name = "speed";
    script_property._type = ECS::EScriptPropertyType::kFloat;
    script_property._float_value = 1;
    registry.GetComponent<ECS::ScriptComponent>(first)->_properties = {script_property};
    script_property._float_value = 3;
    registry.GetComponent<ECS::ScriptComponent>(second)->_properties = {script_property};
    fields = BuildComponentInspectorFields(context);
    const auto speed = fields.back();
    if (!ReadInspectorAggregate(speed, *scene, guids)._mixed) return false;
    InspectorPropertyCommand script(scene, guids, speed);
    script.Preview(double(7));
    script.Execute();
    script.Undo();
    if (registry.GetComponent<ECS::ScriptComponent>(first)->_properties[0]._float_value != 1 ||
        registry.GetComponent<ECS::ScriptComponent>(second)->_properties[0]._float_value != 3) return false;
    registry.GetComponent<ECS::ScriptComponent>(second)->_properties[0]._type = ECS::EScriptPropertyType::kInt;
    if (ReadInspectorAggregate(speed, *scene, guids)._available) return false;
    registry.GetComponent<ECS::ScriptComponent>(second)->_properties.clear();
    script.Execute(); // Stale dynamic field safely skips the missing property.
    if (!registry.GetComponent<ECS::ScriptComponent>(second)->_properties.empty()) return false;

    fields = fields_for(ECS::CCamera::StaticComponentTypeId());
    registry.GetComponent<ECS::CCamera>(first)->_camera.Type(Render::ECameraType::kPerspective);
    registry.GetComponent<ECS::CCamera>(second)->_camera.Type(Render::ECameraType::kOrthographic);
    if (ReadInspectorAggregate(find(fields, "fov"), *scene, guids)._available ||
        ReadInspectorAggregate(find(fields, "size"), *scene, guids)._available) return false;
    fields = fields_for(ECS::CRigidBody::StaticComponentTypeId());
    registry.GetComponent<ECS::CRigidBody>(first)->_velocity = {1, 2, 3};
    registry.GetComponent<ECS::CRigidBody>(second)->_velocity = {4, 5, 6};
    InspectorPropertyCommand axis(scene, guids, find(fields, "_velocity[0]"));
    axis.Preview(double(9));
    axis.Execute();
    if (registry.GetComponent<ECS::CRigidBody>(second)->_velocity != Vector3f(9, 5, 6)) return false;
    scene->RemoveObject(second);
    const auto replacement = scene->AddObject("Replacement");
    registry.AddComponent<ECS::CRigidBody>(replacement);
    registry.GetComponent<ECS::CRigidBody>(replacement)->_velocity = {99, 98, 97};
    axis.Undo();
    axis.Execute();
    if (registry.GetComponent<ECS::CRigidBody>(replacement)->_velocity != Vector3f(99, 98, 97)) return false;
    scene.reset();
    axis.Undo();
    axis.Execute();
    std::cout << "PASS dynamic: animator coupled state, shape arrays, collision profile, material slots, "
        "script schema intersection, camera subtypes, vector axes and GUID lifetime\n";
    return true;
}

static bool CheckGenericPanel()
{
    ResourceMgr resource_manager;
    auto scene = MakeRef<InspectorScene>("GenericPanel", false);
    const auto first = scene->AddObject("First");
    const auto second = scene->AddObject("Second");
    auto &registry = scene->GetRegister();
    registry.AddComponent<ECS::AudioSourceComponent>(first);
    registry.AddComponent<ECS::AudioSourceComponent>(second);
    registry.GetComponent<ECS::AudioSourceComponent>(first)->_volume = .2f;
    registry.GetComponent<ECS::AudioSourceComponent>(second)->_volume = .7f;
    registry.SetComponentEnabled<ECS::AudioSourceComponent>(second, false);
    UI::VerticalBox panel;
    CommandManager manager;
    g_pCommandMgr = &manager;
    bool current = true;
    bool passed = false;
    {
        MultiComponentEditor editor;
        ComponentEditorContext context;
        context._scene = scene.get();
        context._entity = first;
        context._entities = {first, second};
        context._component_info = ComponentEditorRegistry::Get().Find(
            ECS::AudioSourceComponent::StaticComponentTypeId());
        context._content = &panel;
        context._is_current = [&] { return current; };
        editor.Build(context);
        UI::InputBlock *volume = nullptr;
        UI::CheckBox *enabled = nullptr;
        const auto collect = [&](auto &&self, UI::UIElement *element, String key) -> void
        {
            if (element->Name() == "_volume" || element->Name() == "component_enabled") key = element->Name();
            if (key == "_volume")
                if (auto *input = dynamic_cast<UI::InputBlock *>(element)) volume = input;
            if (key == "component_enabled")
                if (auto *checkbox = dynamic_cast<UI::CheckBox *>(element)) enabled = checkbox;
            for (const auto &child : element->GetChildren()) self(self, child.get(), key);
        };
        collect(collect, &panel, "");
        const auto *content = UI::InputBlock::StaticType()->FindPropertyByName("_content");
        if (volume && enabled && enabled->IsMixed() && content->Get<String>(volume) == "--")
        {
            volume->SetContent(".5");
            volume->SetContent(".6");
            manager.Undo(); // Finishes the pending field before undoing it.
            passed = registry.GetComponent<ECS::AudioSourceComponent>(first)->_volume == .2f &&
                registry.GetComponent<ECS::AudioSourceComponent>(second)->_volume == .7f;
            manager.Redo();
            passed &= registry.GetComponent<ECS::AudioSourceComponent>(first)->_volume == .6f &&
                registry.GetComponent<ECS::AudioSourceComponent>(second)->_volume == .6f;
            enabled->SetChecked(true);
            passed &= registry.IsComponentEnabled<ECS::AudioSourceComponent>(second);
            manager.Undo();
            passed &= registry.IsComponentEnabled<ECS::AudioSourceComponent>(first) &&
                !registry.IsComponentEnabled<ECS::AudioSourceComponent>(second);
            current = false;
            volume->SetContent(".9");
            passed &= registry.GetComponent<ECS::AudioSourceComponent>(first)->_volume == .6f;
        }
    }
    g_pCommandMgr = nullptr;
    return passed;
}

int main()
{
    std::cout << std::unitbuf;
    LogMgr::Init();
    ProjectManager::Init();
    Enum::InitTypeInfo();
    const bool passed = CheckMultiEdit() && CheckPropertyPanel() && CheckComponentCoverage() &&
        CheckDynamicAndCoupledFields() && CheckGenericPanel();
    std::cout << (passed ? "PASS" : "FAIL")
        << ": Inspector mixed-value UI, multi-edit, undo/redo, serialization and lifetime\n";
    ProjectManager::Shutdown();
    LogMgr::Shutdown();
    return passed ? 0 : 1;
}
