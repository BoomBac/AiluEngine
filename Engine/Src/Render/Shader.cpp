#include "pch.h"
#include <regex>

#include "Framework/Common/Hash.hpp"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/SystemInfo.h"
#include "RHI/DX12/D3DShader.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/RenderQueue.h"
#include "Render/Renderer.h"
#include "Render/Shader.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu::Render
{
#pragma region Utils
    static List<String> ReadFileToLines(const String &sys_path, u32 &line_count, String begin = "", String end = "")
    {
        std::ifstream file(sys_path);
        List<String> lines{};
        AL_ASSERT(file.is_open());
        String line;
        bool start = begin.empty(), stop = false;
        while (std::getline(file, line))
        {
            line = StringUtils::Trim(line);
            if (!start)
            {
                start = line == begin;
                continue;
            }
            if (!stop && !end.empty())
            {
                stop = end == line;
            }
            if (start && !stop)
            {
                lines.emplace_back(line);
            }
        }
        file.close();
        line_count = static_cast<u32>(lines.size());
        return lines;
    }
    static ECompareFunc StringToCompareFunc(const String &str,bool is_z_test)
    {
        if (su::Equal(str, ShaderCommand::Comparison::kNevel, false))
            return ECompareFunc::kNever;
        if (su::Equal(str, ShaderCommand::Comparison::kLess, false))
        {
            if (is_z_test)
                return SystemInfo::kReverseZ? ECompareFunc::kGreater : ECompareFunc::kLess;
            else
                return ECompareFunc::kLess;
        }
        if (su::Equal(str, ShaderCommand::Comparison::kEqual, false))
            return ECompareFunc::kEqual;
        if (su::Equal(str, ShaderCommand::Comparison::kLEqual, false))
        {
            if (is_z_test)
                return SystemInfo::kReverseZ? ECompareFunc::kGreaterEqual : ECompareFunc::kLessEqual;
            else
                return ECompareFunc::kLessEqual;
        }
        if (su::Equal(str, ShaderCommand::Comparison::kGreater, false))
        {
            if (is_z_test)
                return SystemInfo::kReverseZ? ECompareFunc::kLess : ECompareFunc::kGreater;
            else
                return ECompareFunc::kGreater;
        }
        if (su::Equal(str, ShaderCommand::Comparison::kNotEqual, false))
            return ECompareFunc::kNotEqual;
        if (su::Equal(str, ShaderCommand::Comparison::kGEqual, false))
        {
            if (is_z_test)
                return SystemInfo::kReverseZ? ECompareFunc::kLessEqual : ECompareFunc::kGreaterEqual;
            else
                return ECompareFunc::kGreaterEqual;
        }
        if (su::Equal(str, ShaderCommand::Comparison::kAlways, false) || su::Equal(str, ShaderCommand::Comparison::kOff, false))
            return ECompareFunc::kAlways;
        AL_ASSERT(false);
        return ECompareFunc::kNever;
    }
#pragma endregion

#pragma region ShaderVariantMgr
    void ShaderVariantMgr::BuildIndex(const Vector<std::set<String>> &all_keywords)
    {
        _keyword_group_ids.clear();
        u16 global_index = 0;
        for (u16 i = 0; i < all_keywords.size(); ++i)
        {
            u16 inner_group_id = 0u;
            for (auto &kw: all_keywords[i])
            {
                if (kw.empty() || kw == "_")
                    continue;
                _keyword_group_ids[kw] = KeywordInfo{i, inner_group_id++, global_index++};
            }
        }
    }
    ShaderVariantHash ShaderVariantMgr::GetVariantHash(const std::set<String> &keywords) const
    {
        std::set<String> active_kw_seq;
        return GetVariantHash(keywords, active_kw_seq);
    }
    ShaderVariantHash ShaderVariantMgr::GetVariantHash(const std::set<String> &keywords, std::set<String> &active_kw_seq) const
    {
        ShaderVariantBits bits(_keyword_group_ids.size() / kBitNumPerGroup + 1);
        std::set<u16> group_actived{};
        for (auto &kw: keywords)
        {
            if (auto it = _keyword_group_ids.find(kw); it != _keyword_group_ids.end())
            {
                auto &info = it->second;
                if (!group_actived.contains(info._group_id))
                {
                    u16 bit_group_id = info._global_id / kBitNumPerGroup;
                    bits[bit_group_id] |= static_cast<unsigned long long>(1) << (info._global_id % kBitNumPerGroup);
                    active_kw_seq.insert(it->first);
                    group_actived.insert(info._group_id);
                }
            }
        }
        ShaderVariantHash hash = 0u;
        for (u32 mask: bits)
        {
            hash ^= mask == 0 ? 0 : std::hash<u32>()(mask);
        }
        return hash;
    }
    std::set<String> ShaderVariantMgr::KeywordsSameGroup(const String &kw) const
    {
        std::set<String> res{};
        if (auto it = _keyword_group_ids.find(kw); it != _keyword_group_ids.end())
        {
            u16 group_id = it->second._group_id;
            for (auto &it: _keyword_group_ids)
            {
                if (it.second._group_id == group_id)
                    res.insert(it.first);
            }
        }
        return res;
    }
#pragma endregion
    //------------------------------------------------------------------ShaderVariantMgr--------------------------------------------------------------------------------
    std::vector<std::vector<std::string>> ExtractPassBlocks(const std::list<std::string> &lines)
    {
        std::vector<std::vector<std::string>> passBlocks;
        auto it = lines.begin();
        while (it != lines.end())
        {
            // 查找 pass begin::
            auto passBeginIt = std::find(it, lines.end(), "//pass begin::");
            if (passBeginIt == lines.end())
                break;// 找不到则退出循环

            // 查找 pass end::
            auto passEndIt = std::find(passBeginIt, lines.end(), "//pass end::");
            if (passEndIt == lines.end())
                break;// 找不到则退出循环

            // 提取 pass 区块
            std::vector<std::string> passBlock(++passBeginIt, passEndIt);
            passBlocks.push_back(passBlock);

            // 更新迭代器，以便继续搜索下一个 pass 区块
            it = passEndIt;
        }

        return passBlocks;
    }
#pragma region Shader

    std::set<String> Shader::s_predefined_macros = 
    {
#if defined(_REVERSED_Z)
        "_REVERSED_Z",
#endif
    };
    Ref<Shader> Shader::Create(const WString &sys_path)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                return AdoptGpuResource(new RHI::DX12::D3DShader(sys_path));
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }

    ShaderHash Shader::ConstructHash(const u32 &shader_id, const u16 &pass_hash, ShaderVariantHash variant_hash)
    {
        const static u32 kMaxShaderID = 1u << ShaderHashStruct::kShader._size;
        const static u32 kMaxPassHash = 1u << ShaderHashStruct::kPass._size;
        const static u32 kMaxVariantHash = UINT32_MAX;
        ALHash::Hash<64> shader_hash;
        AL_ASSERT(shader_id < kMaxShaderID);
        AL_ASSERT(pass_hash < kMaxPassHash);
        AL_ASSERT(variant_hash < kMaxVariantHash);
        shader_hash.Set(ShaderHashStruct::kShader._pos,ShaderHashStruct::kShader._size,shader_id);
        shader_hash.Set(ShaderHashStruct::kPass._pos, ShaderHashStruct::kPass._size, pass_hash);
        shader_hash.Set(ShaderHashStruct::kVariant._pos, ShaderHashStruct::kVariant._size, variant_hash);
        return shader_hash.Get(0, 64);
    }
    void Shader::ExtractInfoFromHash(const ShaderHash &shader_hash, u32 &shader_id, u16 &pass_hash, ShaderVariantHash &variant_hash)
    {
        ALHash::Hash<64> hash(shader_hash);
        shader_id = (u32) hash.Get(ShaderHashStruct::kShader._pos,ShaderHashStruct::kShader._size);
        pass_hash = (u16) hash.Get(ShaderHashStruct::kPass._pos, ShaderHashStruct::kPass._size);
        variant_hash = (u32)hash.Get(ShaderHashStruct::kVariant._pos, ShaderHashStruct::kVariant._size);
    }

    u32 Shader::GetShaderState(Shader *shader, u16 pass_index, ShaderVariantHash variant_hash)
    {
        auto &state = shader->_variant_state[pass_index][variant_hash];
        if (state == EShaderVariantState::kError)
        {
            return 4000;
        }
        else if (state == EShaderVariantState::kCompiling)
        {
            return 4001;
        }
        else
        {
            return 0;
        }
    }

    Shader::Shader(const WString &sys_path)
    {
        
        _src_file_path = sys_path;
        _name = ToChar(PathUtils::GetFileName(sys_path));
        PreProcessShader();
        for (auto &p: _passes)
        {
            p._vert_src_file = _src_file_path;
            p._pixel_src_file = _src_file_path;
            p._geom_src_file = _src_file_path;
        }
        _id = s_global_shader_unique_id++;
        s_all_shaders.insert(this);
    }

    void Shader::Bind(RHICommandBuffer *cmd, u16 pass_index, ShaderVariantHash variant_hash)
    {
        const auto& active_variant = _passes[pass_index]._variants[variant_hash];
        auto &recording_context = cmd->RecordingContext();
        recording_context.ConfigureVertexInputLayout(active_variant._pipeline_input_layout.Hash());
        recording_context.ConfigureRasterizerState(_passes[pass_index]._pipeline_raster_state.Hash());
        recording_context.ConfigureDepthStencilState(_passes[pass_index]._pipeline_ds_state.Hash());
        recording_context.ConfigureTopology(static_cast<u8>(ALHash::HashFunc(_passes[pass_index]._pipeline_topology)));
        recording_context.ConfigureBlendState(_passes[pass_index]._pipeline_blend_state.Hash());
        recording_context.ConfigureShader(this, pass_index, variant_hash, active_variant._shader_hash);
        _topology = _passes[pass_index]._pipeline_topology;
    }

    void Shader::BuildBindingLayout(u16 pass_index, ShaderVariantHash variant_hash)
    {
        AL_ASSERT(pass_index < _passes.size());
        auto &variant = _passes[pass_index]._variants.at(variant_hash);
        if (variant._binding_layout)
            _retired_binding_layouts.emplace_back(variant._binding_layout);
        auto layout = MakeRef<ShaderBindingLayout>();
        layout->Version(_binding_layout_version++);
        for (const auto &[name, bind_info] : variant._bind_res_infos)
        {
            ShaderPropertyBinding binding;
            binding._property_id = bind_info._property_id == kInvalidShaderPropertyId
                                       ? ShaderPropertyRegistry::Get().Intern(name)
                                       : bind_info._property_id;
            binding._resource_type = bind_info._res_type;
            binding._bind_slot = bind_info._bind_slot;
            binding._register_space = bind_info._register_space;
            binding._bind_flag = bind_info._bind_flag;
            if (bind_info._res_type & EBindResDescType::kCBufferAttribute)
            {
                binding._buffer_offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                binding._buffer_size = ShaderBindResourceInfo::GetVariableSize(bind_info);
            }
            layout->Add(binding);
        }
        variant._binding_layout = layout;
    }

    bool Shader::Compile(u16 pass_id, ShaderVariantHash variant_hash, bool is_load_cache)
    {
        //LOG_INFO(L"Begin compile shader: {},pass: {},variant: {} with keywords {}...", _src_file_path, pass_id, variant_hash, ToWChar(su::Join(ActiveKeywords(pass_id, variant_hash), ",")));
        TimeMgr::Get().Mark();
        AL_ASSERT(pass_id < _passes.size());
        auto &pass = _passes[pass_id];
        AL_ASSERT(pass._variants.contains(variant_hash));
        _variant_state[pass_id][variant_hash] = EShaderVariantState::kCompiling;
        String variant_str = std::format("{}_{}_{},keywords: {}", _name, pass_id, variant_hash, su::Join(ActiveKeywords(pass_id, variant_hash), ","));
        if (RHICompileImpl(pass_id, variant_hash, is_load_cache))
        {
            pass._variants[variant_hash]._pipeline_input_layout.Hash(PipelineStateHash<VertexInputLayout>::GenHash(pass._variants[variant_hash]._pipeline_input_layout));
            pass._pipeline_raster_state.Hash(PipelineStateHash<RasterizerState>::GenHash(pass._pipeline_raster_state));
            pass._pipeline_blend_state.Hash(PipelineStateHash<BlendState>::GenHash(pass._pipeline_blend_state));
            pass._pipeline_ds_state.Hash(PipelineStateHash<DepthStencilState>::GenHash(pass._pipeline_ds_state));
            pass._variants[variant_hash]._shader_hash = ConstructHash(_id, pass_id, variant_hash);
            GraphicsPipelineStateMgr::Get().OnShaderCompiled(this, pass_id, variant_hash);
            _variant_state[pass_id][variant_hash] = EShaderVariantState::kReady;
            LOG_INFO("Shader({}) compile success with {}ms", variant_str, TimeMgr::Get().GetElapsedSinceLastMark());
        }
        else
        {
            _variant_state[pass_id][variant_hash] = EShaderVariantState::kError;
            LOG_ERROR("Shader({}) compile failed with {}ms", variant_str, TimeMgr::Get().GetElapsedSinceLastMark());
        }
        return _variant_state[pass_id][variant_hash] == EShaderVariantState::kReady;
    }

    bool Shader::Compile(bool is_load_cache)
    {
        u16 pass_index = 0;
        u32 variant_index = 0;
        u32 error_variant_count = 0;
        for (auto &pass: _passes)
        {
            for (auto &variant_it: pass._variants)
            {
                auto &[vhash, kw_seq] = variant_it;
                if (!Compile(pass_index, vhash, is_load_cache))
                    ++error_variant_count;
                ++variant_index;
            }
            ++pass_index;
        }
        return error_variant_count == 0;
    }

    void Shader::SetGlobalTexture(const String &name, Texture *texture)
    {
        if (!RDG::RenderGraph::ValidatePersistentGlobalResource(texture, "Shader::SetGlobalTexture"))
            return;
        auto id = ShaderPropertyRegistry::Get().Intern(name);
        s_global_res_registry.SetTexture(id,texture);
    }
    void Shader::SetGlobalTexture(const String &name, RTHandle handle)
    {
        auto tex = g_pRenderTexturePool->Get(handle);
        SetGlobalTexture(name, tex);
    }
    void Shader::SetGlobalMatrix(const String &name, Matrix4x4f *matrix)
    {
        if (!s_global_matrix_bind_info.contains(name))
            s_global_matrix_bind_info.insert(std::make_pair(name, std::make_tuple(matrix, 1)));
        else
            s_global_matrix_bind_info[name] = std::make_tuple(matrix, 1);
    }

    void Shader::SetGlobalMatrixArray(const String &name, Matrix4x4f *matrix, u32 num)
    {
        if (!s_global_matrix_bind_info.contains(name))
            s_global_matrix_bind_info.insert(std::make_pair(name, std::make_tuple(matrix, num)));
        else
            s_global_matrix_bind_info[name] = std::make_tuple(matrix, num);
    }


    void Shader::EnableGlobalKeyword(const String &keyword)
    {
        for (auto s: s_all_shaders)
        {
            if (s)
            {
                bool vaild_kw = false;
                for (auto &pass: s->_passes)
                {
                    if (ShaderPass::IsPassKeyword(pass, keyword))
                    {
                        vaild_kw = true;
                        break;
                    }
                }
                if (vaild_kw)
                {
                    for (auto mat: s->_reference_mats)
                    {
                        if (mat)
                            mat->EnableKeyword(keyword);
                    }
                }
            }
        }
    }
    void Shader::DisableGlobalKeyword(const String &keyword)
    {
        for (auto s: s_all_shaders)
        {
            if (s)
            {
                bool vaild_kw = false;
                for (auto &pass: s->_passes)
                {
                    if (ShaderPass::IsPassKeyword(pass, keyword))
                    {
                        vaild_kw = true;
                        break;
                    }
                }
                for (auto mat: s->_reference_mats)
                {
                    if (mat)
                        mat->DisableKeyword(keyword);
                }
            }
        }
    }
    void Shader::SetGlobalBuffer(const String &name, ConstantBuffer *buffer)
    {
        auto id = ShaderPropertyRegistry::Get().Intern(name);
        s_global_res_registry.SetBuffer(id,buffer);
    }
    void Shader::SetGlobalBuffer(const String &name, GPUBuffer *buffer)
    {
        auto id = ShaderPropertyRegistry::Get().Intern(name);
        s_global_res_registry.SetBuffer(id, buffer);
    }


    void *Shader::GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash)
    {
        return nullptr;
    }

    void Shader::SetVertexShader(u16 pass_index, const WString &sys_path, const String &entry)
    {
        AL_ASSERT(pass_index < _passes.size());
        _passes[pass_index]._vert_entry = entry;
        _passes[pass_index]._vert_src_file = sys_path;
    }
    void Shader::SetPixelShader(u16 pass_index, const WString &sys_path, const String &entry)
    {
        AL_ASSERT(pass_index < _passes.size());
        _passes[pass_index]._pixel_entry = entry;
        _passes[pass_index]._pixel_src_file = sys_path;
    }
    i16 Shader::FindPass(String pass_name)
    {
        auto it = std::find_if(_passes.begin(), _passes.end(), [&](const ShaderPass &shader_pass) -> bool
                               { return pass_name == shader_pass._name; });
        return it != _passes.end() ? (i16) it->_index : -1;
    }

    ShaderVariantHash Shader::ConstructVariantHash(u16 pass_index, const std::set<String> &kw_seq) const
    {
        std::set<String> active_kw_seq;
        return ConstructVariantHash(pass_index, kw_seq, active_kw_seq);
    }

    ShaderVariantHash Shader::ConstructVariantHash(u16 pass_index, const std::set<String> &kw_seq, std::set<String> &active_kw_seq) const
    {
        AL_ASSERT(pass_index < _passes.size());
        return _passes[pass_index]._variant_mgr.GetVariantHash(kw_seq, active_kw_seq);
        //ShaderVariantHash hash{ 0 };
        // Math::ALHash::Hash<32> hash;
        // auto &kw_index_info = _passes[pass_index]._keywords_ids;
        //for (auto &kw: kw_seq)
        //{
        //    auto it = kw_index_info.find(kw);
        //    if (it != kw_index_info.end())
        //    {
        //        auto &[group_id, inner_id] = it->second;
        //        hash.Set(group_id * 3, 3, inner_id);
        //        active_kw_seq.insert(it->first);
        //    }
        //}
        //ShaderVariantHash hash_value = hash.Get(0, 32);
        //return hash_value;
    }

    bool Shader::IsKeywordValid(u16 pass_index, const String &kw) const
    {
        return std::find_if(_passes[pass_index]._keywords.begin(), _passes[pass_index]._keywords.end(), [&](const auto &kws) -> bool
                            { return kws.contains(kw); }) != _passes[pass_index]._keywords.end();
    }

    std::set<String> Shader::KeywordsSameGroup(u16 pass_index, const String &kw) const
    {
        AL_ASSERT(pass_index < _passes.size());
        return _passes[pass_index]._variant_mgr.KeywordsSameGroup(kw);
    }

    Vector<Material *> Shader::GetAllReferencedMaterials()
    {
        Vector<Material *> ret;
        ret.reserve(_reference_mats.size());
        for (auto it = _reference_mats.begin(); it != _reference_mats.end(); it++)
            ret.emplace_back(*it);
        return ret;
    }
    void Shader::SetCullMode(ECullMode mode)
    {
        u16 pass_index = 0;
        for (auto &pass: _passes)
        {
            if (pass_index++ > 1)//只影响前两个pass，一般为着色和阴影，特殊pass的cull由shader中指定
                break;
            pass._pipeline_raster_state._cull_mode = mode;
            pass._pipeline_raster_state.Hash(pass._pipeline_raster_state._s_hash_obj.GenHash(pass._pipeline_raster_state));
        }
    }
    ECullMode Shader::GetCullMode() const
    {
        AL_ASSERT(!_passes.empty());
        return _passes[0]._pipeline_raster_state._cull_mode;
    }
    void Shader::AddMaterialRef(Material *mat)
    {
        _reference_mats.insert(mat);
    }
    void Shader::RemoveMaterialRef(Material *mat)
    {
        _reference_mats.erase(mat);
    }
    bool Shader::RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash, bool is_load_cache)
    {
        return true;
    }
    void Shader::ParserShaderProperty(String &line, List<ShaderPropertyInfo> &props)
    {
        line = su::Trim(line);
        String addi_info{};
        if (line.find("[") != line.npos)
        {
            auto begin = line.find("["), end = line.find("]");
            addi_info = su::SubStrRange(line, begin + 1, end - 1);
            line = line.substr(end + 1);
            line = su::Trim(line);
        }
        auto cur_edge = line.find_first_of("(");
        String value_name = line.substr(0, cur_edge);
        line = line.substr(cur_edge);
        cur_edge = line.find_first_of("=");
        String defalut_value = line.substr(cur_edge + 1);
        line = line.substr(0, cur_edge - 1);
        su::RemoveSpaces(defalut_value);
        line = line.substr(1, line.find_last_of(")") - 1);
        cur_edge = line.find_first_of(",");
        String prop_name = line.substr(1, cur_edge - 2);
        String prop_type = line.substr(cur_edge + 1);
        EShaderPropertyType seri_type;
        Vector4f prop_default_value{}, prop_params{};
        if (prop_type == "Texture2D") seri_type = EShaderPropertyType::kTexture2D;
        else if (prop_type == "Color")
            seri_type = EShaderPropertyType::kColor;
        else if (*(prop_type.begin()) == *"R")
            seri_type = EShaderPropertyType::kRange;
        else if (prop_type == "Float")
            seri_type = EShaderPropertyType::kFloat;
        else if (prop_type == "Vector")
            seri_type = EShaderPropertyType::kVector;
        else if (prop_type == ShaderPropertyType::Texture3D)
            seri_type = EShaderPropertyType::kTexture3D;
        else
            seri_type = EShaderPropertyType::kUndefined;
        if (!addi_info.empty())
        {
            if (addi_info.starts_with("Toggle"))
            {
                seri_type = EShaderPropertyType::kBool;
                _keywords_props[value_name].emplace_back(value_name + "_OFF");
                _keywords_props[value_name].emplace_back(value_name + "_ON");
            }
            else if (addi_info.starts_with("Enum"))
            {
                seri_type = EShaderPropertyType::kEnum;
                auto enum_strs = su::Split(su::SubStrRange(addi_info, addi_info.find("(") + 1, addi_info.find(")") - 1), ",");
                for (size_t i = 0; i < enum_strs.size(); i += 2)
                {
                    _keywords_props[value_name].emplace_back(value_name + "_" + enum_strs[i]);
                }
                // _passshared_keywords[_value_name].resize(enum_strs.size() / 2);
                // for (size_t i = 0; i < enum_strs.size(); i += 2)
                // {
                // 	_passshared_keywords[_value_name][std::stoi(enum_strs[i + 1])] = _value_name + "_" + enum_strs[i];
                // }
            }
        }
        if (seri_type == EShaderPropertyType::kRange)
        {
            cur_edge = prop_type.find_first_of(",");
            size_t left_bracket = prop_type.find_first_of("(");
            size_t right_bracket = prop_type.find_first_of(")");
            prop_default_value.x = static_cast<float>(std::stod(su::SubStrRange(prop_type, left_bracket + 1, cur_edge - 1)));
            prop_default_value.y = static_cast<float>(std::stod(su::SubStrRange(prop_type, cur_edge + 1, right_bracket - 1)));
            prop_default_value.z = static_cast<float>(std::stod(defalut_value));
        }
        else if (seri_type == EShaderPropertyType::kFloat || seri_type == EShaderPropertyType::kBool)
        {
            prop_default_value.z = static_cast<float>(std::stod(defalut_value));
        }
        else if (seri_type == EShaderPropertyType::kColor || seri_type == EShaderPropertyType::kVector)
        {
            defalut_value = su::SubStrRange(defalut_value, 1, defalut_value.find_first_of(")") - 1);
            auto vec_str = su::Split(defalut_value, ",");
            prop_default_value.x = static_cast<float>(std::stod(vec_str[0]));
            prop_default_value.y = static_cast<float>(std::stod(vec_str[1]));
            prop_default_value.z = static_cast<float>(std::stod(vec_str[2]));
            prop_default_value.w = static_cast<float>(std::stod(vec_str[3]));
            if (addi_info.starts_with("HDR"))
            {
                prop_params.x = 1;
                prop_params.y = 1;
            }
        }
        if (seri_type != EShaderPropertyType::kUndefined)
        {
            props.emplace_back(value_name, prop_name, seri_type, prop_default_value);
            props.back()._params = prop_params;
        }
        else
        {
            //LogMgr::Get().LogWarningFormat("Undefined shader property type with name {}", prop_name);
        }
    }

    //void Shader::ExtractValidShaderProperty(u16 pass_index, ShaderVariantHash variant_hash)
    //{
    //	auto it = _passes[pass_index]._shader_prop_infos.begin();
    //	while (it != _passes[pass_index]._shader_prop_infos.end())
    //	{
    //		if (!_passes[pass_index]._bind_res_infos[variant_hash].contains(it->_value_name))
    //		{
    //			it = _passes[pass_index]._shader_prop_infos.erase(it);
    //		}
    //		else
    //			it++;
    //	}
    //}

    //这里后台线程编译时会影响主线程对其的访问
    bool Shader::PreProcessShader()
    {
        bool is_process_succeed = true;
        List<String> lines{};
        String line{};
        u32 line_count = 0;
        auto sys_path = ToChar(_src_file_path);
        lines = ReadFileToLines(sys_path, line_count, "//info bein", "//info end");
        if (line_count > 0)
        {
            auto pass_blocks = ExtractPassBlocks(lines);
            _passes.resize(pass_blocks.size());
            _keywords_props.clear();
            for (auto &pass: _passes)
                pass._shader_prop_infos.clear();
            for (int i = 0; i < pass_blocks.size(); i++)
            {
                ShaderPass cur_pass;
                String k, v, blend_info;
                bool is_transparent = false;
                RasterizerState pass_pipeline_raster_state;
                DepthStencilState pass_pipeline_ds_state;
                ETopology pass_pipeline_topology = ETopology::kTriangle;
                BlendState pass_pipeline_blend_state;
                u8 need_zbuf_count = 2;
                u32 pass_render_queue = kRenderQueueOpaque;
                //Pass基本信息和管线状态信息
                for (auto it = pass_blocks[i].begin(); it != pass_blocks[i].end(); it++)
                {
                    line = it->substr(2);
                    k = line.substr(0, line.find_first_of(":"));
                    su::RemoveSpaces(k);
                    v = line.substr(line.find_first_of(":") + 1);
                    su::RemoveSpaces(v);
                    if (su::Equal(k, ShaderCommand::kName, false))
                    {
                        cur_pass._name = v;
                    }
                    else if (su::Equal(k, ShaderCommand::kVSEntry, false))
                    {
                        cur_pass._vert_entry = v;
                    }
                    else if (su::Equal(k, ShaderCommand::kPSEntry, false))
                    {
                        cur_pass._pixel_entry = v;
                    }
                    else if (su::Equal(k, ShaderCommand::kGSEntry, false))
                        cur_pass._geometry_entry = v;
                    else if (su::Equal(k, ShaderCommand::kCull, false))
                    {
                        if (su::Equal(v, ShaderCommand::kCullValue.kBack, false))
                        {
                            pass_pipeline_raster_state._cull_mode = ECullMode::kBack;
                        }
                        else if (su::Equal(v, ShaderCommand::kCullValue.kFront, false))
                        {
                            pass_pipeline_raster_state._cull_mode = ECullMode::kFront;
                        }
                        else
                        {
                            pass_pipeline_raster_state._cull_mode = ECullMode::kNone;
                        }
                    }
                    else if (su::Equal(k, ShaderCommand::kTopology, false))
                    {
                        if (su::Equal(v, ShaderCommand::kTopologyValue.kLine))
                        {
                            pass_pipeline_topology = ETopology::kLine;
                        }
                        else if (su::Equal(v, ShaderCommand::kTopologyValue.kTriangleStrip))
                        {
                            pass_pipeline_topology = ETopology::kTriangleStrip;
                        }
                        _topology = pass_pipeline_topology;
                    }
                    else if (su::Equal(k, ShaderCommand::kBlend, false))
                    {
                        blend_info = v;
                    }
                    else if (su::Equal(k, ShaderCommand::kConservative, false))
                    {
                        if (su::Equal(v, ShaderCommand::kConservativeValue.kOn, false))
                            pass_pipeline_raster_state._is_conservative = true;
                    }
                    else if (su::Equal(k, ShaderCommand::kQueue, false))
                    {
                        if (su::Equal(v, ShaderCommand::kQueueValue.kTransplant, false))
                        {
                            pass_render_queue = kRenderQueueTransparent;
                            is_transparent = true;
                        }
                    }
                    else if (su::Equal(k, ShaderCommand::KFill, false))
                    {
                        if (su::Equal(v, ShaderCommand::kFillValue.kWireframe))
                        {
                            pass_pipeline_raster_state._fill_mode = EFillMode::kWireframe;
                        }
                    }
                    else if (su::Equal(k, ShaderCommand::kZTest, false))
                    {
                        pass_pipeline_ds_state._depth_test_func = StringToCompareFunc(v,true);
                        if (pass_pipeline_ds_state._depth_test_func == ECompareFunc::kAlways)
                            need_zbuf_count--;
                    }
                    else if (su::Equal(k, ShaderCommand::kZWrite, false))
                    {
                        if (su::Equal(v, ShaderCommand::kZWriteValue.kOff))
                        {
                            need_zbuf_count--;
                            pass_pipeline_ds_state._b_depth_write = false;
                        }
                    }
                    else if (su::Equal(k, ShaderCommand::kStencil, false))
                    {
                        pass_pipeline_ds_state._b_front_stencil = true;
                        //v = {Ref 5,Comp Always,Pass Replace}
                        std::regex regex("\\{([^}]*)\\}");
                        std::smatch match;
                        if (std::regex_search(v, match, regex))
                        {
                            String extracted = match[1].str();
                            Map<String, String> stencil_values;
                            std::stringstream ss(extracted);
                            String item;
                            while (std::getline(ss, item, ','))
                            {
                                std::stringstream item_ss(item);
                                std::string key, value;
                                std::getline(item_ss, key, ':');
                                std::getline(item_ss, value, ':');
                                stencil_values[key] = value;
                            }
                            if (!stencil_values.empty())
                            {
                                if (stencil_values.contains(ShaderCommand::kStencilStateValue.kStencilRef))
                                {
                                    pass_pipeline_ds_state._stencil_ref_value = std::stoi(stencil_values[ShaderCommand::kStencilStateValue.kStencilRef]);
                                }
                                if (stencil_values.contains(ShaderCommand::kStencilStateValue.kStencilComp))
                                {
                                    auto &comp_str = stencil_values[ShaderCommand::kStencilStateValue.kStencilComp];
                                    pass_pipeline_ds_state._stencil_test_func = StringToCompareFunc(comp_str,false);
                                }
                                if (stencil_values.contains(ShaderCommand::kStencilStateValue.kStencilPass))
                                {
                                    auto &pass_str = stencil_values[ShaderCommand::kStencilStateValue.kStencilPass];
                                    if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kReplace, false))
                                    {
                                        pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kReplace;
                                    }
                                    else if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kKeep, false))
                                    {
                                        pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kKeep;
                                    }
                                    else if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kIncr, false))
                                    {
                                        pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kIncrementAndClamp;
                                    }
                                    else if (su::Equal(pass_str, ShaderCommand::kStencilStateValue.StencilPassValue.kDecr, false))
                                    {
                                        pass_pipeline_ds_state._stencil_test_pass_op = EStencilOp::kDecrementAndClamp;
                                    }
                                    else
                                    {
                                        AL_ASSERT(false);
                                    }
                                }
                            }
                        }
                    }
                    else if (su::Equal(k, ShaderCommand::kColorMask, false))
                    {
                        u32 mask = 0u;
                        if (v.find("R") != String::npos)
                            mask |= (u32) EColorMask::kRED;
                        if (v.find("G") != String::npos)
                            mask |= (u32) EColorMask::kGREEN;
                        if (v.find("B") != String::npos)
                            mask |= (u32) EColorMask::kBLUE;
                        if (v.find("A") != String::npos)
                            mask |= (u32) EColorMask::kALPHA;
                        pass_pipeline_blend_state._color_mask = (EColorMask) mask;
                    }
                    else {};
                    //提取变体信息
                    if (line.find("multi") != line.npos)
                    {
                        //format: groupname_keywordname
                        auto keywords_str = line.substr(line.find_first_of("e") + 1);
                        keywords_str = su::Trim(keywords_str);
                        auto kw_seq = su::Split(keywords_str, " ");
                        if (kw_seq.size() > 0)
                        {
                            std::set<String> kws;
                            for (auto &kw: kw_seq)
                            {
                                kws.insert(kw);
                            }
                            cur_pass._keywords.emplace_back(kws);
                        }
                    }
                    if (is_transparent && !blend_info.empty())
                    {
                        auto blend_factors = su::Split(blend_info, ",");
                        static auto get_ailu_blend_factor = [](const String &s) -> EBlendFactor
                        {
                            if (su::Equal(s, ShaderCommand::kBlendFactorValue.kOne)) return EBlendFactor::kOne;
                            else if (su::Equal(s, ShaderCommand::kBlendFactorValue.kZero))
                                return EBlendFactor::kZero;
                            else if (su::Equal(s, ShaderCommand::kBlendFactorValue.kSrc))
                                return EBlendFactor::kSrcAlpha;
                            else if (su::Equal(s, ShaderCommand::kBlendFactorValue.kOneMinusSrc))
                                return EBlendFactor::kOneMinusSrcAlpha;
                            else
                                return EBlendFactor::kOne;
                        };
                        pass_pipeline_blend_state._b_enable = true;
                        pass_pipeline_blend_state._src_color = get_ailu_blend_factor(blend_factors[0]);
                        pass_pipeline_blend_state._dst_color = get_ailu_blend_factor(blend_factors[1]);
                    }
                }
                if (need_zbuf_count == 0)
                {
                    pass_pipeline_ds_state._b_depth_write = false;
                    pass_pipeline_ds_state._depth_test_func = ECompareFunc::kAlways;
                }
                //解析shader属性
                auto prop_start_it = std::find(lines.begin(), lines.end(), "//Properties");
                auto prop_end_it = std::find(prop_start_it, lines.end(), "//}");
                if (prop_start_it != lines.end())
                {
                    prop_start_it++;
                    prop_start_it++;
                    for (auto &it = prop_start_it; it != prop_end_it; it++)
                    {
                        line = it->substr(2);
                        ParserShaderProperty(line, cur_pass._shader_prop_infos);
                    }
                    prop_start_it--;
                    prop_start_it--;
                }
                //construct keywords
                Vector<Vector<String>> kw_permutation_in{};
                cur_pass._variant_mgr.BuildIndex(cur_pass._keywords);
                for (auto &kw_group: cur_pass._keywords)
                {
                    Vector<String> kw_cur_group;
                    for (auto &kw: kw_group)
                        kw_cur_group.emplace_back(kw);
                    kw_permutation_in.emplace_back(kw_cur_group);
                }
                if (!kw_permutation_in.empty())
                {
                    auto all_kw_seq = Algorithm::Permutations(kw_permutation_in);
                    for (auto &kw_seq: all_kw_seq)
                    {
                        std::set<String> kw_set;
                        for (int i = 0; i < kw_seq.size(); i++)
                            kw_set.insert(kw_seq[i]);
                        ShaderVariantHash kw_hash = cur_pass._variant_mgr.GetVariantHash(kw_set);
                        ShaderPass::PassVariant v;
                        v._active_keywords = kw_set;
                        cur_pass._variants.insert(std::make_pair(kw_hash, v));
                    }
                }
                if (!cur_pass._variants.contains(0))
                {
                    ShaderPass::PassVariant v;
                    cur_pass._variants.insert(std::make_pair(0, v));
                }
                Map<ShaderVariantHash, EShaderVariantState> cur_pass_variant_state;
                for (auto &it: cur_pass._variants)
                {
                    auto &[vhash, variant] = it;
                    cur_pass_variant_state[vhash] = EShaderVariantState::kNotReady;
                }

                auto pass_it = std::find_if(_passes.begin(), _passes.end(), [&](const ShaderPass &p)
                                            { return p._name == cur_pass._name; });
                //预处理时，从原始pass info中，对于完全一致的变体，复制编译后才能获取的信息并填充,否则这些变体未编译的话，导致引用他们的材质出现问题
                if (pass_it != _passes.end())
                {
                    for (auto &it: cur_pass._variants)
                    {
                        auto &[vhash, variant] = it;
                        for (auto &old_it: pass_it->_variants)
                        {
                            auto &[old_vhash, old_variant] = old_it;
                            if (variant._active_keywords == old_variant._active_keywords)
                            {
                                variant = old_variant;
                            }
                        }
                    }
                }
                cur_pass._index = i;
                cur_pass._source_files.insert(_src_file_path);
                cur_pass._pipeline_raster_state = pass_pipeline_raster_state;
                cur_pass._pipeline_ds_state = pass_pipeline_ds_state;
                cur_pass._pipeline_topology = pass_pipeline_topology;
                cur_pass._pipeline_blend_state = pass_pipeline_blend_state;
                cur_pass._render_queue = pass_render_queue;
                cur_pass._vert_src_file = _src_file_path;
                cur_pass._pixel_src_file = _src_file_path;
                cur_pass._geom_src_file = _src_file_path;
                _stencil_ref = pass_pipeline_ds_state._stencil_ref_value;
                _passes[i] = cur_pass;
                _variant_state.emplace_back(std::move(cur_pass_variant_state));
            }
        }
        else
        {
            AL_ASSERT_MSG(_passes.size() > 0, "parser failed, no pass found!");
        }
        _is_pass_elements_init.store(!is_process_succeed);
        return is_process_succeed;
    }
