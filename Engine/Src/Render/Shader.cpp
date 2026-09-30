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
#include "Render/ShaderDependencyPath.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/RayTracing/RayTracingScene.h"

namespace Ailu::Render
{
    // 内置 property 名只在首次使用时 Intern 一次，之后热路径只读取已缓存的 id。
    // 名称与 RenderConstants::kCBufNamePer* 保持一致（这里刻意不 include RenderConstants.h，
    // 避免 Render 头之间的循环依赖）。
    static RenderPropertyIds::LazyId s_per_object_property_id;
    static RenderPropertyIds::LazyId s_per_camera_property_id;
    static RenderPropertyIds::LazyId s_per_material_property_id;
    static RenderPropertyIds::LazyId s_per_scene_property_id;
    const ShaderPropertyId &RenderPropertyIds::PerObject() { return Resolve(s_per_object_property_id, "CBufferPerObjectData"); }
    const ShaderPropertyId &RenderPropertyIds::PerCamera() { return Resolve(s_per_camera_property_id, "CBufferPerCameraData"); }
    const ShaderPropertyId &RenderPropertyIds::PerMaterial() { return Resolve(s_per_material_property_id, "CBufferPerMaterialData"); }
    const ShaderPropertyId &RenderPropertyIds::PerScene() { return Resolve(s_per_scene_property_id, "CBufferPerSceneData"); }

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

    Ref<const ShaderBindingLayout> BuildShaderBindingLayout(const ShaderReflectionResourceMap &bind_res_infos, u32 version)
    {
        auto layout = MakeRef<ShaderBindingLayout>();
        layout->Version(version);
        for (const auto &[name, bind_info] : bind_res_infos)
        {
            ShaderPropertyBinding binding;
            binding._property_id = bind_info._property_id == kInvalidShaderPropertyId
                                       ? ShaderPropertyRegistry::Get().Intern(name)
                                       : bind_info._property_id;
            binding._resource_type = bind_info._res_type;
            binding._bind_slot = bind_info._bind_slot;
            binding._register_space = bind_info._register_space;
            binding._bind_flag = bind_info._bind_flag;
            binding._array_size = bind_info._array_size == 0u ? 1u : bind_info._array_size;
            if (bind_info._res_type & EBindResDescType::kCBufferAttribute)
            {
                binding._buffer_offset = ShaderBindResourceInfo::GetVariableOffset(bind_info);
                binding._buffer_size = ShaderBindResourceInfo::GetVariableSize(bind_info);
            }
            layout->Add(binding);
        }
        return layout;
    }

    void Shader::BuildBindingLayout(u16 pass_index, ShaderVariantHash variant_hash)
    {
        AL_ASSERT(pass_index < _passes.size());
        auto &variant = _passes[pass_index]._variants.at(variant_hash);
        if (variant._binding_layout)
            _retired_binding_layouts.emplace_back(variant._binding_layout);
        variant._binding_layout = BuildShaderBindingLayout(variant._bind_res_infos, _binding_layout_version++);
    }

    bool Shader::Compile(u16 pass_id, ShaderVariantHash variant_hash, EShaderCompilePolicy policy)
    {
        //LOG_INFO(L"Begin compile shader: {},pass: {},variant: {} with keywords {}...", _src_file_path, pass_id, variant_hash, ToWChar(su::Join(ActiveKeywords(pass_id, variant_hash), ",")));
        TimeMgr::Get().Mark();
        AL_ASSERT(pass_id < _passes.size());
        auto &pass = _passes[pass_id];
        AL_ASSERT(pass._variants.contains(variant_hash));
        const auto previous_state = _variant_state[pass_id][variant_hash];
        _variant_state[pass_id][variant_hash] = EShaderVariantState::kCompiling;
        if (RHICompileImpl(pass_id, variant_hash, policy))
        {
            pass._variants[variant_hash]._pipeline_input_layout.Hash(PipelineStateHash<VertexInputLayout>::GenHash(pass._variants[variant_hash]._pipeline_input_layout));
            pass._pipeline_raster_state.Hash(PipelineStateHash<RasterizerState>::GenHash(pass._pipeline_raster_state));
            pass._pipeline_blend_state.Hash(PipelineStateHash<BlendState>::GenHash(pass._pipeline_blend_state));
            pass._pipeline_ds_state.Hash(PipelineStateHash<DepthStencilState>::GenHash(pass._pipeline_ds_state));
            pass._variants[variant_hash]._shader_hash = ConstructHash(_id, pass_id, variant_hash);
            GraphicsPipelineStateMgr::Get().OnShaderCompiled(this, pass_id, variant_hash);
            _variant_state[pass_id][variant_hash] = EShaderVariantState::kReady;
            LOG_INFO("ShaderVariant READY shader={} pass={} variant={} total={}ms", _name, pass_id, variant_hash,
                     TimeMgr::Get().GetElapsedSinceLastMark());
        }
        else
        {
            _variant_state[pass_id][variant_hash] = previous_state == EShaderVariantState::kReady
                                                        ? EShaderVariantState::kReady
                                                        : EShaderVariantState::kError;
            LOG_ERROR("ShaderVariant FAILED shader={} pass={} variant={} total={}ms", _name, pass_id, variant_hash,
                      TimeMgr::Get().GetElapsedSinceLastMark());
        }
        return _variant_state[pass_id][variant_hash] == EShaderVariantState::kReady;
    }

