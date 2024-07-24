#pragma warning(disable : 4251)
#pragma once
#ifndef __LIGHT_PROBE_COMP_H__
#define __LIGHT_PROBE_COMP_H__
#include "Component.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Camera.h"

namespace Ailu
{
    class AILU_API LightProbeComponent : public Component
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(LightProbeComponent)
		DECLARE_REFLECT_FIELD(LightProbeComponent)
	public:
		LightProbeComponent();
		void Tick(const float& delta_time) final;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) final;
		void OnGizmo() final;
	public:
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
	private:
		Vector<Camera> _shadow_cameras;
	};
	REFLECT_FILED_BEGIN(LightProbeComponent)

	REFLECT_FILED_END
}

#endif // !__LIGHT_PROBE_COMP_H__

