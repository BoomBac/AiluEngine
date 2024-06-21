#include "Widgets/ObjectDetail.h"
#include "Widgets/CommonTextureWidget.h"
#include "Common/Selection.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"

#include "Objects/TransformComponent.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/CameraComponent.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/ResourceMgr.h"


namespace Ailu
{
	namespace Editor
	{
		static u16 s_mini_tex_size = 64;

		namespace TreeStats
		{
			static i16 s_cur_opened_texture_selector = -1;
			static bool s_is_texture_selector_open = true;
		}

		static void DrawProperty(u32& property_handle, SerializableProperty& prop, Object* obj, TextureSelector& selector_window)
		{
			++property_handle;
			switch (prop._type)
			{
			case Ailu::ESerializablePropertyType::kString:
				break;
			case Ailu::ESerializablePropertyType::kFloat: ImGui::DragFloat(prop._name.c_str(), static_cast<float*>(prop._value_ptr));
				break;
			case Ailu::ESerializablePropertyType::kBool:
			{
				auto prop_value = SerializableProperty::GetProppertyValue<float>(prop);
				if (prop_value.has_value())
				{
					bool checked = prop_value.value() == 1.0f;
					ImGui::Checkbox(prop._name.c_str(), &checked);
					prop.SetProppertyValue(checked ? 1.0f : 0.0f);
				}
				else
					ImGui::Text("Toogle %s value missing", prop._name.c_str());
			}
			break;
			case Ailu::ESerializablePropertyType::kEnum:
			{
				auto mat = dynamic_cast<Material*>(obj);
				if (mat != nullptr)
				{
					//auto keywords = mat->GetShader()->GetKeywordGroups();
					//auto it = keywords.find(prop._value_name);
					//if (it != keywords.end())
					//{
					//	auto prop_value = static_cast<int>(prop.GetProppertyValue<float>().value_or(0.0));
					//	if (ImGui::BeginCombo(prop._name.c_str(), it->second[prop_value].substr(it->second[prop_value].rfind("_") + 1).c_str()))
					//	{
					//		static int s_selected_index = -1;
					//		for (int i = 0; i < it->second.size(); i++)
					//		{
					//			auto x = it->second[i].rfind("_");
					//			auto enum_str = it->second[i].substr(x + 1);
					//			if (ImGui::Selectable(enum_str.c_str(), i == s_selected_index))
					//				s_selected_index = i;
					//			if (s_selected_index == i)
					//			{
					//				mat->EnableKeyword(it->second[i]);
					//				prop.SetProppertyValue(static_cast<float>(i));
					//			}
					//		}
					//		ImGui::EndCombo();
					//	}
					//}
					//else
					//	ImGui::Text("Eunm %s value missing", prop._name.c_str());
				}
				else
					ImGui::Text("Non-Material Eunm %s value missing", prop._name.c_str());
			}
			break;
			case Ailu::ESerializablePropertyType::kVector3f: ImGui::DragFloat3(prop._name.c_str(), static_cast<Vector3f*>(prop._value_ptr)->data);
				break;
			case Ailu::ESerializablePropertyType::kVector4f:
				break;
			case Ailu::ESerializablePropertyType::kColor: ImGui::ColorEdit3(prop._name.c_str(), static_cast<Vector4f*>(prop._value_ptr)->data);
				break;
			case Ailu::ESerializablePropertyType::kTransform:
				break;
			case Ailu::ESerializablePropertyType::kTexture2D:
			{
				ImGui::Text("Texture2D : %s", prop._name.c_str());
				auto tex_prop_value = SerializableProperty::GetProppertyValue<std::tuple<u8, Texture*>>(prop);
				auto tex = tex_prop_value.has_value() ? std::get<1>(tex_prop_value.value()) : nullptr;
				u32 cur_tex_id = tex ? tex->ID() : Texture::s_p_default_white->ID();
				//auto& desc = std::static_pointer_cast<D3DTexture2D>(tex == nullptr ? TexturePool::GetDefaultWhite() : tex)->GetGPUHandle();
				float contentWidth = ImGui::GetWindowContentRegionWidth();
				float centerX = (contentWidth - s_mini_tex_size) * 0.5f;
				ImGui::SetCursorPosX(centerX);
				if (selector_window.IsCaller(property_handle))
				{
					u32 new_tex_id = selector_window.GetSelectedTexture(property_handle);
					if (TextureSelector::IsValidTextureID(new_tex_id) && new_tex_id != cur_tex_id)
					{
						auto mat = dynamic_cast<Material*>(obj);
						if (mat)
							mat->SetTexture(prop._value_name, g_pResourceMgr->Get<Texture2D>(new_tex_id));
					}
				}
				if (ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(g_pResourceMgr->Get<Texture2D>(cur_tex_id)->GetNativeTextureHandle()), ImVec2(s_mini_tex_size, s_mini_tex_size)))
				{
					selector_window.Open(property_handle);
				}
				if (selector_window.IsCaller(property_handle))
				{
					ImGuiContext* context = ImGui::GetCurrentContext();
					auto drawList = context->CurrentWindow->DrawList;
					ImVec2 cur_img_pos = ImGui::GetCursorPos();
					ImVec2 imgMin = ImGui::GetItemRectMin();
					ImVec2 imgMax = ImGui::GetItemRectMax();
					drawList->AddRect(imgMin, imgMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
				}
			}
			break;
			case Ailu::ESerializablePropertyType::kStaticMesh:
				break;
			case Ailu::ESerializablePropertyType::kRange:
			{
				ImGui::SliderFloat(prop._name.c_str(), static_cast<float*>(prop._value_ptr), prop._param[0], prop._param[1]);
			}
			break;
			default: LOG_WARNING("prop: {} value type havn't be handle!", prop._name)
				break;
			}
		}

