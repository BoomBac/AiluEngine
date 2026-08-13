#pragma once

#ifndef __WIDGET_ASSET_H__
#define __WIDGET_ASSET_H__

#include "Assets/AssetDocument.h"
#include "UI/UIElement.h"
#include "UI/Widget.h"
#include "generated/WidgetAsset.gen.h"

namespace Ailu
{
    AILU_API Ref<UI::UIElement> CloneUIElementTree(const Ref<UI::UIElement> &source);

    ACLASS()
    class AILU_API WidgetAsset : public Object
    {
        GENERATED_BODY()

    public:
        WidgetAsset();
        explicit WidgetAsset(const String &name);

        const Vector2f &DesignSize() const { return _design_size; }
        void SetDesignSize(const Vector2f &design_size) { _design_size = design_size; }
        UI::UIElement *Root() const { return _root.get(); }
        const Ref<UI::UIElement> &RootRef() const { return _root; }
        void SetRoot(Ref<UI::UIElement> root);
        Ref<UI::Widget> CreateInstance() const;

    private:
        APROPERTY()
        Vector2f _design_size = {1920.0f, 1080.0f};
        APROPERTY()
        Ref<UI::UIElement> _root;
    };

    ACLASS()
    class AILU_API WidgetAssetDocument : public Object
    {
        GENERATED_BODY()

    public:
        APROPERTY()
        AssetDocumentHeader _header;
        APROPERTY()
        Vector2f _design_size = {1920.0f, 1080.0f};
        APROPERTY()
        Ref<UI::UIElement> _root;
    };
}

#endif
