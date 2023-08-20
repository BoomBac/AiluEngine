#pragma once
#ifndef __LAYERSTACK_H__
#define __LAYERSTACK_H__
#include "GlobalMarco.h"
#include "Layer.h"
namespace Ailu
{
	class AILU_API LayerStack
	{
	public:
		LayerStack();
		~LayerStack();

		void PushLayer(Layer* layer);
		void PushOverLayer(Layer* overlayer);
		void PopLayer(Layer* layer);
		void PopOverLayer(Layer* overlayer);

		std::vector<Layer*>::iterator begin() { return _layers.begin(); }
		std::vector<Layer*>::iterator end() { return _layers.end(); }
	private:
		std::vector<Layer*> _layers;
		std::vector<Layer*>::iterator _layers_insert;
	};
}


#endif // !LAYERSTACK_H__