    bool Shader::Compile(EShaderCompilePolicy policy)
    {
        u16 pass_index = 0;
        u32 variant_index = 0;
        u32 error_variant_count = 0;
        for (auto &pass: _passes)
        {
            for (auto &variant_it: pass._variants)
            {
                auto &[vhash, kw_seq] = variant_it;
                if (!Compile(pass_index, vhash, policy))
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
        // ShadowCaster/DepthOnly/MotionVector 这类查询每帧每对象都会发生，
        // 缓存之后只有首次（或 shader 重建后首次）才做字符串比较。
        if (const auto cached = _pass_name_to_index.find(pass_name); cached != _pass_name_to_index.end())
            return cached->second;
        auto it = std::find_if(_passes.begin(), _passes.end(), [&](const ShaderPass &shader_pass) -> bool
                               { return pass_name == shader_pass._name; });
        const i16 pass_index = it != _passes.end() ? (i16) it->_index : -1;
        _pass_name_to_index.emplace(std::move(pass_name), pass_index);
        return pass_index;
    }

    i16 Shader::ShadowCasterPassIndex()
    {
        if (_shadow_caster_pass_index == -2)
            _shadow_caster_pass_index = FindPass("ShadowCaster");
        return _shadow_caster_pass_index;
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
    bool Shader::RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash, EShaderCompilePolicy policy)
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
        auto previous_variant_state = std::move(_variant_state);
        _variant_state.clear();
        // pass 列表即将重建，索引缓存必须失效，否则会给出过期/越界的 pass index。
        _pass_name_to_index.clear();
        _shadow_caster_pass_index = -2;
        List<String> lines{};
        String line{};
        u32 line_count = 0;
        auto sys_path = ToChar(_src_file_path);
        lines = ReadFileToLines(sys_path, line_count, "//info begin", "//info end");
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
                const size_t previous_pass_index = pass_it == _passes.end()
                                                        ? _passes.size()
                                                        : static_cast<size_t>(std::distance(_passes.begin(), pass_it));
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
                                if (previous_pass_index < previous_variant_state.size())
                                {
                                    if (auto state_it = previous_variant_state[previous_pass_index].find(old_vhash);
                                        state_it != previous_variant_state[previous_pass_index].end())
                                        cur_pass_variant_state[vhash] = state_it->second;
                                }
                            }
                        }
                    }
                }
                cur_pass._index = i;
                cur_pass._source_files.insert(NormalizeShaderDependencyPath(_src_file_path));
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
            _variant_state = std::move(previous_variant_state);
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
        SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture);
    }

    void ComputeShader::SetTexture(ShaderPropertyId property_id, Texture *texture)
    {
        if (texture == nullptr)
            return;
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        const ComputeBindParams params{static_cast<u16>(ECubemapFace::kUnknown), 0u, depth_slice, UINT32_MAX};
        for (const auto &kernel : _kernels)
            SetResourceBinding(kernel._id, property_id, texture, params);
    }

    void ComputeShader::SetTexture(ShaderPropertyId property_id, Texture *texture, ECubemapFace face, u16 mipmap)
    {
        if (texture == nullptr)
            return;
        const u32 sub_res = face == ECubemapFace::kUnknown ? texture->CalculateSubResIndex(mipmap, 0u) :
                                                             texture->CalculateSubResIndex(face, mipmap, 0u);
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        const ComputeBindParams params{static_cast<u16>(face), mipmap, depth_slice, sub_res};
        for (const auto &kernel : _kernels)
            SetResourceBinding(kernel._id, property_id, texture, params);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture)
    {
        SetTexture(kernel, ShaderPropertyRegistry::Get().Intern(name), texture);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, ShaderPropertyId property_id, Texture *texture)
    {
        if (texture == nullptr || !IsKernelValid(kernel))
            return;
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        SetResourceBinding(kernel, property_id, texture,
                           ComputeBindParams{static_cast<u16>(ECubemapFace::kUnknown), 0u, depth_slice, UINT32_MAX});
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, ShaderPropertyId property_id, Texture *texture,
                                   ECubemapFace face, u16 mipmap)
    {
        if (texture == nullptr || !IsKernelValid(kernel))
            return;
        const u32 sub_res = face == ECubemapFace::kUnknown ? texture->CalculateSubResIndex(mipmap, 0u) :
                                                             texture->CalculateSubResIndex(face, mipmap, 0u);
        u16 depth_slice = static_cast<u16>(-1);
        if (texture->Dimension() == ETextureDimension::kTex3D)
            depth_slice = dynamic_cast<Texture3D *>(texture)->Depth();
        SetResourceBinding(kernel, property_id, texture,
                           ComputeBindParams{static_cast<u16>(face), mipmap, depth_slice, sub_res});
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
        const ComputeBindParams params{static_cast<u16>(ECubemapFace::kUnknown), 0u, depth_slice, UINT32_MAX};
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
        SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture, face, mipmap);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture, u16 mipmap)
    {
        SetTexture(kernel, name, texture, ECubemapFace::kUnknown, mipmap);
    }

    void ComputeShader::SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture,
                                   ECubemapFace face, u16 mipmap)
    {
        SetTexture(kernel, ShaderPropertyRegistry::Get().Intern(name), texture, face, mipmap);
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
        SetFloat(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void ComputeShader::SetFloat(ShaderPropertyId property_id, f32 value)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
                memcpy(_cbuf_data + binding->_buffer_offset, &value, std::min<u32>(sizeof(value), binding->_buffer_size));
        }
    }
    void ComputeShader::SetFloats(const String& name,Vector<f32> values)
    {
        SetFloats(ShaderPropertyRegistry::Get().Intern(name), values);
    }

    void ComputeShader::SetFloats(ShaderPropertyId property_id, const Vector<f32> &values)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding == nullptr || !(binding->_resource_type & EBindResDescType::kCBufferFloats))
                continue;
            if (binding->_array_size > 1u)
            {
                const u16 count = std::min<u16>(binding->_array_size, static_cast<u16>(values.size()));
                for (u16 index = 0u; index < count; ++index)
                    memcpy(_cbuf_data + binding->_buffer_offset + index * 16u, &values[index], sizeof(f32));
            }
            else
            {
                const u32 size = std::min<u32>(binding->_buffer_size, static_cast<u32>(values.size() * sizeof(f32)));
                memcpy(_cbuf_data + binding->_buffer_offset, values.data(), size);
            }
        }
    }

    void ComputeShader::SetBool(const String &name, bool value)
    {
        SetBool(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void ComputeShader::SetBool(ShaderPropertyId property_id, bool value)
    {
        const f32 bool_value = value ? 1.0f : 0.0f;
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferBool)
                memcpy(_cbuf_data + binding->_buffer_offset, &bool_value,
                       std::min<u32>(sizeof(bool_value), binding->_buffer_size));
        }
    }

    void ComputeShader::SetInt(const String &name, i32 value)
    {
        SetInt(ShaderPropertyRegistry::Get().Intern(name), value);
    }

    void ComputeShader::SetInt(ShaderPropertyId property_id, i32 value)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding == nullptr)
                continue;
            if (binding->_resource_type & EBindResDescType::kCBufferInt)
                memcpy(_cbuf_data + binding->_buffer_offset, &value,
                       std::min<u32>(sizeof(value), binding->_buffer_size));
            else if (binding->_resource_type & EBindResDescType::kCBufferUInt)
            {
                const u32 uint_value = static_cast<u32>(value);
                memcpy(_cbuf_data + binding->_buffer_offset, &uint_value,
                       std::min<u32>(sizeof(uint_value), binding->_buffer_size));
            }
        }
    }
    void ComputeShader::SetInts(const String &name, Vector<i32> values)
    {
        SetInts(ShaderPropertyRegistry::Get().Intern(name), values);
    }

    void ComputeShader::SetInts(ShaderPropertyId property_id, const Vector<i32> &values)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding == nullptr || (!(binding->_resource_type & EBindResDescType::kCBufferInts) &&
                                       !(binding->_resource_type & EBindResDescType::kCBufferUInts)))
                continue;
            const u16 count = binding->_array_size > 1u
                                  ? std::min<u16>(binding->_array_size, static_cast<u16>(values.size()))
                                  : static_cast<u16>(values.size());
            const u32 size = std::min<u32>(binding->_buffer_size, count * sizeof(i32));
            if (binding->_resource_type & EBindResDescType::kCBufferUInts)
            {
                Vector<u32> uint_values;
                uint_values.reserve(count);
                for (u16 index = 0u; index < count; ++index)
                    uint_values.emplace_back(static_cast<u32>(values[index]));
                memcpy(_cbuf_data + binding->_buffer_offset, uint_values.data(), size);
            }
            else
                memcpy(_cbuf_data + binding->_buffer_offset, values.data(), size);
        }
    }

    void ComputeShader::SetVector(const String &name, Vector4f vector)
    {
        SetVector(ShaderPropertyRegistry::Get().Intern(name), vector);
    }

    void ComputeShader::SetVector(ShaderPropertyId property_id, Vector4f vector)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding == nullptr)
                continue;
            const u32 size = std::min<u32>(sizeof(vector), binding->_buffer_size);
            if (binding->_resource_type & EBindResDescType::kCBufferInts)
            {
                const Vector4Int int_value(static_cast<i32>(vector.x), static_cast<i32>(vector.y),
                                            static_cast<i32>(vector.z), static_cast<i32>(vector.w));
                memcpy(_cbuf_data + binding->_buffer_offset, &int_value, size);
            }
            else if (binding->_resource_type & EBindResDescType::kCBufferUInts)
            {
                const Vector4UInt uint_value(static_cast<u32>(vector.x), static_cast<u32>(vector.y),
                                              static_cast<u32>(vector.z), static_cast<u32>(vector.w));
                memcpy(_cbuf_data + binding->_buffer_offset, &uint_value, size);
            }
            else if (binding->_resource_type & EBindResDescType::kCBufferFloats)
                memcpy(_cbuf_data + binding->_buffer_offset, &vector, size);
        }
    }

    void ComputeShader::SetVectorArray(const String& name,const Vector<Vector4f>& vectors)
    {
        SetVectorArray(ShaderPropertyRegistry::Get().Intern(name), vectors);
    }

    void ComputeShader::SetVectorArray(ShaderPropertyId property_id, const Vector<Vector4f> &vectors)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding == nullptr || !(binding->_resource_type & EBindResDescType::kCBufferFloats))
                continue;
            const u32 count = binding->_array_size > 1u
                                  ? std::min<u32>(binding->_array_size, static_cast<u32>(vectors.size()))
                                  : static_cast<u32>(vectors.size());
            memcpy(_cbuf_data + binding->_buffer_offset,
                   vectors.data(), std::min<u32>(binding->_buffer_size, count * sizeof(Vector4f)));
        }
    }
    void ComputeShader::SetVectorArray(const String& name,Vector4f* vectors,u16 num)
    {
        SetVectorArray(ShaderPropertyRegistry::Get().Intern(name), Vector<Vector4f>(vectors, vectors + num));
    }

    void ComputeShader::SetBuffer(const String &name, ConstantBuffer *buf)
    {
        SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buf);
    }

    void ComputeShader::SetBuffer(ShaderPropertyId property_id, ConstantBuffer *buf)
    {
        if (buf == nullptr)
            return;
        for (const auto &kernel : _kernels)
            SetResourceBinding(kernel._id, property_id, buf, ComputeBindParams{});
    }
    void ComputeShader::SetBuffer(const String &name, GPUBuffer *buf)
    {
        SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buf);
    }

    void ComputeShader::SetBuffer(ShaderPropertyId property_id, GPUBuffer *buf)
    {
        if (buf == nullptr)
            return;
        for (const auto &kernel : _kernels)
            SetResourceBinding(kernel._id, property_id, buf, ComputeBindParams{});
    }

    void ComputeShader::SetBuffer(ComputeShaderKernelId kernel,const String &name, ConstantBuffer *buf)
    {
        SetBuffer(kernel, ShaderPropertyRegistry::Get().Intern(name), buf);
    }

    void ComputeShader::SetBuffer(ComputeShaderKernelId kernel, ShaderPropertyId property_id, ConstantBuffer *buf)
    {
        if (buf != nullptr && IsKernelValid(kernel))
            SetResourceBinding(kernel, property_id, buf, ComputeBindParams{});
    }
    void ComputeShader::SetBuffer(ComputeShaderKernelId kernel,const String &name, GPUBuffer *buf)
    {
        SetBuffer(kernel, ShaderPropertyRegistry::Get().Intern(name), buf);
    }

    void ComputeShader::SetBuffer(ComputeShaderKernelId kernel, ShaderPropertyId property_id, GPUBuffer *buf)
    {
        if (buf != nullptr && IsKernelValid(kernel))
            SetResourceBinding(kernel, property_id, buf, ComputeBindParams{});
    }

    void ComputeShader::SetAccelerationStructure(ComputeShaderKernelId kernel, const String &name,
                                                 RayTracingScene *scene)
    {
        SetAccelerationStructure(kernel, ShaderPropertyRegistry::Get().Intern(name), scene);
    }

    void ComputeShader::SetAccelerationStructure(ComputeShaderKernelId kernel, ShaderPropertyId property_id,
                                                 RayTracingScene *scene)
    {
        if (scene != nullptr && IsKernelValid(kernel))
            SetResourceBinding(kernel, property_id, scene, ComputeBindParams{});
    }

    void ComputeShader::SetAccelerationStructure(const String &name, RayTracingScene *scene)
    {
        if (scene == nullptr)
            return;
        const auto property_id = ShaderPropertyRegistry::Get().Intern(name);
        for (const auto &kernel : _kernels)
            SetResourceBinding(kernel._id, property_id, scene, ComputeBindParams{});
    }

    void ComputeShader::SetMatrix(const String& name,Matrix4x4f mat)
    {
        SetMatrix(ShaderPropertyRegistry::Get().Intern(name), mat);
    }

    void ComputeShader::SetMatrix(ShaderPropertyId property_id, Matrix4x4f mat)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferMatrix)
                memcpy(_cbuf_data + binding->_buffer_offset, &mat,
                       std::min<u32>(sizeof(mat), binding->_buffer_size));
        }
    }

    void ComputeShader::SetMatrixArray(const String& name,Vector<Matrix4x4f> matrix_arr)
    {
        SetMatrixArray(ShaderPropertyRegistry::Get().Intern(name), matrix_arr);
    }

    void ComputeShader::SetMatrixArray(ShaderPropertyId property_id, const Vector<Matrix4x4f> &matrix_arr)
    {
        for (auto &kernel : _kernels)
        {
            const auto &variant = kernel._variants[kernel._active_variant];
            const auto *binding = variant._binding_layout == nullptr ? nullptr : variant._binding_layout->Find(property_id);
            if (binding == nullptr || !(binding->_resource_type & EBindResDescType::kCBufferMatrix))
                continue;
            const u32 count = binding->_array_size > 1u
                                  ? std::min<u32>(binding->_array_size, static_cast<u32>(matrix_arr.size()))
                                  : static_cast<u32>(matrix_arr.size());
            memcpy(_cbuf_data + binding->_buffer_offset,
                   matrix_arr.data(), std::min<u32>(binding->_buffer_size, count * sizeof(Matrix4x4f)));
        }
    }

    void ComputeShader::GetThreadNum(ComputeShaderKernelId kernel, u16 &x, u16 &y, u16 &z) const
    {
        if (!IsKernelValid(kernel))
            return;
        const auto kernel_index = ResolveKernelIndex(kernel);
        x = _kernels[kernel_index]._thread_num.x;
        y = _kernels[kernel_index]._thread_num.y;
        z = _kernels[kernel_index]._thread_num.z;
    }

    bool Ailu::ComputeShader::IsDependencyFile(const WString &sys_path) const
    {
        const WString normalized_path = NormalizeShaderDependencyPath(sys_path);
        for (const auto &k: _kernels)
        {
            for (const auto &it: k._variants)
            {
                auto &variant = it.second;
                if (variant._all_dep_file_pathes.contains(normalized_path))
                    return true;
            }
        }
        return false;
    }
    bool ComputeShader::Compile(EShaderCompilePolicy policy)
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
                is_succeed &= Compile(kernel_index, v.first, policy);
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
        if (cs_ele._binding_layout == nullptr)
            return;
        for (const auto &[property_id, bind_info] : cs_ele._binding_layout->Bindings())
        {
            if (bind_info._bind_slot < 0 || bind_info._bind_slot >= cur_state._bind_res.size())
                continue;
            if (bind_info._resource_type == EBindResDescType::kConstBuffer && bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagInternal)
            {
                ConstantBuffer *cb = ConstBufferPool::Acquire(kCBufferSize);
                cb->SetData(_cbuf_data);
                cur_state._bind_res[bind_info._bind_slot] = cb;
                cur_state._bind_params[bind_info._bind_slot] = ComputeBindParams{};
                cur_state._bind_params[bind_info._bind_slot]._is_internal_cbuf = true;
                cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
            }
            else if (const auto *binding = FindResourceBinding(kernel, property_id); binding != nullptr && binding->_resource != nullptr)
            {
                cur_state._bind_res[bind_info._bind_slot] = binding->_resource;
                cur_state._bind_params[bind_info._bind_slot] = binding->_params;
                cur_state._bind_res_priority[bind_info._bind_slot] = PipelineResource::kPriorityLocal;
            }
            cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot,bind_info._bind_slot);
        }
        const auto global_resources = s_global_res_registry.CaptureSnapshot();
        for (const auto &[property_id, resource_binding] : global_resources._resources)
        {
            const auto *bind_info = cs_ele._binding_layout->Find(property_id);
            if (bind_info == nullptr || bind_info->_bind_slot < 0 || bind_info->_bind_slot >= cur_state._bind_res.size() ||
                resource_binding._resource == nullptr || cur_state._bind_res_priority[bind_info->_bind_slot] > PipelineResource::kPriorityGlobal)
                continue;
            cur_state._bind_res[bind_info->_bind_slot] = resource_binding._resource;
            cur_state._bind_params[bind_info->_bind_slot] = ComputeBindParams{};
            if (auto *texture3d = dynamic_cast<Texture3D *>(resource_binding._resource))
                cur_state._bind_params[bind_info->_bind_slot]._slice = texture3d->Depth();
            if (bind_info->_resource_type == EBindResDescType::kTexture2D || bind_info->_resource_type == EBindResDescType::kTexture3D ||
                bind_info->_resource_type == EBindResDescType::kCubeMap)
                cur_state._bind_params[bind_info->_bind_slot]._view_index = Texture::kMainSRVIndex;
            cur_state._bind_res_priority[bind_info->_bind_slot] = PipelineResource::kPriorityGlobal;
            cur_state._max_bind_slot = std::max<u16>(cur_state._max_bind_slot, bind_info->_bind_slot);
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
        std::shared_lock global_resource_lock(s_global_resource_mutex);
        snapshot = ComputeDispatchSnapshot{};
        if (!IsKernelValid(kernel))
            return;
        const auto kernel_index = ResolveKernelIndex(kernel);
        const auto variant_hash = ResolveActiveVariant(kernel);
        const auto &kernel_variant = _kernels[kernel_index]._variants[variant_hash];
        if (kernel_variant._binding_layout == nullptr)
            return;
        memcpy(snapshot._cbuf_data.data(), _cbuf_data, sizeof(_cbuf_data));
        for (const auto &[property_id, value] : s_global_floats)
        {
            const auto *binding = kernel_variant._binding_layout->Find(property_id);
            if (binding != nullptr && binding->_resource_type & EBindResDescType::kCBufferFloat)
                memcpy(snapshot._cbuf_data.data() + binding->_buffer_offset, &value,
                       std::min<u32>(sizeof(value), binding->_buffer_size));
        }
        for (const auto &[property_id, value] : s_global_ints)
        {
            const auto *binding = kernel_variant._binding_layout->Find(property_id);
            if (binding != nullptr)
            {
                if (binding->_resource_type & EBindResDescType::kCBufferInt)
                    memcpy(snapshot._cbuf_data.data() + binding->_buffer_offset, &value,
                           std::min<u32>(sizeof(value), binding->_buffer_size));
                else if (binding->_resource_type & EBindResDescType::kCBufferUInt)
                {
                    const u32 uint_value = static_cast<u32>(value);
                    memcpy(snapshot._cbuf_data.data() + binding->_buffer_offset, &uint_value,
                           std::min<u32>(sizeof(uint_value), binding->_buffer_size));
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
                                  const ComputeBindParams &params,
                                  const PipelineResource::AddiInfo &addi_info = PipelineResource::AddiInfo{})
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
            entry->_data_version = {};
            if (auto *buffer = dynamic_cast<GPUBuffer *>(resource))
                entry->_data_version = buffer->CurrentDataVersion();
            entry->_resource_type = resource_type;
            entry->_priority = priority;
            entry->_face = static_cast<u16>(params._face);
            entry->_mipmap = params._mipmap;
            entry->_slice = params._slice;
            entry->_view_index = params._view_index;
            entry->_sub_res = params._sub_res;
            entry->_is_internal_cbuf = params._is_internal_cbuf;
            entry->_addi_info = addi_info;
            if (entry->_view_index == static_cast<u16>(-1) &&
                (resource_type == EBindResDescType::kTexture2D || resource_type == EBindResDescType::kCubeMap ||
                 resource_type == EBindResDescType::kTexture3D || resource_type == EBindResDescType::kUAVTexture2D ||
                 resource_type == EBindResDescType::kRWTexture3D))
            {
                const bool is_uav = resource_type == EBindResDescType::kUAVTexture2D ||
                                    resource_type == EBindResDescType::kRWTexture3D;
                const auto view_type = is_uav ? Texture::ETextureViewType::kUAV : Texture::ETextureViewType::kSRV;
                auto *texture = dynamic_cast<Texture *>(resource);
                // A default array binding must expose every slice to shader-side array indexing (for example CSM).
                // Explicit subresource bindings keep their single-slice view.
                const bool is_default_array = texture != nullptr &&
                    texture->Dimension() == ETextureDimension::kTex2DArray && params._sub_res == UINT32_MAX;
                if (!is_uav && params._face == static_cast<u16>(ECubemapFace::kUnknown) && texture != nullptr &&
                    (texture->Dimension() == ETextureDimension::kCube || is_default_array))
                    entry->_view_index = Texture::kMainSRVIndex;
                else if (auto *texture3d = dynamic_cast<Texture3D *>(resource))
                    entry->_view_index = texture3d->CalculateViewIndex(
                        view_type, params._mipmap, params._slice);
                else
                    entry->_view_index = static_cast<Texture *>(resource)->CalculateViewIndex(
                        view_type, static_cast<ECubemapFace>(params._face), params._mipmap, 0u);
            }
        };

        for (const auto &[property_id, bind_info] : kernel_variant._binding_layout->Bindings())
        {
            const u16 slot = static_cast<u16>(bind_info._bind_slot);
            if (bind_info._bind_slot < 0 || slot >= 32u)
                continue;
            if (bind_info._resource_type == EBindResDescType::kConstBuffer
                && bind_info._bind_flag & ShaderBindResourceInfo::kBindFlagInternal)
            {
                auto *cb = ConstBufferPool::Acquire(kCBufferSize);
                cb->SetData(snapshot._cbuf_data.data());
                ComputeBindParams params{};
                params._is_internal_cbuf = true;
                apply_resource(slot, cb, bind_info._resource_type, PipelineResource::kPriorityLocal, params);
            }
            else if (const auto *binding = FindResourceBinding(kernel, property_id); binding != nullptr && binding->_resource != nullptr)
            {
                apply_resource(slot, binding->_resource, bind_info._resource_type, PipelineResource::kPriorityLocal, binding->_params);
            }
        }

        auto apply_global_resource = [&](ShaderPropertyId property_id, GpuResource *resource, const ComputeBindParams &params)
        {
            if (resource == nullptr)
                return;
            const auto *bind_info = kernel_variant._binding_layout->Find(property_id);
            if (bind_info != nullptr)
                apply_resource(static_cast<u16>(bind_info->_bind_slot), resource, bind_info->_resource_type,
                               PipelineResource::kPriorityGlobal, params);
        };

        const auto global_resources = s_global_res_registry.CaptureSnapshot();
        for (const auto &[property_id, global_binding] : global_resources._resources)
        {
            auto *resource = global_binding._resource;
            if (resource == nullptr)
                continue;
            u16 depth_slice = static_cast<u16>(-1);
            if (auto *texture3d = dynamic_cast<Texture3D *>(resource))
                depth_slice = texture3d->Depth();
            apply_global_resource(property_id, resource,
                                  ComputeBindParams{static_cast<u16>(ECubemapFace::kUnknown), 0u, depth_slice, UINT32_MAX});
        }

        if (command_resources != nullptr)
        {
            for (const auto &[property_id, command_binding] : *command_resources)
            {
                if (command_binding._resource == nullptr)
                    continue;
                const auto *bind_info = kernel_variant._binding_layout->Find(property_id);
                if (bind_info == nullptr || bind_info->_bind_slot < 0 || bind_info->_bind_slot >= 32)
                    continue;
                const auto resource_type = command_binding._resource_type == EBindResDescType::kConstBufferRaw
                                                ? EBindResDescType::kConstBufferRaw : bind_info->_resource_type;
                apply_resource(static_cast<u16>(bind_info->_bind_slot), command_binding._resource, resource_type,
                               PipelineResource::kPriorityCmd, ComputeBindParams{}, command_binding._addi_info);
            }
        }
        for (u16 i = 0u; i < captured_count; ++i)
            snapshot._entries[i] = captured[i];
        snapshot._entry_count = captured_count;
        snapshot._variant_hash = variant_hash;
        snapshot._is_ready = _variant_state[kernel_index][variant_hash] == EShaderVariantState::kReady;
    }


    bool ComputeShader::Compile(u16 kernel_index, ShaderVariantHash variant_hash, EShaderCompilePolicy policy)
    {
        _is_compiling.store(true);
        const auto previous_state = _variant_state[kernel_index][variant_hash];
        bool is_succeed = RHICompileImpl(kernel_index, variant_hash, policy);
        if (is_succeed || previous_state == EShaderVariantState::kReady)
            _variant_state[kernel_index][variant_hash] = EShaderVariantState::kReady;
        else
            _variant_state[kernel_index][variant_hash] = EShaderVariantState::kError;
        _is_compiling.store(false);
        return is_succeed;
    }
    bool ComputeShader::RHICompileImpl(u16 kernel_index, ShaderVariantHash variant_hash, EShaderCompilePolicy policy)
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
        auto previous_kernels = std::move(_kernels);
        auto previous_variant_state = std::move(_variant_state);
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
                    v._all_dep_file_pathes.insert(NormalizeShaderDependencyPath(_src_file_path));
                    cur_kernel._variants.insert(std::make_pair(kw_hash, v));
                }
            }
            if (!cur_kernel._variants.contains(0))
            {
                KernelElement::KernelVariant v;
                v._all_dep_file_pathes.insert(NormalizeShaderDependencyPath(_src_file_path));
                cur_kernel._variants.insert(std::make_pair(0, v));
            }
            Map<ShaderVariantHash, EShaderVariantState> cur_kernel_variant_state;
            const auto previous_kernel_it = std::find_if(previous_kernels.begin(), previous_kernels.end(),
                                                         [&](const KernelElement &kernel)
                                                         { return kernel._name == cur_kernel._name; });
            const size_t previous_kernel_index = previous_kernel_it == previous_kernels.end()
                                                     ? previous_kernels.size()
                                                     : static_cast<size_t>(std::distance(previous_kernels.begin(), previous_kernel_it));
            if (previous_kernel_it != previous_kernels.end())
            {
                cur_kernel._thread_num = previous_kernel_it->_thread_num;
                for (auto &[variant_hash, variant] : cur_kernel._variants)
                {
                    for (const auto &[old_variant_hash, old_variant] : previous_kernel_it->_variants)
                    {
                        if (variant._active_keywords != old_variant._active_keywords)
                            continue;
                        HashMap<String, String> root_cbuffer_names;
                        for (const auto &[name, info] : old_variant._bind_res_infos)
                        {
                            if (info._p_root_cbuf != nullptr)
                                root_cbuffer_names[name] = info._p_root_cbuf->_name;
                        }
                        variant = old_variant;
                        for (auto &[name, info] : variant._bind_res_infos)
                        {
                            if (auto root_name_it = root_cbuffer_names.find(name);
                                root_name_it != root_cbuffer_names.end())
                            {
                                if (auto root_it = variant._bind_res_infos.find(root_name_it->second);
                                    root_it != variant._bind_res_infos.end())
                                    info._p_root_cbuf = &root_it->second;
                            }
                        }
                        if (previous_kernel_index < previous_variant_state.size())
                        {
                            if (auto state_it = previous_variant_state[previous_kernel_index].find(old_variant_hash);
                                state_it != previous_variant_state[previous_kernel_index].end())
                                cur_kernel_variant_state[variant_hash] = state_it->second;
                        }
                        break;
                    }
                }
            }
            for (auto &it: cur_kernel._variants)
            {
                auto &[vhash, variant] = it;
                if (!cur_kernel_variant_state.contains(vhash))
                    cur_kernel_variant_state[vhash] = EShaderVariantState::kNotReady;
            }
            _variant_state.emplace_back(std::move(cur_kernel_variant_state));
            _kernel_id_to_index[cur_kernel._id] = kernel_index;
            _kernels.emplace_back(std::move(cur_kernel));
        }
        if (_kernels.empty())
        {
            _kernels = std::move(previous_kernels);
            _variant_state = std::move(previous_variant_state);
            _kernel_id_to_index.clear();
            for (u16 index = 0u; index < _kernels.size(); ++index)
                _kernel_id_to_index[_kernels[index]._id] = index;
            return false;
        }
        return true;
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