#pragma endregion

#pragma region ComputeShader
    //---------------------------------------------------------------------ComputeShader-------------------------------------------------------------------
    Ref<ComputeShader> ComputeShader::Create(const WString &sys_path)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
                return nullptr;
            case RendererAPI::ERenderAPI::kDirectX12:
            {
                auto shader = AdoptGpuResource(new RHI::DX12::D3DComputeShader(sys_path));
                s_global_variant_update_map[shader->ID()] = true;
                return shader;
            }
        }
        AL_ASSERT_MSG(false, "Unsupported render api!");
        return nullptr;
    }

    ComputeShader::ComputeShader(const WString &sys_path) : _src_file_path(sys_path)
    {
        _name = ToChar(PathUtils::GetFileName(sys_path));
        Preprocess();
        memset(_cbuf_data, 0, sizeof(_cbuf_data));
    }
    void ComputeShader::SetGlobalTexture(const String &name, RTHandle texture)
    {
        SetGlobalTexture(name, g_pRenderTexturePool->Get(texture));
    }

    bool ComputeShader::ValidatePersistentGlobalTexture(Texture *texture)
    {
        return RDG::RenderGraph::ValidatePersistentGlobalResource(texture, "ComputeShader::SetGlobalTexture");
    }
    void ComputeShader::Bind(RHICommandBuffer *cmd, ComputeShaderKernelId kernel)
    {
    }

    void ComputeShader::SetTexture(const String &name, Texture *texture)
    {
        if (texture == nullptr)
            return;
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        const ComputeBindParams params{ECubemapFace::kUnknown, 0u, depth_slice, UINT32_MAX, Texture::kMainSRVIndex};
        for (auto &cs_ele: _kernels)
        {
            for (const auto &[variant_hash, variant] : cs_ele._variants)
            {
                if (variant._bind_res_infos.contains(name))
                {
                    SetResourceBinding(cs_ele._id, ShaderPropertyRegistry::Get().Intern(name), texture, params);
                    break;
                }
            }
        }
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture)
    {
        if (texture == nullptr || !IsKernelValid(kernel))
            return;

        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        const ComputeBindParams params{ECubemapFace::kUnknown, 0u, depth_slice, UINT32_MAX, Texture::kMainSRVIndex};
        const auto kernel_index = ResolveKernelIndex(kernel);
        const auto &kernel_element = _kernels[kernel_index];
        for (const auto &[variant_hash, variant] : kernel_element._variants)
        {
            if (!variant._bind_res_infos.contains(name))
                continue;
            SetResourceBinding(kernel, ShaderPropertyRegistry::Get().Intern(name), texture, params);
            return;
        }
    }

    void ComputeShader::SetTexture(const String &name, RTHandle handle)
    {
        auto texture = g_pRenderTexturePool->Get(handle);
        SetTexture(name, texture);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, RTHandle handle)
    {
        SetTexture(kernel, name, g_pRenderTexturePool->Get(handle));
    }

    void ComputeShader::SetTexture(const String &name, RTHandle handle, ECubemapFace face, u16 mipmap)
     {
        auto texture = g_pRenderTexturePool->Get(handle);
        SetTexture(name, texture,face,mipmap);
     }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, RTHandle handle,
                                   ECubemapFace face, u16 mipmap)
    {
        SetTexture(kernel, name, g_pRenderTexturePool->Get(handle), face, mipmap);
    }

    void ComputeShader::SetTexture(u8 bind_slot, Texture *texture)
    {
        if (texture == nullptr)
            return;
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        const ComputeBindParams params{ECubemapFace::kUnknown, 0u, depth_slice, UINT32_MAX, Texture::kMainSRVIndex};
        for (auto &cs_ele: _kernels)
        {
            for (const auto &[variant_hash, variant] : cs_ele._variants)
            {
                auto rit = std::find_if(variant._bind_res_infos.begin(), variant._bind_res_infos.end(), [=](const auto &it)
                                        { return it.second._bind_slot == bind_slot; });
                if (rit != variant._bind_res_infos.end())
                {
                    SetResourceBinding(cs_ele._id, rit->second._property_id, texture, params);
                    break;
                }
            }
        }
    }

    void ComputeShader::SetTexture(const String &name, Texture *texture, ECubemapFace face, u16 mipmap)
    {
        u32 sub_res = UINT32_MAX;
        if (face == ECubemapFace::kUnknown)
            sub_res = texture->CalculateSubResIndex(mipmap, 0);
        else
            sub_res = texture->CalculateSubResIndex(face, mipmap, 0);
        bool is_set = false;
        for (auto &cs_ele: _kernels)
        {
            for (const auto &[variant_hash, variant] : cs_ele._variants)
            {
                if (variant._bind_res_infos.contains(name))
                {
                    u16 depth_slice = static_cast<u16>(-1);
                    if (texture->Dimension() == ETextureDimension::kTex3D)
                        depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
                    SetResourceBinding(cs_ele._id, ShaderPropertyRegistry::Get().Intern(name), texture,
                                       ComputeBindParams{face, mipmap, depth_slice, sub_res});
                    is_set = true;
                    break;
                }
            }
        }
        if (!is_set)
            LOG_WARNING("ComputeShader::SetTexture: texture with name {} not found on shader({})",name,_name);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture, u16 mipmap)
    {
        SetTexture(kernel, name, texture, ECubemapFace::kUnknown, mipmap);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture,
                                   ECubemapFace face, u16 mipmap)
    {
        if (texture == nullptr || !IsKernelValid(kernel))
            return;

        const u32 sub_res = face == ECubemapFace::kUnknown ? texture->CalculateSubResIndex(mipmap, 0) :
                                                             texture->CalculateSubResIndex(face, mipmap, 0);
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        const auto &kernel_element = _kernels[ResolveKernelIndex(kernel)];
        for (const auto &[variant_hash, variant] : kernel_element._variants)
        {
            if (!variant._bind_res_infos.contains(name))
                continue;
            SetResourceBinding(kernel, ShaderPropertyRegistry::Get().Intern(name), texture,
                               ComputeBindParams{face, mipmap, depth_slice, sub_res});
            return;
        }
    }

    //void ComputeShader::SetTexture(const String &name, RDG::RGHandle handle)
    //{
    //    auto tex = dynamic_cast<Texture *>(handle._res);
    //    SetTexture(name, tex);
    //}

    //void ComputeShader::SetTexture(const String &name, RDG::RGHandle handle, ECubemapFace face, u16 mipmap)
    //{
    //    auto tex = dynamic_cast<Texture *>(handle._res);
    //    SetTexture(name, tex,face,mipmap);
    //}


    void ComputeShader::SetTexture(const String &name, Texture *texture, u16 mipmap)
    {
        SetTexture(name, texture, ECubemapFace::kUnknown, mipmap);
    }
    void ComputeShader::SetFloat(const String &name, f32 value)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            const auto& it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end() && it->second._res_type & EBindResDescType::kCBufferFloat)
                memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(f32));
        }
    }
    void ComputeShader::SetFloats(const String& name,Vector<f32> values)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end())
            {
                auto& bind_info = it->second;
                if (bind_info._res_type & EBindResDescType::kCBufferFloats)
                {
                    if (bind_info._array_size > 0)
                    {
                        u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                        u8 ele_num = std::min(bind_info._array_size, (u8)values.size());
                        for(u8 i = 0; i < ele_num; i++)
                            memcpy(_cbuf_data + offset + i * 16, &values[i], 4); //16字节对齐
                    }
                    else//(bind_info._res_type & EBindResDescType::kCBufferFloats)
                    {
                        u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                        u16 size = ShaderBindResourceInfo::GetVariableSize(bind_info);
                        size = std::min(size, (u16)(values.size() * sizeof(f32)));
                        memcpy(_cbuf_data + offset, values.data(), size);
                    }
                }
            }
        }
    }

    void ComputeShader::SetBool(const String &name, bool value)
    {
        f32 v = value ? 1.0f : 0.0f;
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end() && it->second._res_type & EBindResDescType::kCBufferBool)
                memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &v, sizeof(f32));
        }
    }

    void ComputeShader::SetInt(const String &name, i32 value)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end())
            {
                if (it->second._res_type & EBindResDescType::kCBufferInt)
                    memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(i32));
                else if (it->second._res_type & EBindResDescType::kCBufferUInt)
                {
                    u32 v = (u32)value;
                    memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &v, sizeof(u32));
                }
                else {}
            }
        }
    }
    void ComputeShader::SetInts(const String &name, Vector<i32> values)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end())
            {
                auto& bind_info = it->second;
                if (bind_info._res_type & EBindResDescType::kCBufferInts)
                {
                    if (bind_info._array_size > 0)
                    {
                        u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                        u8 ele_num = std::min(bind_info._array_size, (u8)values.size());
                        for(u8 i = 0; i < ele_num; i++)
                            memcpy(_cbuf_data + offset + i * 16, &values[i], 4); //16字节对齐
                    }
                    else//(bind_info._res_type & EBindResDescType::kCBufferFloats)
                    {
                        u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                        u16 size = ShaderBindResourceInfo::GetVariableSize(bind_info);
                        size = std::min(size, (u16)(values.size() * sizeof(i32)));
                        memcpy(_cbuf_data + offset, values.data(), size);
                    }
                }
                else if (bind_info._res_type & EBindResDescType::kCBufferUInts)
                {
                    if (bind_info._array_size > 0)
                    {
                        u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                        u8 ele_num = std::min(bind_info._array_size, (u8)values.size());
                        for(u8 i = 0; i < ele_num; i++)
                        {
                            u32 v = (u32)values[i];
                            memcpy(_cbuf_data + offset + i * 16, &v, 4); //16字节对齐
                        }
                    }
                    else
                    {
                        u16 offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                        u16 size = ShaderBindResourceInfo::GetVariableSize(bind_info);
                        size = std::min(size, (u16)(values.size() * sizeof(i32)));
                        u32 buf[256];
                        for(u32 i = 0; i < values.size(); i++)
                            buf[i] = (u32)values[i];
                        memcpy(_cbuf_data + offset, buf, size);
                    }
                }
                else {};
            }
        }
    }

    void ComputeShader::SetVector(const String &name, Vector4f vector)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end())
            {
                u16 offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                u16 size = ShaderBindResourceInfo::GetVariableSize(it->second);
                if (it->second._res_type & EBindResDescType::kCBufferInts)
                {
                    Vector4Int v = Vector4Int((i32) vector.x, (i32) vector.y, (i32) vector.z, (i32) vector.w);
                    memcpy(_cbuf_data + offset, &v, size);
                }
                else if (it->second._res_type & EBindResDescType::kCBufferUInts)
                {
                    Vector4UInt v = Vector4UInt((u32) vector.x, (u32) vector.y, (u32) vector.z, (u32) vector.w);
                    memcpy(_cbuf_data + offset, &v, size);
                }
                else
                    memcpy(_cbuf_data + offset, &vector, size);
            }
        }
    }

    void ComputeShader::SetVectorArray(const String& name,const Vector<Vector4f>& vectors)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end())
            {
                u16 offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                u16 size = ShaderBindResourceInfo::GetVariableSize(it->second);
                u16 array_size = std::min<u16>((u16)it->second._array_size,(u16)vectors.size());
                memcpy(_cbuf_data + offset, vectors.data(), array_size * sizeof(Vector4f));
            }
        }
    }
    void ComputeShader::SetVectorArray(const String& name,Vector4f* vectors,u16 num)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end())
            {
                u16 offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                u16 size = ShaderBindResourceInfo::GetVariableSize(it->second);
                u16 array_size = std::min<u16>((u16)it->second._array_size,num);
                memcpy(_cbuf_data + offset, vectors, array_size * sizeof(Vector4f));
            }
        }
    }

    void ComputeShader::SetBuffer(const String &name, ConstantBuffer *buf)
    {
        if (buf == nullptr)
            return;
        for (auto &cs_ele: _kernels)
        {
            for (const auto &[variant_hash, variant] : cs_ele._variants)
            {
                if (variant._bind_res_infos.contains(name))
                {
                    ComputeBindParams params{};
                    params._is_internal_cbuf = variant._bind_res_infos.at(name)._bind_flag & ShaderBindResourceInfo::kBindFlagInternal;
                    SetResourceBinding(cs_ele._id, ShaderPropertyRegistry::Get().Intern(name), buf, params);
                    break;
                }
            }
        }
    }
    void ComputeShader::SetBuffer(const String &name, GPUBuffer *buf)
    {
        if (buf == nullptr)
            return;
        for (auto &cs_ele: _kernels)
        {
            for (const auto &[variant_hash, variant] : cs_ele._variants)
            {
                if (variant._bind_res_infos.contains(name))
                {
                    ComputeBindParams params{};
                    params._is_internal_cbuf = variant._bind_res_infos.at(name)._bind_flag & ShaderBindResourceInfo::kBindFlagInternal;
                    SetResourceBinding(cs_ele._id, ShaderPropertyRegistry::Get().Intern(name), buf, params);
                    break;
                }
            }
        }
    }

    void ComputeShader::SetBuffer(ComputeShaderKernelId kernel,const String &name, ConstantBuffer *buf)
    {
        if (buf == nullptr || !IsKernelValid(kernel))
            return;
        const auto &cs_ele = _kernels[ResolveKernelIndex(kernel)];
        for (const auto &[variant_hash, variant] : cs_ele._variants)
        {
            if (variant._bind_res_infos.contains(name))
            {
                ComputeBindParams params{};
                params._is_internal_cbuf = variant._bind_res_infos.at(name)._bind_flag & ShaderBindResourceInfo::kBindFlagInternal;
                SetResourceBinding(kernel, ShaderPropertyRegistry::Get().Intern(name), buf, params);
                break;
            }
        }
    }
    void ComputeShader::SetBuffer(ComputeShaderKernelId kernel,const String &name, GPUBuffer *buf)
    {
        if (buf == nullptr || !IsKernelValid(kernel))
            return;
        const auto &cs_ele = _kernels[ResolveKernelIndex(kernel)];
        for (const auto &[variant_hash, variant] : cs_ele._variants)
        {
            if (variant._bind_res_infos.contains(name))
            {
                ComputeBindParams params{};
                params._is_internal_cbuf = variant._bind_res_infos.at(name)._bind_flag & ShaderBindResourceInfo::kBindFlagInternal;
                SetResourceBinding(kernel, ShaderPropertyRegistry::Get().Intern(name), buf, params);
                break;
            }
        }
    }

    void ComputeShader::SetMatrix(const String& name,Matrix4x4f mat)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end() && it->second._res_type & EBindResDescType::kCBufferMatrix)
                memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), &mat, sizeof(Matrix4x4f));
        }
    }

    void ComputeShader::SetMatrixArray(const String& name,Vector<Matrix4x4f> matrix_arr)
    {
        for (auto &cs_ele: _kernels)
        {
            auto &variant = cs_ele._variants[cs_ele._active_variant];
            auto it = variant._bind_res_infos.find(name);
            if (it != variant._bind_res_infos.end() && it->second._res_type & EBindResDescType::kCBufferMatrix && it->second._array_size > 0)
            {
                u8 matrix_num = std::min(it->second._array_size, (u8)matrix_arr.size());
                memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), matrix_arr.data(), sizeof(Matrix4x4f) * matrix_num);
            }
        }
    }

    void ComputeShader::GetThreadNum(ComputeShaderKernelId kernel, u16 &x, u16 &y, u16 &z) const
    {
        const auto kernel_index = ResolveKernelIndex(kernel);
        x = _kernels[kernel_index]._thread_num.x;
        y = _kernels[kernel_index]._thread_num.y;
        z = _kernels[kernel_index]._thread_num.z;
    }

    bool Ailu::ComputeShader::IsDependencyFile(const WString &sys_path) const
    {
        for (const auto &k: _kernels)
        {
            for (const auto &it: k._variants)
            {
                auto &variant = it.second;
                if (variant._all_dep_file_pathes.contains(sys_path))
                    return true;
            }
        }
        return false;
    }
    bool ComputeShader::Compile(bool is_load_cache)
    {
        _is_compiling.store(true);
        if (!FileManager::Exist(_src_file_path))
        {
            LOG_ERROR(L"ComputeShader::Compile: source:{} not exist!", _src_file_path);
            return false;
        }
        //Preprocess();
        bool is_succeed = true;
        memcpy( _cache_cbuf_data, _cbuf_data, sizeof(u8) * kCBufferSize);
        for (auto& k: _kernels)
        {
            for (auto &v: k._variants)
            {
                const auto kernel_index = ResolveKernelIndex(k._id);
                is_succeed &= Compile(kernel_index, v.first, is_load_cache);
            }
        }
        _is_compiling.store(false);
        return is_succeed;
    }
    
    i16 ComputeShader::NameToSlot(const String &name, ComputeShaderKernelId kernel,ShaderVariantHash variant_hash) const
    {
        const auto kernel_index = ResolveKernelIndex(kernel);
        if (auto it = _kernels[kernel_index]._variants.at(variant_hash)._bind_res_infos.find(name); it != _kernels[kernel_index]._variants.at(variant_hash)._bind_res_infos.end())
            return it->second._bind_slot;
        return -1;
    }
    String ComputeShader::SlotToName(ComputeShaderKernelId kernel, u16 slot) const
    {
        const auto kernel_index = ResolveKernelIndex(kernel);
        if (kernel_index >= _kernels.size())
            return "";
        const auto variant_hash = ResolveActiveVariant(kernel);
        const auto variant_it = _kernels[kernel_index]._variants.find(variant_hash);
        if (variant_it == _kernels[kernel_index]._variants.end())
            return "";
        for (const auto &[name, info] : variant_it->second._bind_res_infos)
        {
            if (info._bind_slot == (i16)slot)
                return name;
        }
        return "";
    }
    void ComputeShader::PushState(ComputeShaderKernelId kernel)
    {
        const auto kernel_index = ResolveKernelIndex(kernel);
        BindState cur_state;
        cur_state._kernel = _kernels[kernel_index]._id;
        cur_state._variant_hash = ResolveActiveVariant(kernel);
        cur_state._max_bind_slot = 0u;
        auto &cs_ele = _kernels[kernel_index]._variants[cur_state._variant_hash];
        memset(cur_state._bind_res.data(),0,sizeof(GpuResource*)*32);
        memset(cur_state._bind_res_priority.data(),0u,sizeof(u16) * 32);
        for (auto &info: cs_ele._bind_res_infos)
        {
            auto &bind_info = info.second;
            if (bind_info._res_type == EBindResDescType::kConstBuffer && bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagInternal)
            {
                ConstantBuffer *cb = ConstBufferPool::Acquire(bind_info._cbuf_size);
                cb->SetData(_cbuf_data);
                cur_state._bind_res[bind_info._bind_slot] = cb;
                cur_state._bind_params[bind_info._bind_slot] = ComputeBindParams{};
                cur_state._bind_params[bind_info._bind_slot]._is_internal_cbuf = true;
                cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
            }
            else if (const auto *binding = FindResourceBinding(kernel, bind_info._property_id); binding != nullptr && binding->_resource != nullptr)
            {
                cur_state._bind_res[bind_info._bind_slot] = binding->_resource;
                cur_state._bind_params[bind_info._bind_slot] = binding->_params;
                cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
            }
            cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot,bind_info._bind_slot);
        }
        for (auto &it: s_global_cbuffer_bind_info)
        {
            if (const auto& itt = cs_ele._bind_res_infos.find(it.first);itt != cs_ele._bind_res_infos.end())
            {
                const auto& bind_info = itt->second;
                if (cur_state._bind_res_priority[bind_info._bind_slot] <= PipelineResource::kPriorityGlobal)
                {
                    cur_state._bind_res[bind_info._bind_slot] = it.second;
                    cur_state._bind_params[bind_info._bind_slot] = ComputeBindParams{ECubemapFace::kUnknown, 0, 0, UINT32_MAX, 0};
                    cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityGlobal;
                    cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot,bind_info._bind_slot);
                }
            }
        }
        for (auto &it: s_global_buffer_bind_info)
        {
            if (const auto &itt = cs_ele._bind_res_infos.find(it.first); itt != cs_ele._bind_res_infos.end())
            {
                const auto &bind_info = itt->second;
                if (cur_state._bind_res_priority[bind_info._bind_slot] <= PipelineResource::kPriorityGlobal)
                {
                    cur_state._bind_res[bind_info._bind_slot] = it.second;
                    cur_state._bind_params[bind_info._bind_slot] = ComputeBindParams{ECubemapFace::kUnknown, 0, 0, UINT32_MAX, 0};
                    cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityGlobal;
                    cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, bind_info._bind_slot);
                }
            }
        }
        for (auto &it: s_global_textures_bind_info)
        {
            if (const auto& itt = cs_ele._bind_res_infos.find(it.first);itt != cs_ele._bind_res_infos.end())
            {
                const auto& bind_info = itt->second;
                if (cur_state._bind_res_priority[bind_info._bind_slot] <= PipelineResource::kPriorityGlobal)
                {
                    cur_state._bind_res[bind_info._bind_slot] = it.second;
                    u16 depth_slice = -1;
                    if (it.second->Dimension() == ETextureDimension::kTex3D)
                        depth_slice = dynamic_cast<Texture3D *>(it.second)->Depth();
                    cur_state._bind_params[bind_info._bind_slot] = ComputeBindParams{ECubemapFace::kUnknown, 0, depth_slice, UINT32_MAX, Texture::kMainSRVIndex};
                    cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityGlobal;
                    cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot,bind_info._bind_slot);
                }
            }
        }
        for (auto &it: s_global_floats)
        {
            SetFloat(it.first, it.second);
        }
        for (auto &it: s_global_ints)
        {
            SetInt(it.first, it.second);
        }
        
        std::unique_lock lock(_state_mutex);
        _bind_state.push(cur_state);
    }

    void ComputeShader::CaptureDispatchState(ComputeShaderKernelId kernel, ComputeDispatchSnapshot &snapshot,
                                              const HashMap<ShaderPropertyId, CommandResourceBinding> *command_resources)
    {
        snapshot = ComputeDispatchSnapshot{};
        if (!IsKernelValid(kernel))
            return;
        const auto kernel_index = ResolveKernelIndex(kernel);
        const auto variant_hash = ResolveActiveVariant(kernel);
        const auto &kernel_variant = _kernels[kernel_index]._variants[variant_hash];
        memcpy(snapshot._cbuf_data.data(), _cbuf_data, sizeof(_cbuf_data));
        for (const auto &[name, value] : s_global_floats)
        {
            if (const auto it = kernel_variant._bind_res_infos.find(name); it != kernel_variant._bind_res_infos.end()
                && it->second._res_type & EBindResDescType::kCBufferFloat)
                memcpy(snapshot._cbuf_data.data() + ShaderBindResourceInfo::GetVariableOffset(it->second), &value, sizeof(value));
        }
        for (const auto &[name, value] : s_global_ints)
        {
            if (const auto it = kernel_variant._bind_res_infos.find(name); it != kernel_variant._bind_res_infos.end())
            {
                const auto offset = ShaderBindResourceInfo::GetVariableOffset(it->second);
                if (it->second._res_type & EBindResDescType::kCBufferInt)
                    memcpy(snapshot._cbuf_data.data() + offset, &value, sizeof(value));
                else if (it->second._res_type & EBindResDescType::kCBufferUInt)
                {
                    const u32 uint_value = static_cast<u32>(value);
                    memcpy(snapshot._cbuf_data.data() + offset, &uint_value, sizeof(uint_value));
                }
            }
        }
        Array<ComputeBindingSnapshotEntry, 32> captured{};
        u16 captured_count = 0u;
        auto find_captured = [&](u16 slot) -> ComputeBindingSnapshotEntry *
        {
            for (u16 i = 0u; i < captured_count; ++i)
            {
                if (captured[i]._slot == slot)
                    return &captured[i];
            }
            return nullptr;
        };

        auto apply_resource = [&](u16 slot, GpuResource *resource, EBindResDescType resource_type, u16 priority,
                                  const ComputeBindParams &params)
        {
            if (resource == nullptr || slot >= 32u)
                return;
            auto *entry = find_captured(slot);
            if (entry == nullptr)
            {
                AL_ASSERT(captured_count < captured.size());
                if (captured_count >= captured.size())
                    return;
                entry = &captured[captured_count++];
                entry->_slot = slot;
            }
            if (entry->_resource.IsValid() && entry->_priority > priority)
                return;
            entry->_resource = resource->Handle();
            entry->_resource_type = resource_type;
            entry->_priority = priority;
            entry->_face = static_cast<u16>(params._face);
            entry->_mipmap = params._mipmap;
            entry->_slice = params._slice;
            entry->_view_index = params._view_index;
            entry->_sub_res = params._sub_res;
            entry->_is_internal_cbuf = params._is_internal_cbuf;
        };

        for (const auto &[name, bind_info] : kernel_variant._bind_res_infos)
        {
            const u16 slot = bind_info._bind_slot;
            if (bind_info._res_type == EBindResDescType::kConstBuffer
                && bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagInternal)
            {
                auto *cb = ConstBufferPool::Acquire(bind_info._cbuf_size);
                cb->SetData(snapshot._cbuf_data.data());
                ComputeBindParams params{};
                params._is_internal_cbuf = true;
                apply_resource(slot, cb, bind_info._res_type, PipelineResource::kPriorityLocal, params);
            }
            else if (const auto *binding = FindResourceBinding(kernel, bind_info._property_id); binding != nullptr && binding->_resource != nullptr)
            {
                apply_resource(slot, binding->_resource, bind_info._res_type, PipelineResource::kPriorityLocal, binding->_params);
            }
        }

        auto apply_global_resource = [&](const String &name, GpuResource *resource, const ComputeBindParams &params)
        {
            if (resource == nullptr)
                return;
            const auto *bind_info = kernel_variant._bind_res_infos.contains(name)
                ? &kernel_variant._bind_res_infos.at(name) : nullptr;
            if (bind_info != nullptr)
                apply_resource(bind_info->_bind_slot, resource, bind_info->_res_type, PipelineResource::kPriorityGlobal, params);
        };

        for (const auto &[name, resource] : s_global_cbuffer_bind_info)
            apply_global_resource(name, resource, ComputeBindParams{});
        for (const auto &[name, resource] : s_global_buffer_bind_info)
            apply_global_resource(name, resource, ComputeBindParams{});
        for (const auto &[name, resource] : s_global_textures_bind_info)
        {
            if (resource == nullptr)
                continue;
            u16 depth_slice = static_cast<u16>(-1);
            if (resource->Dimension() == ETextureDimension::kTex3D)
                depth_slice = dynamic_cast<Texture3D *>(resource)->Depth();
            apply_global_resource(name, resource,
                                  ComputeBindParams{ECubemapFace::kUnknown, 0u, depth_slice, UINT32_MAX, Texture::kMainSRVIndex});
        }

        if (command_resources != nullptr)
        {
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                if (command_binding._resource == nullptr)
                    continue;
                const auto &name = ShaderPropertyRegistry::Get().GetName(property_id);
                auto bind_it = kernel_variant._bind_res_infos.find(name);
                if (bind_it == kernel_variant._bind_res_infos.end())
                    continue;
                const auto &bind_info = bind_it->second;
                auto *entry = find_captured(bind_info._bind_slot);
                if (entry == nullptr)
                {
                    AL_ASSERT(captured_count < captured.size());
                    entry = &captured[captured_count++];
                    entry->_slot = bind_info._bind_slot;
                }
                entry->_resource = command_binding._resource != nullptr ? command_binding._resource->Handle() : GpuResourceHandle{};
                entry->_resource_type = command_binding._resource_type == EBindResDescType::kConstBufferRaw
                    ? EBindResDescType::kConstBufferRaw : bind_info._res_type;
                entry->_priority = PipelineResource::kPriorityCmd;
                entry->_face = static_cast<u16>(ECubemapFace::kUnknown);
                entry->_mipmap = 0u;
                entry->_slice = 0u;
                entry->_view_index = Texture::kMainSRVIndex;
                entry->_sub_res = UINT32_MAX;
                entry->_is_internal_cbuf = false;
                entry->_addi_info = command_binding._addi_info;
            }
        }
        for (u16 i = 0u; i < captured_count; ++i)
            snapshot._entries[i] = captured[i];
        snapshot._entry_count = captured_count;
        snapshot._variant_hash = variant_hash;
        snapshot._is_ready = _variant_state[kernel_index][variant_hash] == EShaderVariantState::kReady;
    }


    bool ComputeShader::Compile(u16 kernel_index, ShaderVariantHash variant_hash, bool is_load_cache)
    {
        _is_compiling.store(true);
        bool is_succeed = RHICompileImpl(kernel_index, variant_hash, is_load_cache);
        _variant_state[kernel_index][variant_hash] = is_succeed ? EShaderVariantState::kReady : EShaderVariantState::kError;
        _is_compiling.store(false);
        return is_succeed;
    }
    bool ComputeShader::RHICompileImpl(u16 kernel_index, ShaderVariantHash variant_hash, bool is_load_cache)
    {
        return false;
    }

    u16 ComputeShader::ResolveKernelIndex(ComputeShaderKernelId kernel) const
    {
        if (kernel == kInvalidComputeShaderKernelId)
        {
            AL_ASSERT_MSG(false, "ComputeShader kernel id is invalid!");
            return 0u;
        }
        if (auto it = _kernel_id_to_index.find(kernel); it != _kernel_id_to_index.end())
            return it->second;
        AL_ASSERT_MSG(false, "ComputeShader kernel id is not valid on this shader!");
        return 0u;
    }

    ShaderVariantHash ComputeShader::ResolveActiveVariant(ComputeShaderKernelId kernel) const
    {
        const auto kernel_index = ResolveKernelIndex(kernel);
        const auto &element = _kernels[kernel_index];
        std::set<String> active_keywords;
        for (const auto &keyword : _local_active_keywords)
        {
            if (KernelElement::IsKernelKeyword(element, keyword))
                active_keywords.insert(keyword);
        }
        for (const auto &keyword : s_global_active_keywords)
        {
            if (KernelElement::IsKernelKeyword(element, keyword))
                active_keywords.insert(keyword);
        }
        return element._variant_mgr.GetVariantHash(active_keywords);
    }

    void ComputeShader::SetResourceBinding(ComputeShaderKernelId kernel, ShaderPropertyId property_id, GpuResource *resource,
                                            const ComputeBindParams &params)
    {
        if (!IsKernelValid(kernel))
            return;
        auto &binding = _resource_bindings[kernel][property_id];
        binding._resource = resource;
        binding._params = params;
        const auto &element = _kernels[ResolveKernelIndex(kernel)];
        for (const auto &[variant_hash, variant] : element._variants)
        {
            const auto bind_it = std::find_if(variant._bind_res_infos.begin(), variant._bind_res_infos.end(),
                                              [property_id](const auto &item)
                                              { return item.second._property_id == property_id; });
            if (bind_it == variant._bind_res_infos.end())
                continue;
            const bool is_uav = bind_it->second._res_type == EBindResDescType::kUAVTexture2D
                || bind_it->second._res_type == EBindResDescType::kRWTexture3D;
            if (!is_uav && binding._params._view_index != (u16)-1)
                break;
            const auto view_type = is_uav ? Texture::ETextureViewType::kUAV : Texture::ETextureViewType::kSRV;
            auto *texture = static_cast<Texture *>(resource);
            if (texture->Dimension() == ETextureDimension::kTex3D)
                binding._params._view_index = texture->CalculateViewIndex(view_type, binding._params._mipmap,
                                                                           binding._params._slice);
            else
                binding._params._view_index = texture->CalculateViewIndex(view_type, binding._params._face,
                                                                           binding._params._mipmap, 0u);
            break;
        }
    }

    const ComputeShader::ComputeResourceBinding *ComputeShader::FindResourceBinding(ComputeShaderKernelId kernel,
                                                                                     ShaderPropertyId property_id) const
    {
        const auto kernel_it = _resource_bindings.find(kernel);
        if (kernel_it == _resource_bindings.end())
            return nullptr;
        const auto binding_it = kernel_it->second.find(property_id);
        return binding_it == kernel_it->second.end() ? nullptr : &binding_it->second;
    }

    bool ComputeShader::IsKernelValid(ComputeShaderKernelId kernel) const
    {
        return _kernel_id_to_index.contains(kernel);
    }

    bool ComputeShader::Preprocess()
    {
        String data;
        FileManager::ReadFile(_src_file_path, data);
        auto lines = su::Split(data, "\n");
        _kernels.clear();
        _variant_state.clear();
        _kernel_id_to_index.clear();
        HashMap<String, Vector<std::set<String>>> kernels;
        for (auto &line: lines)
        {
            if (su::BeginWith(line, "#pragma kernel"))
            {
                auto kernel_line = su::Split(line, " ");
                kernels[kernel_line[2]] = Vector<std::set<String>>();
            }
            else if (su::BeginWith(line, "#pragma multi_compile"))
            {
                auto kernel_line = su::Split(line, " ");
                auto &kernel_name = kernel_line[2];
                if (kernels.contains(kernel_name) && kernel_line.size() > 3)
                {
                    auto &kw_set = kernels[kernel_name].emplace_back();
                    for (u16 i = 3; i < kernel_line.size(); ++i)
                    {
                        kw_set.insert(kernel_line[i]);
                    }
                }
                else
                {
                    LOG_ERROR("ComputeShader::Preprocess: pragma multi_compile:{} not exist!", kernel_name);
                }
            }
        }
        for (auto &it: kernels)
        {
            auto &[kernel_name, keywords] = it;
            KernelElement cur_kernel{};
            const auto kernel_index = static_cast<u16>(_kernels.size());
            cur_kernel._id = ComputeShaderKernelRegistry::Get().Intern(kernel_name);
            cur_kernel._name = kernel_name;
            cur_kernel._thread_num = Vector3UInt(8u, 8u, 1u);
            cur_kernel._keywords = keywords;
            //construct keywords
            Vector<Vector<String>> kw_permutation_in{};
            cur_kernel._variant_mgr.BuildIndex(cur_kernel._keywords);
            for (auto &kw_group: cur_kernel._keywords)
            {
                Vector<String> kw_cur_group;
                for (auto &kw: kw_group)
                    kw_cur_group.emplace_back(kw);
                kw_permutation_in.emplace_back(kw_cur_group);
            }
            if (!kw_permutation_in.empty())
            {
                auto all_kw_seq = Algorithm::Permutations(kw_permutation_in);
                for (auto &kw_seq: all_kw_seq)
                {
                    std::set<String> kw_set;
                    for (int i = 0; i < kw_seq.size(); i++)
                        kw_set.insert(kw_seq[i]);
                    ShaderVariantHash kw_hash = cur_kernel._variant_mgr.GetVariantHash(kw_set);
                    KernelElement::KernelVariant v;
                    v._active_keywords = kw_set;
                    v._all_dep_file_pathes.insert(_src_file_path);
                    cur_kernel._variants.insert(std::make_pair(kw_hash, v));
                }
            }
            if (!cur_kernel._variants.contains(0))
            {
                KernelElement::KernelVariant v;
                v._all_dep_file_pathes.insert(_src_file_path);
                cur_kernel._variants.insert(std::make_pair(0, v));
            }
            Map<ShaderVariantHash, EShaderVariantState> cur_kernel_variant_state;
            for (auto &it: cur_kernel._variants)
            {
                auto &[vhash, variant] = it;
                cur_kernel_variant_state[vhash] = EShaderVariantState::kNotReady;
            }
            _variant_state.emplace_back(std::move(cur_kernel_variant_state));
            _kernel_id_to_index[cur_kernel._id] = kernel_index;
            _kernels.emplace_back(std::move(cur_kernel));
        }
        AL_ASSERT(!_kernels.empty());
        return !_kernels.empty();
    }

    void ComputeShader::EnableKeyword(const String &kw)
    {
        if (_local_active_keywords.contains(kw))
            return;
        for (auto &kernel: _kernels)
        {
            for (auto &group_kw: kernel._variant_mgr.KeywordsSameGroup(kw))
                _local_active_keywords.erase(group_kw);
            _local_active_keywords.insert(kw);
        }
        s_global_variant_update_map[_id] = true;
    }
    void ComputeShader::DisableKeyword(const String &kw)
    {
        if (!_local_active_keywords.contains(kw))
            return;
        _local_active_keywords.erase(kw);
        s_global_variant_update_map[_id] = true;
    }

    std::tuple<u16, u16, u16> ComputeShader::CalculateDispatchNum(ComputeShaderKernelId kernel, u16 task_num_x, u16 task_num_y, u16 task_num_z) const
    {
        const auto kernel_index = ResolveKernelIndex(kernel);
        f32 x = static_cast<f32>(_kernels[kernel_index]._thread_num.x);
        f32 y = static_cast<f32>(_kernels[kernel_index]._thread_num.y);
        f32 z = static_cast<f32>(_kernels[kernel_index]._thread_num.z);
        return std::make_tuple((u16) std::ceilf(task_num_x / x), (u16) std::ceilf(task_num_y / y), (u16) std::ceilf(task_num_z / z));
    }
#pragma endregion
    //---------------------------------------------------------------------ComputeShader-------------------------------------------------------------------
}// namespace Ailu
