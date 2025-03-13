#include "Render/Material.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "pch.h"
#include <iosfwd>

namespace Ailu
{
    Material::Material(Shader *shader, String name) : _p_shader(shader)
    {
        AL_ASSERT(s_total_material_num < RenderConstants::kMaxMaterialDataCount);
        _name = name;
        _p_active_shader = _p_shader;
        Construct(true);
        shader->AddMaterialRef(this);
        ++s_total_material_num;
    }

    Material &Material::operator=(const Material &other)
    {
        //标准pass才会使用上面两个变量
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        for (auto &cbuf: other._p_cbufs)
        {
            u32 buffer_size = cbuf->GetBufferSize();
            _p_cbufs.emplace_back(ConstantBuffer::Create(buffer_size));
            memcpy(_p_cbufs.back()->GetData(), cbuf->GetData(), buffer_size);
        }
        _textures_all_passes = other._textures_all_passes;
        return *this;
    }

    Material &Material::operator=(Material &&other) noexcept
    {
        //标准pass才会使用上面两个变量
        _name = other._name;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _p_cbufs = std::move(other._p_cbufs);
        other._p_cbufs.clear();
        _textures_all_passes = std::move(other._textures_all_passes);
        other._textures_all_passes.clear();
        return *this;
    }

    Material::Material(const Material &other)
    {
        _name = other._name;
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        for (auto &cbuf: other._p_cbufs)
        {
            u32 buffer_size = cbuf->GetBufferSize();
            _p_cbufs.emplace_back(ConstantBuffer::Create(buffer_size));
            memcpy(_p_cbufs.back()->GetData(), cbuf->GetData(), buffer_size);
        }
        _textures_all_passes = other._textures_all_passes;
    }
    Material::Material(Material &&other) noexcept
    {
        _p_shader = other._p_shader;
        _p_active_shader = other._p_active_shader;
        _standard_pass_index = other._standard_pass_index;
        _mat_cbuf_per_pass_size = other._mat_cbuf_per_pass_size;
        _p_cbufs = std::move(other._p_cbufs);
        other._p_cbufs.clear();
        _textures_all_passes = std::move(other._textures_all_passes);
        other._textures_all_passes.clear();
    }

    Material::~Material()
    {
        --s_total_material_num;
    }
    void Material::Bind(u16 pass_index)
    {
        AL_ASSERT(pass_index < _pass_variants.size());
        auto &cur_pass_variant_hash = _pass_variants[pass_index]._variant_hash;
        auto variant_state = _p_active_shader->GetVariantState(pass_index, cur_pass_variant_hash);
        if (variant_state != EShaderVariantState::kReady)
            return;
        _p_active_shader->SetCullMode((ECullMode) _common_uint_property[kCullModeKey]);
        _p_active_shader->Bind(pass_index, cur_pass_variant_hash);
        i8 cbuf_bind_slot = _p_active_shader->_passes[pass_index]._variants[cur_pass_variant_hash]._per_mat_buf_bind_slot;
        if (cbuf_bind_slot != -1)
        {
            GraphicsPipelineStateMgr::SubmitBindResource(PipelineResource(_p_cbufs[pass_index].get(), EBindResDescType::kConstBuffer, cbuf_bind_slot, PipelineResource::kPriorityLocal));
        }
        auto &bind_textures = _textures_all_passes[pass_index];
        auto &bind_infos = _p_active_shader->_passes[pass_index]._variants[cur_pass_variant_hash]._bind_res_infos;
        for (auto it = bind_textures.begin(); it != bind_textures.end(); it++)
        {
            auto bind_it = bind_infos.find(it->first);
            if (bind_it != bind_infos.end())
            {
                auto &[slot, texture] = it->second;
                if (texture != nullptr)
                {
                    GraphicsPipelineStateMgr::SubmitBindResource(PipelineResource(texture, EBindResDescType::kTexture2D, bind_it->second._bind_slot, PipelineResource::kPriorityLocal));
                }
            }
            //else
            //{
            //	LOG_WARNING("Material: {} haven't set texture on bind slot {}",_name,(short)slot);
            //}
        }
    }

