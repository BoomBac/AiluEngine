#pragma warning(push)
#pragma warning(disable: 4251) //std库直接暴露为接口dll在客户端上使用时可能会有问题，禁用该编译警告

#pragma once
#ifndef __LAYER_H__
#define __LAYER_H__
#include "GlobalMarco.h"
#include "Framework/Events/Event.h"
namespace Ailu
{
	class AILU_API Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer();
		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate() {}
		virtual void OnEvent(Event& e) {}

		inline const std::string GetName() const { return _debug_name; }
	protected:
		std::string _debug_name;
	};
}

#pragma warning(pop)
#endif // !LAYER_H__

