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
		for (auto layer : _layers)
			delete layer;
	}
	void LayerStack::PushLayer(Layer* layer)
	{
		_layers_insert = _layers.emplace(_layers_insert, layer);
	}
	void LayerStack::PushOverLayer(Layer* overlayer)
	{
		_layers.emplace_back(overlayer);
	}
	void LayerStack::PopLayer(Layer* layer)
	{
		auto it = std::find(_layers.begin(), _layers.end(), layer);
		if (it != _layers.end())
		{
			_layers.erase(it);
			_layers_insert--;
		}		
	}
	void LayerStack::PopOverLayer(Layer* overlayer)
	{
		auto it = std::find(_layers.begin(), _layers.end(), overlayer);
		if (it != _layers.end())
			_layers.erase(it);
	}
}
