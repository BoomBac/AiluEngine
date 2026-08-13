#ifndef INSPECTOR_REFLECTEDPROPERTYPANEL_H
#define INSPECTOR_REFLECTEDPROPERTYPANEL_H
#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Objects/Type.h"
#include "UI/Composite.h"
#include <functional>

namespace Ailu
{
    namespace UI
    {
        class VerticalBox;
    }
    namespace Editor
    {
        class ReflectedPropertyPanel final : public NonCopyable
        {
        public:
            using PropertyFilter = std::function<bool(const PropertyInfo &)>;
            using PropertyChanging = std::function<void(const PropertyInfo &)>;
            using PropertyChanged = std::function<void(const PropertyInfo &)>;

            struct BuildArgs
            {
                const Type *_type = nullptr;
                void *_instance = nullptr;
                UI::VerticalBox *_parent = nullptr;
                PropertyFilter _filter;
                PropertyChanging _on_property_changing;
                PropertyChanged _on_property_changed;
            };

            void Build(const BuildArgs &args);
            void Clear();

        private:
            Vector<Scope<UI::CompositeBuilder::Params>> _params;

            void SortProperties(Vector<const PropertyInfo *> &properties) const;
            void BuildCategories(const BuildArgs &args, const Vector<const PropertyInfo *> &properties);
            Scope<UI::CompositeBuilder::Params> BuildParams(const PropertyInfo &property) const;
            String ResolveDisplayName(const PropertyInfo &property) const;
            String ResolveCategory(const PropertyInfo &property) const;

            static bool ReadMetaBool(const MemberInfo &member, const String &key, bool default_val);
            static f32 ReadMetaFloat(const MemberInfo &member, const String &key, f32 default_val);
        };
    }// namespace Editor
}// namespace Ailu
#endif// INSPECTOR_REFLECTEDPROPERTYPANEL_H
