#include "Assets/WidgetAsset.h"
#include "Objects/JsonArchive.h"
#include "UI/Container.h"

namespace Ailu
{
    Ref<UI::UIElement> CloneUIElementTree(const Ref<UI::UIElement> &source)
    {
        if (source == nullptr)
            return nullptr;

        const String root_name = "_ui_element_root";
        Ref<UI::UIElement> source_copy = source;
        JsonArchive save_archive;
        SerializerWrapper<Ref<UI::UIElement>>::Serialize(&source_copy, save_archive, &root_name);

        JsonArchive load_archive;
        if (!load_archive.LoadFromString(save_archive.SaveToString()))
            return nullptr;

        Ref<UI::UIElement> clone;
        SerializerWrapper<Ref<UI::UIElement>>::Deserialize(&clone, load_archive, &root_name);
        return clone;
    }

    WidgetAsset::WidgetAsset() : Object("WidgetAsset")
    {
        auto root = MakeRef<UI::Canvas>();
        root->Name("RootCanvas");
        SetRoot(std::move(root));
    }

    WidgetAsset::WidgetAsset(const String &name) : Object(name)
    {
        auto root = MakeRef<UI::Canvas>();
        root->Name("RootCanvas");
        SetRoot(std::move(root));
    }

    void WidgetAsset::SetRoot(Ref<UI::UIElement> root)
    {
        _root = std::move(root);
        if (_root != nullptr && _root->Name().empty())
            _root->Name("RootCanvas");
    }

    Ref<UI::Widget> WidgetAsset::CreateInstance() const
    {
        auto widget = MakeRef<UI::Widget>();
        widget->SetSize(_design_size);
        widget->AddToWidget(CloneUIElementTree(_root));
        return widget;
    }
}
