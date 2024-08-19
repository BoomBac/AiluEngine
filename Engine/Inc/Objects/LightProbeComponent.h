#pragma warning(disable : 4251)
#pragma once
#ifndef __LIGHT_PROBE_COMP_H__
#define __LIGHT_PROBE_COMP_H__
#include "Component.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Camera.h"

namespace Ailu
{
	class CubeMapGenPass;
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
        void UpdateProbe();
        Ref<Material> _debug_mat;
	public:
        f32 _size = 10.0f;
		bool _update_every_tick = false;
		int _src_type = 0;
		f32 _mipmap = 0.0f;
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
	private:
		Vector<Camera> _shadow_cameras;
        Scope<IConstantBuffer> _obj_cbuf;
        Ref<RenderTexture> _cubemap;
		Scope<CubeMapGenPass> _pass;
	};
	REFLECT_FILED_BEGIN(LightProbeComponent)

	REFLECT_FILED_END
}

#endif // !__LIGHT_PROBE_COMP_H__

