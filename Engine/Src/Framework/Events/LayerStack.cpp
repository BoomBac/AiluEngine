#include "pch.h"
#include "Framework/Events/LayerStack.h"

namespace Ailu
{
    LayerStack::LayerStack()
    {
        _layers_insert = _layers.begin();
    }
    LayerStack::~LayerStack()
    {
        for (auto layer: _layers)
        {
            layer->OnDetach();
            delete layer;
        }
    }
    void LayerStack::PushLayer(Layer* layer)
    {
        _layers_insert = _layers.emplace(_layers.begin(), layer);
        if (_layers_insert == _layers.end())
            _layers_insert = _layers.begin();
        layer->OnAttach();
    }

    void LayerStack::PushOverLayer(Layer* overlayer)
    {
        _layers.emplace_back(overlayer);
        overlayer->OnAttach();
    }
    void LayerStack::PopLayer(Layer* layer)
    {
        auto it = std::find(_layers.begin(), _layers.end(), layer);
        if (it != _layers.end())
        {
            layer->OnDetach();
            _layers.erase(it);
        }
    }
    void LayerStack::PopOverLayer(Layer* overlayer)
    {
        auto it = std::find(_layers.begin(), _layers.end(), overlayer);
        if (it != _layers.end())
        {
            overlayer->OnDetach();
            _layers.erase(it);
        }
    }
}
