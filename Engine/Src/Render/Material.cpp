#include "pch.h"
#include "Render/Material.h"

namespace Ailu
{
	Material::Material(Ref<Shader> shader) : _p_shader(shader)
	{
		_cbuf_index = s_current_cbuf_offset++;
		_p_cbuf = _p_shader->GetCBufferPtr(_cbuf_index);
	}

	void Material::SetVector(const std::string& name, const Vector4f& vector)
	{
		memcpy(_p_cbuf + _p_shader->GetVariableOffset(name), &vector, sizeof(vector));
	}

	void Material::Bind()
	{
		_p_shader->Bind(_cbuf_index);
	}
	Shader* Material::GetShader() const
	{
		return _p_shader.get();
	}
}
