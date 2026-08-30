#include "Inspector/ReflectedPropertyPanel.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Animation/SkeletonAsset.h"
#include "Assets/PrefabAsset.h"
#include "Render/2D/Sprite.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Shader.h"
#include "Render/Texture.h"
#include "UI/Composite.h"
#include "UI/Container.h"

namespace Ailu
{
    namespace Editor
    {
        bool ReflectedPropertyPanel::ReadMetaBool(const MemberInfo &member, const String &key, bool default_val)
        {
            return member.MetaInfo().GetBool(key, default_val);
        }

        f32 ReflectedPropertyPanel::ReadMetaFloat(const MemberInfo &member, const String &key, f32 default_val)
        {
            return member.MetaInfo().GetFloat(key, default_val);
        }

        String ReflectedPropertyPanel::ResolveDisplayName(const PropertyInfo &property) const
        {
            String name = property.MetaInfo().GetString("DisplayName", String{});
            if (!name.empty())
                return name;
            name = property.Name();
            if (!name.empty() && name[0] == '_')
                name = name.substr(1);
            return name;
        }

        String ReflectedPropertyPanel::ResolveCategory(const PropertyInfo &property) const
        {
            return property.MetaInfo().GetString("Category", String{"General"});
        }

        Scope<UI::CompositeBuilder::Params> ReflectedPropertyPanel::BuildParams(const PropertyInfo &property) const
        {
            const Type *asset_type = nullptr;
            const String &name = property.Name();
            if (name == "_skeleton")
                asset_type = SkeletonAsset::StaticType();
            else if (name == "_shader_guid")
                asset_type = Render::Shader::StaticType();
            else if (name == "_texture" || name == "_texture_guid")
                asset_type = Render::Texture2D::StaticType();
            else if (name == "_preview_mesh_guid" || name == "_mesh_guid")
                asset_type = Render::Mesh::StaticType();
            else if (name == "_sprite" || name == "_sprite_guid")
                asset_type = Render::Sprite::StaticType();
            else if (name == "_material_guid")
                asset_type = Render::Material::StaticType();
            else if (name == "_controller_guid")
                asset_type = AnimationControllerAsset::StaticType();
            else if (name == "_clip_guid")
                asset_type = AnimationClip::StaticType();
            else if (name == "_prefab_asset")
                asset_type = PrefabAssetDocument::StaticType();

            if (asset_type != nullptr)
            {
                auto params = MakeScope<UI::ObjectAssetFieldParams>();
                params->_object_type = asset_type;
                params->_allow_none = true;
                return params;
            }
            const bool is_range = ReadMetaBool(property, "IsRange", false);
            if (!is_range)
                return nullptr;

            auto params = MakeScope<UI::FloatFieldParams>();
            params->_range.x = ReadMetaFloat(property, "RangeMin", 0.0f);
            params->_range.y = ReadMetaFloat(property, "RangeMax", 1.0f);
            params->_step = ReadMetaFloat(property, "Step", 0.01f);
            return params;
        }

        void ReflectedPropertyPanel::SortProperties(Vector<const PropertyInfo *> &properties) const
        {
            std::sort(properties.begin(), properties.end(), [this](const PropertyInfo *a, const PropertyInfo *b)
            {
                String cat_a = ResolveCategory(*a);
                String cat_b = ResolveCategory(*b);
                if (cat_a != cat_b)
                    return cat_a < cat_b;

                i32 order_a = a->MetaInfo().GetInt("Order", 0);
                i32 order_b = b->MetaInfo().GetInt("Order", 0);
                if (order_a != order_b)
                    return order_a < order_b;

                return a->Name() < b->Name();
            });
        }

        void ReflectedPropertyPanel::BuildCategories(const BuildArgs &args, const Vector<const PropertyInfo *> &properties)
        {
            String current_category;
            UI::CollapsibleView *current_block = nullptr;
            UI::VerticalBox *current_content = args._parent;

            for (const PropertyInfo *property : properties)
            {
                String category = ResolveCategory(*property);
                if (category != current_category)
                {
                    current_category = category;
                    if (!current_category.empty())
                    {
                        current_block = args._parent->AddChild<UI::CollapsibleView>(current_category);
                        current_block->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
                        current_content = static_cast<UI::VerticalBox *>(current_block->GetContent()->AddChild<UI::VerticalBox>());
                    }
                    else
                    {
                        current_block = nullptr;
                        current_content = args._parent;
                    }
                }

                String display_name = ResolveDisplayName(*property);

                if (ReadMetaBool(*property, "Hidden", false))
                    continue;

                auto params = BuildParams(*property);
                if (args._on_property_changing || args._on_property_changed)
                {
                    if (params == nullptr)
                        params = MakeScope<UI::CompositeBuilder::Params>();
                    params->_on_value_changing = [callback = args._on_property_changing](PropertyInfo *p)
                    {
                        if (callback)
                            callback(*p);
                    };
                    params->_on_value_changed = [callback = args._on_property_changed](PropertyInfo *p)
                    {
                        if (callback)
                            callback(*p);
                    };
                }

                Ref<UI::UIElement> element = UI::CompositeBuilder::BuildPropertyElement(display_name, const_cast<PropertyInfo *>(property), args._instance, params.get());
                if (element != nullptr)
                {
                    current_content->AddChild(element);
                    if (params != nullptr)
                        _params.push_back(std::move(params));
                }
            }
        }

        void ReflectedPropertyPanel::Build(const BuildArgs &args)
        {
            Clear();

            if (args._type == nullptr || args._instance == nullptr || args._parent == nullptr)
                return;

            Vector<const PropertyInfo *> properties;

            for (const Type *type = args._type; type != nullptr; type = type->BaseType())
            {
                for (const PropertyInfo &property : type->GetProperties())
                {
                    if (args._filter && !args._filter(property))
                        continue;

                    if (ReadMetaBool(property, "Hidden", false))
                        continue;

                    properties.emplace_back(&property);
                }
            }

            SortProperties(properties);
            BuildCategories(args, properties);
        }

        void ReflectedPropertyPanel::Clear()
        {
            _params.clear();
        }
    }// namespace Editor
}// namespace Ailu