    void Material::ChangeShader(Shader *shader)
    {
        _p_shader->RemoveMaterialRef(this);
        _p_shader = shader;
        _p_shader->AddMaterialRef(this);
        _p_active_shader->RemoveMaterialRef(this);
        _p_active_shader = shader;
        _p_active_shader->AddMaterialRef(this);
        Construct(false);
    }

    bool Material::IsReadyForDraw(u16 pass_index) const
    {
        return _p_active_shader->GetVariantState(pass_index, _pass_variants[pass_index]._variant_hash) == EShaderVariantState::kReady;
    }

    void Material::SetFloat(const String &name, const float &f)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
            {
                memcpy(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &f, sizeof(f));
                //LOG_WARNING("float value{}", *reinterpret_cast<float*>(_p_cbuf->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second)));
            }
            else
            {
                //LOG_WARNING("Material: {} set float with name {} failed!", _name, name);
            }
            ++pass_index;
        }
    }

    void Material::SetUint(const String &name, const u32 &value)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
            {
                memcpy(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(value));
                auto offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                //LOG_WARNING("set uint {} value to {} at address {}", name, *reinterpret_cast<u32 *>(_p_cbufs[pass._index]->GetData() + offset), (u64)(_p_cbufs[pass._index]->GetData() + offset));
            }
            else
            {
                //LOG_WARNING("Material: {} set uint with name {} failed!", _name, name);
            }
            ++pass_index;
        }
        _common_uint_property[name] = value;
    }

    void Material::SetVector(const String &name, const Vector4f &vector)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
            {
                u16 offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                u16 size = ShaderBindResourceInfo::GetVariableSize(it->second);
                memcpy(_p_cbufs[pass._index]->GetData() + offset, &vector, size);
            }
            else
            {
                //LOG_WARNING("Material: {} set vector with name {} failed!", _name, name);
            }
            ++pass_index;
        }
    }
    void Material::SetVector(const String &name, const Vector4Int &vector)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
            {
                u16 offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                u16 size = ShaderBindResourceInfo::GetVariableSize(it->second);
                if (it->second._res_type & EBindResDescType::kCBufferUInts)
                {
                    Vector4UInt vector_uint = {(u32) vector.x, (u32) vector.y, (u32) vector.z, (u32) vector.w};
                    memcpy(_p_cbufs[pass._index]->GetData() + offset, &vector_uint, size);
                }
                else
                    memcpy(_p_cbufs[pass._index]->GetData() + offset, &vector, size);
            }
            else
            {
                //LOG_WARNING("Material: {} set vector with name {} failed!", _name, name);
            }
            ++pass_index;
        }
    }


    void Material::SetMatrix(const String &name, const Matrix4x4f &matrix)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
            {
                memcpy(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second), &matrix, sizeof(matrix));
            }
            else
            {
                //LOG_WARNING("Material: {} set vector with name {} failed!", _name, name);
            }
            ++pass_index;
        }
    }
    float Material::GetFloat(const String &name)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
                return *reinterpret_cast<float *>(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second));
            ++pass_index;
        }
        return 0.0f;
    }
    void Material::SetCullMode(ECullMode mode)
    {
        _common_uint_property[kCullModeKey] = (u32) mode;
    }
    ECullMode Material::GetCullMode() const
    {
        return (ECullMode) _common_uint_property.at(kCullModeKey);
    };

    ShaderVariantHash Material::ActiveVariantHash(u16 pass_index) const
    {
        AL_ASSERT(pass_index < _pass_variants.size());
        return _pass_variants[pass_index]._variant_hash;
    }
    std::set<String> Material::ActiveKeywords(u16 pass_index) const
    {
        AL_ASSERT(pass_index < _pass_variants.size());
        return _pass_variants[pass_index]._keywords;
    };

    u32 Material::GetUint(const String &name)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
            {
                auto offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                u32 value = *reinterpret_cast<u32 *>(_p_cbufs[pass._index]->GetData() + offset);
                //LOG_WARNING("get uint {} value {} at address {}", name, value, (u64) (_p_cbufs[pass._index]->GetData() + offset));
                return value;
            }
            ++pass_index;
        }
        if (_common_uint_property.contains(name))
        {
            return _common_uint_property[name];
        }
        return -1;
    }

    Vector4f Material::GetVector(const String &name)
    {
        u16 pass_index = 0;
        for (auto &pass: _p_shader->_passes)
        {
            auto &res_info = pass._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
            auto it = res_info.find(name);
            if (it != res_info.end())
                return *reinterpret_cast<Vector4f *>(_p_cbufs[pass._index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(it->second));
            ++pass_index;
        }
        return Vector4f::kZero;
    }

    void Material::SetTexture(const String &name, Texture *texture)
    {
        for (auto &pass_tex: _textures_all_passes)
        {
            auto it = pass_tex.find(name);
            if (it == pass_tex.end())
            {
                continue;
            }
            std::get<1>(pass_tex[name]) = texture;
            if (_properties.find(name) != _properties.end())
            {
                _properties[name]._value_ptr = reinterpret_cast<void *>(texture);
            }
        }
        //else
        //	LOG_WARNING("Cann't find texture prop with value name: {} when set material {} texture!", name, _name);
    }

    void Material::SetTexture(const String &name, const WString &texture_path)
    {
        auto texture = g_pResourceMgr->Get<Texture2D>(texture_path);
        if (texture == nullptr)
        {
            g_pLogMgr->LogErrorFormat("Cann't find texture: {} when set material {} texture{}!", ToChar(texture_path), _name, name);
            return;
        }
        SetTexture(name, texture);
    }

    void Material::SetTexture(const String &name, RTHandle texture)
    {
        auto raw_texture = g_pRenderTexturePool->Get(texture);
        SetTexture(name, raw_texture);
    }

    void Material::EnableKeyword(const String &keyword)
    {
        u16 pass_index = 0;
        if (_all_keywords.contains(keyword))
            return;
        for (auto &p: _pass_variants)
        {
            if (_p_active_shader->IsKeywordValid(pass_index, keyword))
            {
                auto group_kws = _p_active_shader->KeywordsSameGroup(pass_index, keyword);
                for (auto &kw: group_kws)
                {
                    _pass_variants[pass_index]._keywords.erase(kw);
                    _all_keywords.erase(kw);
                }
                _pass_variants[pass_index]._keywords.insert(keyword);
                _pass_variants[pass_index]._variant_hash = _p_active_shader->ConstructVariantHash(pass_index, _pass_variants[pass_index]._keywords);
                _all_keywords.insert(keyword);
                UpdateBindTexture(pass_index, _pass_variants[pass_index]._variant_hash);
            }
            ++pass_index;
        }
    }

    void Material::DisableKeyword(const String &keyword)
    {
        if (!_all_keywords.contains(keyword))
            return;
        u16 pass_index = 0;
        for (auto &p: _pass_variants)
        {
            if (_p_active_shader->IsKeywordValid(pass_index, keyword))
            {
                if (_pass_variants[pass_index]._keywords.contains(keyword))
                {
                    _pass_variants[pass_index]._keywords.erase(keyword);
                    _pass_variants[pass_index]._variant_hash = _p_active_shader->ConstructVariantHash(pass_index, _pass_variants[pass_index]._keywords);
                    UpdateBindTexture(pass_index, _pass_variants[pass_index]._variant_hash);
                }
            }
            _all_keywords.erase(keyword);
            ++pass_index;
        }
    }

    void Material::RemoveTexture(const String &name)
    {
        for (auto &pass_tex: _textures_all_passes)
        {
            auto it = pass_tex.find(name);
            if (it == pass_tex.end())
            {
                return;
            }
            std::get<1>(pass_tex[name]) = nullptr;
        }
    }

    List<std::tuple<String, float>> Material::GetAllFloatValue()
    {
        u16 pass_index = 0;
        Map<String, f32> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferFloat && ShaderBindResourceInfo::GetVariableSize(bind_info) == 4 && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    if (!value_map.contains((name)))
                        value_map[name] = *reinterpret_cast<f32 *>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, float>> ret{};
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }

    List<std::tuple<String, Vector4f>> Material::GetAllVectorValue()
    {
        u16 pass_index = 0;
        Map<String, Vector4f> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferFloats && (ShaderBindResourceInfo::GetVariableSize(bind_info) == 16 || ShaderBindResourceInfo::GetVariableSize(bind_info) == 12) && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    bool three_dim = ShaderBindResourceInfo::GetVariableSize(bind_info) == 12;
                    Vector4f buf{};
                    if (three_dim)
                    {
                        auto value = *reinterpret_cast<Vector3f *>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                        buf = Vector4f(value.x, value.y, value.z, 1.0f);
                    }
                    else
                    {
                        buf = *reinterpret_cast<Vector4f *>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                    }
                    if (!value_map.contains((name)))
                        value_map[name] = buf;
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, Vector4f>> ret{};
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }
    List<std::tuple<String, Vector4Int>> Material::GetAllIntVectorValue()
    {
        u16 pass_index = 0;
        Map<String, Vector4Int> value_map{};
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) &&
                    ShaderBindResourceInfo::GetVariableSize(bind_info) == 16 && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    if (!value_map.contains((name)))
                    {
                        Vector4Int v;
                        if (bind_info._res_type & EBindResDescType::kCBufferUInts)
                        {
                            auto value = *reinterpret_cast<Vector4UInt *>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                            v.x = (i32) value.x;
                            v.y = (i32) value.y;
                            v.z = (i32) value.z;
                            v.w = (i32) value.w;
                            value_map[name] = v;
                        }
                        else if (bind_info._res_type & EBindResDescType::kCBufferInts)
                        {
                            v = *reinterpret_cast<Vector4Int *>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                            value_map[name] = v;
                        }
                    }
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, Vector4Int>> ret{};
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }

    List<std::tuple<String, u32>> Material::GetAllUintValue()
    {
        Map<String, u32> value_map{};
        u16 pass_index = 0;
        for (auto &pass: _pass_variants)
        {
            for (auto &[name, bind_info]: _p_shader->GetBindResInfo(pass_index, _pass_variants[pass_index]._variant_hash))
            {
                if (!ShaderBindResourceInfo::s_reversed_res_name.contains(name) && bind_info._res_type & EBindResDescType::kCBufferUInt && bind_info._bind_flag == ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    if (!value_map.contains((name)))
                        value_map[name] = *reinterpret_cast<u32 *>(_p_cbufs[pass_index]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info));
                }
            }
            ++pass_index;
        }
        List<std::tuple<String, u32>> ret{};
        for (auto &prop: _common_uint_property)
        {
            if (!value_map.contains(prop.first))
                value_map[prop.first] = prop.second;
        }
        for (auto &it: value_map)
        {
            ret.emplace_back(std::make_tuple(it.first, it.second));
        }
        return ret;
    }

    void Material::Construct(bool first_time)
    {
        AL_ASSERT(_p_shader->PassCount() != 0);
        _common_uint_property["_cull"] = (u32) _p_shader->GetCullMode();
        ConstructKeywords(_p_shader);
        static u8 s_unused_shader_prop_buf[256]{0};
        u16 unused_shader_prop_buf_offset = 0u;
        u16 pass_count = _p_shader->PassCount();
        //u16 cur_shader_cbuf_size = 0; //每个passcbuffer大小一致。
        Vector<u16> cbuf_size_per_passes(pass_count);
        if (!first_time)
        {
            _properties.clear();
        }
        else
        {
            _textures_all_passes.resize(pass_count);
            _mat_cbuf_per_pass_size.resize(pass_count);
            _p_cbufs.resize(pass_count);
        }
        _prop_views.clear();
        _properties.clear();
        _textures_all_passes.resize(pass_count);
        _mat_cbuf_per_pass_size.resize(pass_count);
        _p_cbufs.resize(pass_count);
        Vector<Map<String, std::tuple<u8, Texture *>>> _tmp_textures_all_passes(pass_count);
        for (int i = 0; i < pass_count; i++)
        {
            for (auto &bind_info: _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash))
            {
                if (bind_info.second._res_type == EBindResDescType::kTexture2D)
                {
                    auto tex_info = std::make_tuple(bind_info.second._bind_slot, nullptr);
                    auto &[slot, tex_ptr] = tex_info;
                    if (first_time)
                    {
                        _textures_all_passes[i].insert(std::make_pair(bind_info.second._name, tex_info));
                    }
                    else
                    {
                        _tmp_textures_all_passes[i].insert(std::make_pair(bind_info.second._name, tex_info));
                    }
                }
                else if (bind_info.second._res_type & EBindResDescType::kCBufferAttribute && bind_info.second._bind_flag & ShaderBindResourceInfo::kBindFlagPerMaterial)
                {
                    auto byte_size = ShaderBindResourceInfo::GetVariableSize(bind_info.second);
                    cbuf_size_per_passes[i] += byte_size;
                }
            }
        }

        if (!first_time)
        {
            for (int i = 0; i < pass_count; i++)
            {
                auto &tmp_textures = _tmp_textures_all_passes[i];
                auto &textures = _textures_all_passes[i];
                for (auto it = tmp_textures.begin(); it != tmp_textures.end(); it++)
                {
                    if (!textures.contains(it->first))
                    {
                        _textures_all_passes[i].insert(*it);
                    }
                }
                //_textures_all_passes[i] = std::move(_tmp_textures_all_passes[i]);
            }
        }
        for (int i = 0; i < pass_count; i++)
        {
            //处理cbuffer
            if (cbuf_size_per_passes[i] == 0)
                cbuf_size_per_passes[i] = 256;
            cbuf_size_per_passes[i] = ALIGN_TO_256(cbuf_size_per_passes[i]);
            AL_ASSERT(cbuf_size_per_passes[i] <= 256);
            if (first_time)
            {
                _mat_cbuf_per_pass_size[i] = cbuf_size_per_passes[i];
                _p_cbufs[i].reset(ConstantBuffer::Create(_mat_cbuf_per_pass_size[i]));
                memset(_p_cbufs[i]->GetData(), 0, _mat_cbuf_per_pass_size[i]);
            }
            else if (_mat_cbuf_per_pass_size[i] != cbuf_size_per_passes[i])
            {
                throw std::runtime_error("Material: " + _name + " shader cbuf size not equal!");
                LOG_ERROR("Material: " + _name + " shader cbuf size not equal!");
                //u8* new_cbuf_data = new u8[cur_shader_cbuf_size];
                //memcpy(new_cbuf_data, _p_cbuf_cpu, _mat_cbuf_size);
                //DESTORY_PTRARR(_p_cbuf_cpu);
                //_p_cbuf_cpu = new_cbuf_data;
            }
            else {}
            //处理纹理和属性
            auto &bind_info = _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
            auto &textures = _textures_all_passes[i];
            for (auto &prop_info: _p_shader->GetShaderPropertyInfos(i))
            {
                ShaderPropertyInfo cur_prop = prop_info;
                if (prop_info._type == EShaderPropertyType::kTexture2D)
                {
                    _properties.insert(std::make_pair(prop_info._value_name, cur_prop));
                    if (!first_time)
                    {
                        if (auto it = textures.find(prop_info._value_name); it != textures.end())
                        {
                            auto [slot, tex] = it->second;
                            _properties[prop_info._value_name]._value_ptr = tex;
                        }
                    }
                }
                else
                {
                    if (auto it = bind_info.find(prop_info._value_name); it != bind_info.end())
                    {
                        cur_prop = prop_info;
                        cur_prop._value_ptr = (void *) (_p_cbufs[i]->GetData() + ShaderBindResourceInfo::GetVariableOffset(bind_info.find(prop_info._value_name)->second));
                    }
                    else
                    {
                        _common_float_property[prop_info._value_name] = prop_info._default_value.z;
                        cur_prop._value_ptr = (void *) (&_common_float_property[prop_info._value_name]);
                    }
                    //memcpy(prop._param, prop_info._param, sizeof(Vector4f));
                    if (cur_prop._type == EShaderPropertyType::kFloat || cur_prop._type == EShaderPropertyType::kRange)
                    {
                        f32 *value = (f32 *) cur_prop._value_ptr;
                        if (*value == 0.0f)
                            *value = prop_info._default_value[0];
                    }
                    else if (cur_prop._type == EShaderPropertyType::kColor || cur_prop._type == EShaderPropertyType::kVector)
                    {
                        auto *value = (Vector4f *) cur_prop._value_ptr;
                        if (*value == Vector4f::kZero)
                            *value = prop_info._default_value;
                    }
                    else if (cur_prop._type == EShaderPropertyType::kBool || cur_prop._type == EShaderPropertyType::kEnum)
                    {
                        // f32 *value = (f32 *) cur_prop._value_ptr;
                        // if (*value == 0.0f)
                        //     *value = prop_info._param.z;
                    }
                    _properties.insert(std::make_pair(prop_info._value_name, cur_prop));
                }
            }
        }
        _render_queue = _p_shader->RenderQueue(0);
        for (auto &it: _properties)
        {
            _prop_views.emplace_back(&it.second);
        }
    }

    void Material::ConstructKeywords(Shader *shader)
    {
        _pass_variants.clear();
        for (u16 i = 0; i < shader->_passes.size(); i++)
        {
            auto &new_shader_pass = shader->_passes[i];
            ShaderVariantHash new_variant_hash;
            std::set<String> active_kws;
            new_variant_hash = shader->ConstructVariantHash(i, _all_keywords, active_kws);
            PassVariantInfo info;
            info._pass_name = new_shader_pass._name;
            info._keywords = std::move(active_kws);
            info._variant_hash = new_variant_hash;
            _pass_variants.emplace_back(info);
        }
    }

    void Material::UpdateBindTexture(u16 pass_index, ShaderVariantHash new_hash)
    {
        auto &bind_textures = _textures_all_passes[pass_index];
        auto &bind_infos = _p_active_shader->_passes[pass_index]._variants[_pass_variants[pass_index]._variant_hash]._bind_res_infos;
        for (auto it = bind_textures.begin(); it != bind_textures.end(); it++)
        {
            auto shader_bind_info_it = bind_infos.find(it->first);
            if (shader_bind_info_it != bind_infos.end())
            {
                auto &[slot, texture] = it->second;
                u8 new_slot = shader_bind_info_it->second._bind_slot;
                it->second = std::make_pair(new_slot, texture);
            }
        }
    }
    ShaderPropertyInfo *Material::GetShaderProperty(const String &name)
    {
        if (_properties.contains(name))
        {
            return &_properties[name];
        }
        return nullptr;
    }


    //-------------------------------------------StandardMaterial--------------------------------------------------------
    static void MarkTextureUsedHelper(u32 &mask, const ETextureUsage &usage, const bool &b_use)
    {
        switch (usage)
        {
            case Ailu::ETextureUsage::kAlbedo:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kAlbedo._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kAlbedo._mask_flag);
                break;
            case Ailu::ETextureUsage::kNormal:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kNormal._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kNormal._mask_flag);
                break;
            case Ailu::ETextureUsage::kEmission:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kEmission._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kEmission._mask_flag);
                break;
            case Ailu::ETextureUsage::kRoughness:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kRoughness._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kRoughness._mask_flag);
                break;
            case Ailu::ETextureUsage::kMetallic:
                mask = b_use ? mask | StandardMaterial::StandardPropertyName::kMetallic._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kMetallic._mask_flag);
                break;
            case Ailu::ETextureUsage::kSpecular:
                mask = b_use ? StandardMaterial::StandardPropertyName::kSpecular._mask_flag : mask & (~StandardMaterial::StandardPropertyName::kSpecular._mask_flag);
                break;
        }
    }
    StandardMaterial::StandardMaterial(String name) : Material(Shader::s_p_defered_standart_lit.lock().get(), name)
    {
    }
    StandardMaterial::~StandardMaterial()
    {
    }
    void StandardMaterial::MarkTextureUsed(std::initializer_list<ETextureUsage> use_infos, bool b_use)
    {
        //40 根据shader中MaterialBuf计算，可能会有变动
        u32 *sampler_mask = reinterpret_cast<u32 *>(_p_cbufs[_standard_pass_index]->GetData() + _sampler_mask_offset);
        //*sampler_mask = 0;
        for (auto &usage: use_infos)
        {
            MarkTextureUsedHelper(*sampler_mask, usage, b_use);
        }
    }

    bool StandardMaterial::IsTextureUsed(ETextureUsage use_info)
    {
        u32 sampler_mask = *reinterpret_cast<u32 *>(_p_cbufs[_standard_pass_index]->GetData() + _sampler_mask_offset);
        switch (use_info)
        {
            case Ailu::ETextureUsage::kAlbedo:
                return sampler_mask & StandardPropertyName::kAlbedo._mask_flag;
            case Ailu::ETextureUsage::kNormal:
                return sampler_mask & StandardPropertyName::kNormal._mask_flag;
            case Ailu::ETextureUsage::kEmission:
                return sampler_mask & StandardPropertyName::kEmission._mask_flag;
            case Ailu::ETextureUsage::kRoughness:
                return sampler_mask & StandardPropertyName::kRoughness._mask_flag;
            case Ailu::ETextureUsage::kMetallic:
                return sampler_mask & StandardPropertyName::kMetallic._mask_flag;
            case Ailu::ETextureUsage::kSpecular:
                return sampler_mask & StandardPropertyName::kSpecular._mask_flag;
        }
        return false;
    }
    void StandardMaterial::Bind(u16 pass_index)
    {
        Material::Bind(pass_index);
        if (_material_id_offset != 0)
        {
            if (_material_id == EMaterialID::kChecker)
            {
                u32 id = (u32) _material_id;
                memcpy(_p_cbufs[_standard_pass_index]->GetData() + _material_id_offset, &id, sizeof(u32));
            }
            //memset(_p_cbufs[_standard_pass_index]->GetData() + _material_id_offset, 2.0f, sizeof(u32));
            else
                memset(_p_cbufs[_standard_pass_index]->GetData() + _material_id_offset, 0, sizeof(u32));
        }
    }
    void StandardMaterial::MaterialID(const EMaterialID::EMaterialID &value)
    {
        _material_id = value;
        _common_uint_property["_MaterialID"] = (u32) _material_id;
    }
    void StandardMaterial::SurfaceType(const ESurfaceType::ESurfaceType &value)
    {
        if (_surface == value)
            return;
        _p_active_shader->RemoveMaterialRef(this);
        if (value == ESurfaceType::kOpaque)
        {
            _p_active_shader = _p_shader;
            _render_queue = Shader::kRenderQueueOpaque;
            DisableKeyword("ALPHA_TEST");
        }
        else if (value == ESurfaceType::kTransparent)
        {
            DisableKeyword("ALPHA_TEST");
            _p_active_shader = g_pResourceMgr->Get<Shader>(L"Shaders/forwardlit.alasset");
            _render_queue = Shader::kRenderQueueTransparent;
        }
        else if (value == ESurfaceType::kAlphaTest)
        {
            if (_p_active_shader != _p_shader)
                _p_active_shader = _p_shader;
            EnableKeyword("ALPHA_TEST");
            _render_queue = Shader::kRenderQueueAlphaTest;
        }
        _p_active_shader->AddMaterialRef(this);
        _surface = value;
        _common_uint_property[kSurfaceKey] = _surface;
    }
    void StandardMaterial::SetTexture(const String &name, Texture *texture)
    {
        Material::SetTexture(name, texture);
        bool use_tex = texture != nullptr;
        if (name == StandardPropertyName::kAlbedo._tex_name) MarkTextureUsed({ETextureUsage::kAlbedo}, use_tex);
        else if (name == StandardPropertyName::kEmission._tex_name)
            MarkTextureUsed({ETextureUsage::kEmission}, use_tex);
        else if (name == StandardPropertyName::kRoughness._tex_name)
            MarkTextureUsed({ETextureUsage::kRoughness}, use_tex);
        else if (name == StandardPropertyName::kMetallic._tex_name)
            MarkTextureUsed({ETextureUsage::kMetallic}, use_tex);
        else if (name == StandardPropertyName::kSpecular._tex_name)
            MarkTextureUsed({ETextureUsage::kSpecular}, use_tex);
        else if (name == StandardPropertyName::kNormal._tex_name)
            MarkTextureUsed({ETextureUsage::kNormal}, use_tex);
        else {};
        for (auto &pass_tex: _textures_all_passes)
        {
            auto it = pass_tex.find(name);
            if (it == pass_tex.end())
            {
                return;
            }
            std::get<1>(pass_tex[name]) = texture;
            if (_properties.find(name) != _properties.end())
            {
                _properties[name]._value_ptr = reinterpret_cast<void *>(texture);
            }
        }
        //else
        //	LOG_WARNING("Cann't find texture prop with value name: {} when set material {} texture!", name, _name);
    }

    void StandardMaterial::SetTexture(const String &name, const WString &texture_path)
    {
        auto texture = g_pResourceMgr->Get<Texture2D>(texture_path);
        if (texture == nullptr)
        {
            g_pLogMgr->LogErrorFormat("Cann't find texture: {} when set material {} texture{}!", ToChar(texture_path), _name, name);
            return;
        }
        SetTexture(name, texture);
    }

    void StandardMaterial::SetTexture(const String &name, RTHandle texture)
    {
        auto raw_texture = g_pRenderTexturePool->Get(texture);
        SetTexture(name, raw_texture);
    }
    const Texture *StandardMaterial::MainTex(ETextureUsage usage) const
    {
        auto &info = StandardPropertyName::GetInfoByUsage(usage);
        auto &prop = _properties.at(info._tex_name);
        AL_ASSERT(prop._type == EShaderPropertyType::kTexture2D);
        return reinterpret_cast<Texture *>(prop._value_ptr);
    }
    const ShaderPropertyInfo &StandardMaterial::MainProperty(ETextureUsage usage)
    {
        auto &info = StandardPropertyName::GetInfoByUsage(usage);
        return _properties[info._value_name];
    }
    void StandardMaterial::SetTexture(ETextureUsage usage, Texture *tex)
    {
        MarkTextureUsed({usage}, tex != nullptr);
        auto &info = StandardPropertyName::GetInfoByUsage(usage);
        SetTexture(info._tex_name, tex);
    }
    void StandardMaterial::Construct(bool first_time)
    {
        Material::Construct(first_time);
        //只有首个pass支持默认着色
        for (i16 i = 0; i < _p_shader->PassCount(); i++)
        {
            auto &cur_variant_bind_infos = _p_shader->GetBindResInfo(i, _pass_variants[i]._variant_hash);
            auto it = cur_variant_bind_infos.find("_SamplerMask");
            if (it != cur_variant_bind_infos.end())
            {
                _sampler_mask_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                _standard_pass_index = i;
            }
            it = cur_variant_bind_infos.find("_MaterialID");
            if (it != cur_variant_bind_infos.end())
            {
                _material_id_offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
            }
            if (_standard_pass_index != -1)
                break;
        }
        _material_id = (EMaterialID::EMaterialID) _common_uint_property["_MaterialID"];
        _surface = (ESurfaceType::ESurfaceType) _common_uint_property["_surface"];
    }
    //-------------------------------------------StandardMaterial--------------------------------------------------------
}// namespace Ailu