		static void DrawInternalStandardMaterial(Material* mat, TextureSelector& selector_window)
		{
			u16 mat_prop_index = 0;
			static bool use_textures[10]{};
			static auto func_show_prop = [&](u16& mat_prop_index, Material* mat, String name) {
				ImGui::PushID(mat_prop_index);
				ETextureUsage tex_usage{};
				String non_tex_prop_name;
				if (name == InternalStandardMaterialTexture::kAlbedo)
				{
					tex_usage = ETextureUsage::kAlbedo;
					non_tex_prop_name = "BaseColor";
				}
				else if (name == InternalStandardMaterialTexture::kEmssive)
				{
					tex_usage = ETextureUsage::kEmssive;
					non_tex_prop_name = "EmssiveColor";
				}
				else if (name == InternalStandardMaterialTexture::kMetallic)
				{
					tex_usage = ETextureUsage::kMetallic;
					non_tex_prop_name = "MetallicValue";
				}
				else if (name == InternalStandardMaterialTexture::kRoughness)
				{
					tex_usage = ETextureUsage::kRoughness;
					non_tex_prop_name = "RoughnessValue";
				}
				else if (name == InternalStandardMaterialTexture::kSpecular)
				{
					tex_usage = ETextureUsage::kSpecular;
					non_tex_prop_name = "SpecularColor";
				}
				else if (name == InternalStandardMaterialTexture::kNormal)
				{
					tex_usage = ETextureUsage::kNormal;
					non_tex_prop_name = "None";
				}
				else
				{
					tex_usage = ETextureUsage::kAlbedo;
					non_tex_prop_name = "BaseColor";
				};
				use_textures[mat_prop_index] = mat->IsTextureUsed(tex_usage);
				ImGui::Checkbox(name.c_str(), &use_textures[mat_prop_index]);
				mat->MarkTextureUsed({ tex_usage }, use_textures[mat_prop_index]);
				ImGui::PopID();
				ImGui::SameLine();
				auto prop = mat->GetProperty(name);
				if (!prop._name.empty())
				{
					if (use_textures[mat_prop_index])
					{
						auto prop = mat->GetProperty(name);
						ImGui::Text(" :%s", prop._name.c_str());
						auto tex_prop_value = SerializableProperty::GetProppertyValue<std::tuple<u8, Texture*>>(prop);
						auto tex = tex_prop_value.has_value() ? std::get<1>(tex_prop_value.value()) : nullptr;
						if (tex == nullptr)
						{
							tex = mat_prop_index == 1 ? Texture::s_p_default_normal : Texture::s_p_default_white;
							mat->SetTexture(name, tex);
						}
						u32 cur_tex_id = tex->ID();
						float contentWidth = ImGui::GetWindowContentRegionWidth();
						float centerX = (contentWidth - s_mini_tex_size) * 0.5f;
						ImGui::SetCursorPosX(centerX);
						if (selector_window.IsCaller(mat_prop_index))
						{
							u32 new_tex_id = selector_window.GetSelectedTexture(mat_prop_index);
							if (TextureSelector::IsValidTextureID(new_tex_id) && new_tex_id != cur_tex_id)
							{
								auto new_tex = g_pResourceMgr->Get<Texture2D>(new_tex_id);
								mat->SetTexture(name, new_tex);
							}
						}

						if (ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(g_pResourceMgr->Get<Texture2D>(cur_tex_id)->GetNativeTextureHandle()), ImVec2(s_mini_tex_size, s_mini_tex_size)))
						{
							selector_window.Open(mat_prop_index);
						}
						if (selector_window.IsCaller(mat_prop_index))
						{
							ImGuiContext* context = ImGui::GetCurrentContext();
							auto drawList = context->CurrentWindow->DrawList;
							ImVec2 cur_img_pos = ImGui::GetCursorPos();
							ImVec2 imgMin = ImGui::GetItemRectMin();
							ImVec2 imgMax = ImGui::GetItemRectMax();
							drawList->AddRect(imgMin, imgMax, IM_COL32(255, 255, 0, 255), 0.0f, 0, 2.0f);
						}
					}
					else
					{
						selector_window.Close(mat_prop_index);
						if (tex_usage == ETextureUsage::kAlbedo || tex_usage == ETextureUsage::kEmssive || tex_usage == ETextureUsage::kSpecular)
						{
							auto prop = mat->GetProperty(non_tex_prop_name);
							ImGui::ColorEdit4(std::format("##{}", name).c_str(), static_cast<float*>(prop._value_ptr));
						}
						else if (tex_usage == ETextureUsage::kMetallic || tex_usage == ETextureUsage::kRoughness)
						{
							auto prop = mat->GetProperty(non_tex_prop_name);
							ImGui::SliderFloat(std::format("##{}", name).c_str(), static_cast<float*>(prop._value_ptr), prop._param[0], prop._param[1]);
						}
						else
						{
							ImGui::NewLine();
						}
						mat->RemoveTexture(name);
					}
				}
				else
					LOG_WARNING("Internal shader property {} missing", name);
				++mat_prop_index;
				};
			if (!TreeStats::s_is_texture_selector_open)
				TreeStats::s_cur_opened_texture_selector = -1;
			auto mat_type = mat->MaterialID();
			u32 mat_id = mat_type;
			ImGui::Text("Materia Type: %s", EMaterialID::ToString(mat_type));
			if (ImGui::BeginCombo("##MaterialID", EMaterialID::ToString(mat_type)))
			{
				for (u32 i = 0; i < EMaterialID::COUNT; i++)
				{
					const bool is_selected = (mat_id == i);
					if (ImGui::Selectable(EMaterialID::ToString((EMaterialID::EMaterialID)i), is_selected))
						mat_id = i;
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			mat->MaterialID((EMaterialID::EMaterialID)mat_id);
			auto surface_type = mat->SurfaceType();
			u16 surface_type_value = surface_type;
			ImGui::Text("Surface Type: %s", ESurfaceType::ToString(surface_type));
			if (ImGui::BeginCombo("##SurfaceType", ESurfaceType::ToString(surface_type)))
			{
				for (u32 i = 0; i < ESurfaceType::COUNT; i++)
				{
					const bool is_selected = (surface_type_value == i);
					if (ImGui::Selectable(ESurfaceType::ToString((ESurfaceType::ESurfaceType)i), is_selected))
						surface_type_value = i;
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			mat->SurfaceType((ESurfaceType::ESurfaceType)surface_type_value);
			func_show_prop(mat_prop_index, mat, "Albedo");
			func_show_prop(mat_prop_index, mat, "Normal");
			func_show_prop(mat_prop_index, mat, "Specular");
			func_show_prop(mat_prop_index, mat, "Roughness");
			func_show_prop(mat_prop_index, mat, "Metallic");
			func_show_prop(mat_prop_index, mat, "Emssive");
		}

		static void DrawComponentProperty(u32& property_handle, Component* comp, TextureSelector& selector_window)
		{
			if (ImGui::CollapsingHeader(Component::GetTypeName(comp->GetType()).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (comp->GetType() == LightComponent::GetStaticType())
				{
					auto light = static_cast<LightComponent*>(comp);
					static const char* items[] = { "Directional", "Point", "Spot" };
					int item_current_idx = 0;
					switch (light->_light_type)
					{
					case Ailu::ELightType::kDirectional: item_current_idx = 0;
						break;
					case Ailu::ELightType::kPoint: item_current_idx = 1;
						break;
					case Ailu::ELightType::kSpot: item_current_idx = 2;
						break;
					}
					const char* combo_preview_value = items[item_current_idx];
					if (ImGui::BeginCombo("LightType", combo_preview_value, 0))
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
					ImGui::SliderFloat("Intensity", &light->_intensity, 0.0f, 16.0f);
					ImGui::ColorEdit3("Color", static_cast<float*>(light->_light._light_color.data));
					if (item_current_idx == 0) light->_light_type = ELightType::kDirectional;
					else if (item_current_idx == 1) light->_light_type = ELightType::kPoint;
					else light->_light_type = ELightType::kSpot;

					if (light->_light_type == ELightType::kPoint)
					{
						auto& light_data = light->_light;
						ImGui::DragFloat("Radius", &light_data._light_param.x, 1.0, 0.0, 5000.0f);
					}
					else if (light->_light_type == ELightType::kSpot)
					{
						auto& light_data = light->_light;
						ImGui::DragFloat("Radius", &light_data._light_param.x);
						ImGui::SliderFloat("InnerAngle", &light_data._light_param.y, 0.0f, light_data._light_param.z);
						ImGui::SliderFloat("OuterAngle", &light_data._light_param.z, 1.0f, 180.0f);
					}
					bool b_cast_shadow = light->CastShadow();
					ImGui::Checkbox("CastShadow", &b_cast_shadow);
					if (b_cast_shadow != light->CastShadow())
						light->CastShadow(b_cast_shadow);
					if (b_cast_shadow)
					{
						auto& shadow_data = light->_shadow;
						ImGui::SliderFloat("ShadowArea", &shadow_data._size, 100, 10000);
						ImGui::SliderFloat("ShadowDistance", &shadow_data._distance, 100, 50000);
					}
				}
				else if (comp->GetType() == TransformComponent::GetStaticType())
				{
					auto transf = static_cast<TransformComponent*>(comp);
					auto& pos = transf->GetProperty("Position");
					ImGui::DragFloat3(pos._name.c_str(), static_cast<Vector3f*>(pos._value_ptr)->data);
					auto& rot = transf->GetProperty("Rotation");
					ImGui::SliderFloat3(rot._name.c_str(), static_cast<Vector3f*>(rot._value_ptr)->data, -180.f, 180.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					auto& scale = transf->GetProperty("Scale");
					ImGui::DragFloat3(scale._name.c_str(), static_cast<Vector3f*>(scale._value_ptr)->data);
				}
				else if (comp->GetType() == StaticMeshComponent::GetStaticType() || comp->GetType() == SkinedMeshComponent::GetStaticType())
				{
					auto static_mesh_comp = static_cast<StaticMeshComponent*>(comp);
					bool is_skined_comp = comp->GetType() == SkinedMeshComponent::GetStaticType();
					for (auto& prop : static_mesh_comp->GetAllProperties())
					{
						if (prop.second._type == ESerializablePropertyType::kStaticMesh)
						{
							ImGui::Text(is_skined_comp ? "SkinedMesh" : "StaticMesh");
							auto& mesh = static_mesh_comp->GetMesh();
							ImGui::Text("    submesh: %d", mesh ? mesh->SubmeshCount() : 0);
							int mesh_count = 0;
							static int s_mesh_selected_index = -1;
							if (ImGui::BeginCombo("Select Mesh: ", mesh ? mesh->Name().c_str() : "missing"))
							{

								for (auto it = g_pResourceMgr->ResourceBegin<Mesh>(); it != g_pResourceMgr->ResourceEnd<Mesh>(); it++)
								{
									auto mesh = ResourceMgr::IterToRefPtr<Mesh>(it);
									if (is_skined_comp && !dynamic_cast<SkinedMesh*>(mesh.get()))
										continue;
									if (ImGui::Selectable(mesh->Name().c_str(), s_mesh_selected_index == mesh_count))
										s_mesh_selected_index = mesh_count;
									if (s_mesh_selected_index == mesh_count)
									{
										static_mesh_comp->SetMesh(mesh);
										g_pSceneMgr->MarkCurSceneDirty();
									}
									++mesh_count;
								}
								ImGui::EndCombo();
							}
						}
					}
					//int mat_prop_index = 0;
					auto& materials = static_mesh_comp->GetMaterials();
					ImGui::Text("Material: %d", materials.size());
					if (ImGui::Button("Add Material"))
					{
						static_mesh_comp->AddMaterial(nullptr);
						g_pSceneMgr->MarkCurSceneDirty();
					};
					ImGui::SameLine();
					if (ImGui::Button("Remove Material"))
					{
						static_mesh_comp->RemoveMaterial(static_cast<u16>(materials.size() - 1));
						g_pSceneMgr->MarkCurSceneDirty();
					};
					for (int i = 0; i < materials.size(); i++)
					{
						int mat_count = 0;
						auto mat = materials[i];
						static int s_mat_selected_index = -1;
						ImGui::Text("  Material slot %d", i);
						ImGui::SameLine();
						String slot_label = std::format("{}: {}", i, mat ? mat->Name() : "Select Material");
						if (ImGui::CollapsingHeader(slot_label.c_str()))
						{
							if (ImGui::BeginCombo("Select Material: ", mat ? mat->Name().c_str() : "missing"))
							{
								for (auto it = g_pResourceMgr->ResourceBegin<Material>(); it != g_pResourceMgr->ResourceEnd<Material>(); it++)
								{
									auto mat = ResourceMgr::IterToRefPtr<Material>(it);
									if (ImGui::Selectable(mat->Name().c_str(), s_mat_selected_index == mat_count))
										s_mat_selected_index = mat_count;
									if (s_mat_selected_index == mat_count)
									{
										static_mesh_comp->SetMaterial(mat, i);
										g_pSceneMgr->MarkCurSceneDirty();
									}
									++mat_count;
								}
								ImGui::EndCombo();
							}
							if (mat)
							{
								int shader_count = 0;
								static int s_shader_selected_index = -1;
								if (ImGui::BeginCombo("Select Shader: ", mat->GetShader()->Name().c_str()))
								{

									for (auto it = g_pResourceMgr->ResourceBegin<Shader>(); it != g_pResourceMgr->ResourceEnd<Shader>(); it++)
									{
										auto shader = ResourceMgr::IterToRefPtr<Shader>(it);
										if (ImGui::Selectable(shader->Name().c_str(), s_shader_selected_index == shader_count))
											s_shader_selected_index = shader_count;
										if (s_shader_selected_index == shader_count)
											mat->ChangeShader(shader.get());
										++shader_count;
									}
									ImGui::EndCombo();
								}
								if (mat->IsInternal())
								{
									DrawInternalStandardMaterial(mat.get(), selector_window);
								}
								else
								{
									for (auto& prop : mat->GetAllProperties())
									{
										DrawProperty(property_handle, prop.second, mat.get(), selector_window);
									}
								}
							}
						}
					}
					if (comp->GetType() == SkinedMeshComponent::GetStaticType())
					{
						auto sk_mesh_comp = static_cast<SkinedMeshComponent*>(comp);
						ImGui::Text("Animation: ");
						auto sk_mesh = dynamic_cast<SkinedMesh*>(sk_mesh_comp->GetMesh().get());
						if (sk_mesh)
						{
							u32 anim_index = 0;
							static u32 s_anim_selected_index = -1;
							auto clip = sk_mesh_comp->GetAnimationClip();
							if (ImGui::BeginCombo("Select Animation: ", clip ? clip->Name().c_str() : "null"))
							{
								for (auto it = AnimationClipLibrary::Begin(); it != AnimationClipLibrary::End(); it++)
								{
									auto& clip = it->second;
									if (clip->CurSkeletion() == sk_mesh->CurSkeleton())
									{
										if (ImGui::Selectable(it->second->Name().c_str(), anim_index == s_anim_selected_index))
											s_anim_selected_index = anim_index;
										if (s_anim_selected_index == anim_index)
										{
											sk_mesh_comp->SetAnimationClip(clip);
										}
										++anim_index;
									}
								}
								ImGui::EndCombo();
							}
							if (clip)
							{
								ImGui::Text("Name: %s", clip->Name().c_str());
								ImGui::Text("FrameCount: %d", clip->FrameCount());
								ImGui::Text("Duration: %f s", clip->Duration());
								ImGui::Text("SkinTime: %.2f ms", sk_mesh_comp->_skin_time);
								ImGui::Checkbox("DrawSkeleton", &sk_mesh_comp->_is_draw_debug_skeleton);
								ImGui::Checkbox("MT Skin", &sk_mesh_comp->_is_mt_skin);
								ImGui::SliderFloat("Time", &sk_mesh_comp->_anim_time, 0.f, (float)sk_mesh_comp->GetAnimationClip()->FrameCount());
							}
						}
					}
				}
				else if (comp->GetType() == CameraComponent::GetStaticType())
				{
					auto& cam = static_cast<CameraComponent*>(comp)->_camera;
					const char* items[]{ "Orthographic","Perspective" };
					int item_current_idx = 0;
					if (cam.Type() == ECameraType::kPerspective) item_current_idx = 1;
					const char* combo_preview_value = items[item_current_idx];
					if (ImGui::BeginCombo("CameraType", combo_preview_value, 0))
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
					float cam_near = cam.Near(), cam_far = cam.Far(), aspect = cam.Aspect();
					if (item_current_idx == 0) cam.Type(ECameraType::kOrthographic);
					else cam.Type(ECameraType::kPerspective);
					ImGui::DragFloat("Near", &cam_near, 1.0f, 0, 1000.0f);
					ImGui::DragFloat("Far", &cam_far, 1.0f, cam_near, 500000.f);
					ImGui::SliderFloat("Aspect", &aspect, 0.01f, 5.f);
					if (cam.Type() == ECameraType::kPerspective)
					{
						float fov_h = cam.FovH();
						ImGui::SliderFloat("Horizontal Fov", &fov_h, 0.0f, 135.0f);
						cam.FovH(fov_h);
					}
					else
					{
						float size = cam.Size();
						ImGui::SliderFloat("Size", &size, 0.0f, 1000.0f);
						cam.Size(size);
					}
					cam.Near(cam_near);
					cam.Far(cam_far);
					cam.Aspect(aspect);
				}
			}
		}

		ObjectDetail::ObjectDetail() : ImGuiWidget("ObjectDetail")
		{
			_p_texture_selector = new TextureSelector();
		}
		ObjectDetail::~ObjectDetail()
		{
			DESTORY_PTR(_p_texture_selector);
		}
		void ObjectDetail::Open(const i32& handle)
		{
			ImGuiWidget::Open(handle);
		}
		void ObjectDetail::Close(i32 handle)
		{
			ImGuiWidget::Close(handle);
		}
		void ObjectDetail::ShowImpl()
		{
			auto s = Selection::First<Shader>();
			auto actor = Selection::First<SceneActor>();
			if (s)
			{
				ShowShaderPannel(s);
			}
			else if (actor)
			{
				ShowSceneActorPannel(actor);
			}
			else
			{

			}
		}
		void ObjectDetail::ShowSceneActorPannel(SceneActor* cur_selected_actor)
		{
			if (cur_selected_actor != nullptr)
			{
				ImGui::BulletText(cur_selected_actor->Name().c_str());
				ImGui::SameLine();
				i32 new_comp_count = 0;
				i32 selected_new_comp_index = -1;
				if (ImGui::BeginCombo(" ", "+ Add Component"))
				{
					auto& type_str = Component::GetAllComponentTypeStr();
					for (auto& comp_type : type_str)
					{
						if (ImGui::Selectable(comp_type.c_str(), new_comp_count == selected_new_comp_index))
							selected_new_comp_index = new_comp_count;
						if (selected_new_comp_index == new_comp_count)
						{
							if (comp_type == Component::GetTypeName(StaticMeshComponent::GetStaticType()))
								cur_selected_actor->AddComponent<StaticMeshComponent>();
							else if (comp_type == Component::GetTypeName(SkinedMeshComponent::GetStaticType()))
								cur_selected_actor->AddComponent<SkinedMeshComponent>();
							else if (comp_type == Component::GetTypeName(LightComponent::GetStaticType()))
							{
								cur_selected_actor->AddComponent<LightComponent>();
							}
						}
						++new_comp_count;
					}
					ImGui::EndCombo();
				}
				u32 comp_index = 0;
				Component* comp_will_remove = nullptr;
				for (auto& comp : cur_selected_actor->GetAllComponent())
				{
					//if (comp->GetTypeName() == StaticMeshComponent::GetStaticType()) continue;
					ImGui::PushID(comp_index);
					bool b_active = comp->Active();
					ImGui::Checkbox("", &b_active);
					if (b_active != comp->Active())
					{
						g_pSceneMgr->MarkCurSceneDirty();
					}
					ImGui::PopID();
					comp->Active(b_active);
					ImGui::SameLine();
					ImGui::PushID(comp_index);
					if (ImGui::Button("x"))
						comp_will_remove = comp.get();
					ImGui::PopID();
					ImGui::SameLine();
					DrawComponentProperty(_property_handle, comp.get(), *_p_texture_selector);
					++comp_index;
				}
				if (comp_will_remove && comp_will_remove->GetType() != EComponentType::kTransformComponent)
				{
					cur_selected_actor->RemoveComponent(comp_will_remove);
					g_pSceneMgr->MarkCurSceneDirty();
				}
			}
			_p_texture_selector->Show();
		}

		void ObjectDetail::ShowShaderPannel(Shader* s)
		{
			f32 indent = 10.0f;
			ImGui::Text("Name: %s", s->Name().c_str());
			for (u16 i = 0; i < s->PassCount(); i++)
			{
				auto& pass = s->GetPassInfo(i);
				ImGui::Indent(indent);
				ImGui::Text("Pass_%d: %s",i,pass._name.c_str());
				{
					ImGui::Indent(indent);
					ImGui::Text("vertex entry: %s",pass._vert_entry.c_str());
					ImGui::Text("pixel entry: %s",pass._pixel_entry.c_str());
					{
						ImGui::Text("local keywords: ");
						for (auto& kw_group : pass._keywords)
						{
							String kws;
							for (auto& kw : kw_group)
							{
								kws.append(kw);
								kws.append(" ");
							}
							ImGui::Text("group: %s",kws.c_str());
						}
						ImGui::Text("variants: ");
						for (auto& variant : pass._shader_variants)
						{
							auto& [hash, kw_seq] = variant;
							String kws;
							for (auto& kw : kw_seq)
							{
								kws.append(kw);
								kws.append(" ");
							}
							ImGui::Text(": %s", kws.c_str());
						}
					}
					ImGui::Unindent(indent);
				}
				ImGui::Unindent(indent);
			}
		}
	}
}
