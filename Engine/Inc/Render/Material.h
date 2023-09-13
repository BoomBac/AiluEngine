#pragma once
#ifndef __MATERIAL_H__
#define __MATERIAL_H__
#include "Shader.h"
#include "GlobalMarco.h"
namespace Ailu
{
	class Material
	{
	public:
		Material(Ref<Shader> shader);
		void SetVector(const std::string& name, const Vector4f& vector);
		void Bind();
		Shader* GetShader() const;
	private:
		Ref<Shader> _p_shader;
		uint8_t* _p_cbuf = nullptr;
		uint32_t _cbuf_index;
		inline static uint32_t s_current_cbuf_offset = 0u;
	};
}

#endif // !MATERIAL_H__
