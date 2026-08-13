#include "Inspector/ReflectedPropertyPanel.h"
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
