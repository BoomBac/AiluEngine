#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include "Buffer.h"
#include "Framework/Math/ALMath.hpp"
#include "Objects/Object.h"
#include "PipelineState.h"
#include "Texture.h"
#include "CoreType.h"
#include "MaterialDrawState.h"
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include "generated/Shader.gen.h"


namespace Ailu::Render
{
    class RHICommandBuffer;

    struct ShaderCommand
    {
        inline static const String kName = "name";
        inline static const String kVSEntry = "Vert";
        inline static const String kPSEntry = "Pixel";
        inline static const String kGSEntry = "Geometry";
        inline static const String kCull = "Cull";
        inline static const String kQueue = "Queue";
        inline static const String kTopology = "Topology";
        inline static const String kBlend = "Blend";
        inline static const String KFill = "Fill";
        inline static const String kZTest = "ZTest";
        inline static const String kZWrite = "ZWrite";
        inline static const String kStencil = "Stencil";
        inline static const String kColorMask = "ColorMask";
        inline static const String kConservative = "Conservative";
        static struct Queue
        {
            inline static const String kGeometry = "Geometry";
            inline static const String kTransplant = "Transparent";
        } kQueueValue;
        static struct Cull
        {
            inline static const String kNone = "none";
            inline static const String kFront = "Front";
            inline static const String kBack = "Back";
        } kCullValue;
        static struct Topology
        {
            inline static const String kTriangle = "Triangle";
            inline static const String kTriangleStrip = "TriangleStrip";
            inline static const String kLine = "Line";
        } kTopologyValue;
        static struct BlendFactor
        {
            inline static const String kSrc = "Src";
            inline static const String kOneMinusSrc = "OneMinusSrc";
            inline static const String kOne = "One";
            inline static const String kZero = "Zero";
        } kBlendFactorValue;
        static struct Fill
        {
            inline static const String kSolid = "Solid";
            inline static const String kWireframe = "Wireframe";
        } kFillValue;
        static struct Comparison
        {
            inline static const String kOff = "Off";
            inline static const String kLess = "Less";
            inline static const String kLEqual = "LEqual";
            inline static const String kEqual = "Equal";
            inline static const String kGreater = "Greater";
            inline static const String kGEqual = "GEqual";
            inline static const String kNotEqual = "NotEqual";
            inline static const String kAlways = "Always";
            inline static const String kNevel = "Nevel";
        } kZTestValue;
        static struct ZWrite
        {
            inline static const String kOff = "Off";
            inline static const String kOn = "On";
        } kZWriteValue;
        static struct StencilState
        {
            inline static const String kStencilRef = "Ref";
            inline static const String kStencilComp = "Comp";
            inline static const String kStencilPass = "Pass";
            static struct StencilOperation
            {
                inline static const String kReplace = "Replace";
                inline static const String kKeep = "Keep";//z and stencil all passed
                inline static const String kZero = "Zero";
                inline static const String kIncr = "Incr";
                inline static const String kDecr = "Decr";
            } StencilPassValue;
        } kStencilStateValue;
        static struct Conservative
        {
            inline static const String kOff = "Off";
            inline static const String kOn = "On";
        } kConservativeValue;
    };

    struct GlobalResourceBinding
    {
        GpuResource *_resource = nullptr;
    };

    class ShaderGlobalResourceRegistry
    {
    public:
        void SetTexture(ShaderPropertyId property_id, Texture *texture)
        {
            SetResource(property_id, texture);
        }

        void SetBuffer(ShaderPropertyId property_id, GpuResource *buffer)
        {
            SetResource(property_id, buffer);
        }

        u64 Version() const { return _binding_version; }
        u64 LayoutVersion() const { return _layout_version; }
        u64 BindingVersion() const { return _binding_version; }
        const HashMap<ShaderPropertyId, GlobalResourceBinding> &Resources() const { return _resources; }

    private:
        void SetResource(ShaderPropertyId property_id, GpuResource *resource)
        {
            const bool is_new_resource_id = !_resources.contains(property_id);
            auto &binding = _resources[property_id];
            if (!is_new_resource_id && binding._resource == resource)
                return;

            binding._resource = resource;
            if (is_new_resource_id)
                ++_layout_version;
            ++_binding_version;
        }

        HashMap<ShaderPropertyId, GlobalResourceBinding> _resources;
        u64 _layout_version = 1u;
        u64 _binding_version = 1u;
    };


    using ShaderHash = u64;
    using ShaderVariantHash = u32;

    class ShaderVariantMgr
    {
    public:
        void BuildIndex(const Vector<std::set<String>>& all_keywords);
        ShaderVariantHash GetVariantHash(const std::set<String> &keywords) const;
        ShaderVariantHash GetVariantHash(const std::set<String> &keywords, std::set<String> &active_kw_seq) const;
        std::set<String> KeywordsSameGroup(const String &kw) const;

    private:
        inline static const u16 kBitNumPerGroup = 32u;
        using ShaderVariantBits = Vector<u32>;

        struct KeywordInfo
        {
            u16 _group_id;
            u16 _inner_group_id;
            u16 _global_id;
        };
        //关键字-组id-组内id
        Map<String, KeywordInfo> _keyword_group_ids;
    };

    struct AILU_API ShaderPass
    {
        static bool IsPassKeyword(const ShaderPass &pass, const String &kw)
        {
            bool is_pass_kw = false;
            for (auto &kw_group: pass._keywords)
            {
                if (kw_group.find(kw) != kw_group.end())
                {
                    is_pass_kw = true;
                    break;
                }
            }
            return is_pass_kw;
        }
        struct PassVariant
        {
            u8 _vertex_input_num = 0u;
            i8 _per_mat_buf_bind_slot = -1;
            i8 _per_frame_buf_bind_slot = -1;
            i8 _per_pass_buf_bind_slot = -1;
            VertexInputLayout _pipeline_input_layout;
            // Reflection/build-time resource table. Runtime material lookup should use _binding_layout.
            ShaderReflectionResourceMap _bind_res_infos;
            Ref<const ShaderBindingLayout> _binding_layout;
            std::set<String> _active_keywords;
            ShaderHash _shader_hash;
        };
        //pass info
        u16 _index;
        String _name;
        String _tag;
        //shader info
        String _vert_entry, _pixel_entry, _geometry_entry;
        WString _vert_src_file, _pixel_src_file, _geom_src_file;
        RasterizerState _pipeline_raster_state;
        DepthStencilState _pipeline_ds_state;
        ETopology _pipeline_topology;
        BlendState _pipeline_blend_state;
        u32 _render_queue;
        std::set<WString> _source_files;
        Vector<std::set<String>> _keywords;
        ShaderVariantMgr _variant_mgr;
        List<ShaderPropertyInfo> _shader_prop_infos;
        Map<ShaderVariantHash, PassVariant> _variants;
    };

    enum class EShaderVariantState
    {
        kNotReady,
        kCompiling,
        kError,
        kReady
    };

    ACLASS()
    class AILU_API Shader : public Object
    {
        GENERATED_BODY()
        friend class Material;
        struct ShaderHashStruct
        {
            struct BitDesc
            {
                u8 _pos;
                u8 _size;
            };
            inline static const BitDesc kShader = {0, 6};
            inline static const BitDesc kPass = {6, 3};
            inline static const BitDesc kVariant = {9, 32};
        };
        
    public:
        inline static Weak<Shader> s_p_defered_standart_lit;
        inline static const u16 kRenderQueueOpaque = 2000;
        inline static const u16 kRenderQueueAlphaTest = 2500;
        inline static const u16 kRenderQueueTransparent = 3000;
        inline static const u16 kRenderQueueEnd = 4000;

    public:
        static Ref<Shader> Create(const WString &sys_path);
        /// @brief 构造一个64位的shader哈希
        /// @param shader_id 0~5 6位
        /// @param pass_hash 6~8 3位
        /// @param variant_hash 9~41 32位
        /// @return 64位哈希，后23位暂时没用
        static ShaderHash ConstructHash(const u32 &shader_id, const u16 &pass_hash, ShaderVariantHash variant_hash = 0u);
        static void ExtractInfoFromHash(const ShaderHash &shader_hash, u32 &shader_id, u16 &pass_hash, ShaderVariantHash &variant_hash);

        static void SetGlobalTexture(const String &name, Texture *texture);
        static void SetGlobalTexture(const String &name, RTHandle handle);
        static void SetGlobalBuffer(const String &name, ConstantBuffer *buffer);
        static void SetGlobalMatrix(const String &name, Matrix4x4f *matrix);
        static void SetGlobalMatrixArray(const String &name, Matrix4x4f *matrix, u32 num);
        static void EnableGlobalKeyword(const String &keyword);
        static void DisableGlobalKeyword(const String &keyword);
        static const std::set<String>& GetPreDefinedMacros() {return s_predefined_macros;};

        //0 is normal, 4001 is compiling, 4000 is error,same with render queue id
        static u32 GetShaderState(Shader *shader, u16 pass_index, ShaderVariantHash variant_hash);
        static const ShaderGlobalResourceRegistry &GlobalResourceRegistry() { return s_global_res_registry; }
        static ShaderPropertyId PropertyID(const String &name) { return ShaderPropertyRegistry::Get().Intern(name); }
        Shader() = default;
        Shader(const WString &sys_path);
        virtual ~Shader() = default;
        virtual void Bind(RHICommandBuffer *cmd, u16 pass_index, ShaderVariantHash variant_hash);
        //call this before compile
        virtual bool PreProcessShader();
        virtual bool Compile(u16 pass_id, ShaderVariantHash variant_hash, bool is_load_cache = true);
        //compile all pass's variants
        virtual bool Compile(bool is_load_cache = true);
        virtual void *GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash);
        const WString &GetMainSourceFile() { return _src_file_path; }
        void SetVertexShader(u16 pass_index, const WString &sys_path, const String &entry);
        void SetPixelShader(u16 pass_index, const WString &sys_path, const String &entry);

        const ShaderPass &GetPassInfo(u16 pass_index) const
        {
            AL_ASSERT(pass_index < _passes.size());
            return _passes[pass_index];
        }
        const i16 PassCount() const { return static_cast<i16>(_passes.size()); }
        i16 FindPass(String pass_name);
        //根据任意字符序列构建变体hash，并返回合法的序列
        ShaderVariantHash ConstructVariantHash(u16 pass_index, const std::set<String> &kw_seq, std::set<String> &active_kw_seq) const;
        ShaderVariantHash ConstructVariantHash(u16 pass_index, const std::set<String> &kw_seq) const;
        const i8 &GetPerMatBufferBindSlot(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._per_mat_buf_bind_slot; }
        const i8 &GetPerFrameBufferBindSlot(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._per_frame_buf_bind_slot; }
        const i8 &GetPerPassBufferBindSlot(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._per_pass_buf_bind_slot; }
        const ShaderReflectionResourceMap &GetReflectionBindResInfo(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._bind_res_infos; }
        const ShaderReflectionResourceMap &GetBindResInfo(u16 pass_index, ShaderVariantHash variant_hash) const { return GetReflectionBindResInfo(pass_index, variant_hash); }
        const ShaderBindingLayout *GetBindingLayout(u16 pass_index, ShaderVariantHash variant_hash) const
        {
            const auto &layout = _passes[pass_index]._variants.at(variant_hash)._binding_layout;
            return layout.get();
        }

        const VertexInputLayout &PipelineInputLayout(u16 pass_index = 0, ShaderVariantHash variant_hash = 0) const { return _passes[pass_index]._variants.at(variant_hash)._pipeline_input_layout; };
        const RasterizerState &PipelineRasterizerState(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_raster_state; };
        const DepthStencilState &PipelineDepthStencilState(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_ds_state; };
        const ETopology &PipelineTopology(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_topology; };
        const BlendState &PipelineBlendState(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_blend_state; };
        const u32 &RenderQueue(u16 pass_index = 0) const { return _passes[pass_index]._render_queue; };
        const List<ShaderPropertyInfo> &GetShaderPropertyInfos(u16 pass_index) { return _passes[pass_index]._shader_prop_infos; }
        const Vector<std::set<String>> &PassLocalKeywords(u16 pass_index) const { return _passes[pass_index]._keywords; }
        bool IsKeywordValid(u16 pass_index, const String &kw) const;
        std::set<String> KeywordsSameGroup(u16 pass_index, const String &kw) const;
        std::set<String> ActiveKeywords(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._active_keywords; };
        void SetTopology(ETopology topology) { _topology = topology; }
        ETopology GetTopology() const { return _topology; }
        const Map<String, Vector<String>> &GetKeywordsProps() const { return _keywords_props; }

        Vector<class Material *> GetAllReferencedMaterials();
        void SetCullMode(ECullMode mode);
        ECullMode GetCullMode() const;
        void AddMaterialRef(class Material *mat);
        void RemoveMaterialRef(class Material *mat);
        std::tuple<String &, String &> GetShaderEntry(u16 pass_index = 0)
        {
            AL_ASSERT(pass_index < _passes.size());
            return std::tie(_passes[pass_index]._vert_entry, _passes[pass_index]._pixel_entry);
        }
        EShaderVariantState GetVariantState(u16 pass_index, ShaderVariantHash hash) const
        {
            AL_ASSERT(pass_index < _variant_state.size());
            const auto &info = _variant_state[pass_index];
            const auto it = info.find(hash);
            if (it != info.end())
                return it->second;
            AL_ASSERT(false);
            return EShaderVariantState::kNotReady;
        }
    public:
        u8 _stencil_ref = 0u;

    protected:
        virtual bool RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash, bool is_load_cache);
        void BuildBindingLayout(u16 pass_index, ShaderVariantHash variant_hash);

    protected:
        WString _src_file_path;
        Vector<ShaderPass> _passes;
        Vector<Ref<const ShaderBindingLayout>> _retired_binding_layouts;
        u32 _binding_layout_version = 1u;
        std::set<Material *> _reference_mats;
        Vector<Map<ShaderVariantHash, EShaderVariantState>> _variant_state;
        std::atomic<bool> _is_pass_elements_init = false;
        ETopology _topology = ETopology::kTriangle;
        Map<String, Vector<String>> _keywords_props;

    private:
        void ParserShaderProperty(String &line, List<ShaderPropertyInfo> &props);
        //void ExtractValidShaderProperty(u16 pass_index,ShaderVariantHash variant_hash);
    private:
        inline static std::atomic<u16> s_global_shader_unique_id = 0u;
        inline static std::map<String, std::tuple<Matrix4x4f *, u32>> s_global_matrix_bind_info{};
        inline static std::set<Shader *> s_all_shaders{};
        inline static ShaderGlobalResourceRegistry s_global_res_registry;
        static std::set<String> s_predefined_macros;
    };

    ACLASS()
    class AILU_API ComputeShader : public Object
    {
        GENERATED_BODY()
        struct ComputeBindParams
        {
            ECubemapFace _face;
            u16 _mipmap;
            // depth or array
            u16 _slice;
            u32 _sub_res;
            u16 _view_index = (u16)-1;
            bool _is_internal_cbuf = false;
        };
        struct ComputeResourceBinding
        {
            GpuResource *_resource = nullptr;
            ComputeBindParams _params{};
        };
        struct KernelElement
        {
            static bool IsKernelKeyword(const KernelElement &ker, const String &kw)
            {
                bool is_pass_kw = false;
                for (auto &kw_group: ker._keywords)
                {
                    if (kw_group.find(kw) != kw_group.end())
                    {
                        is_pass_kw = true;
                        break;
                    }
                }
                return is_pass_kw;
            }
            struct KernelVariant
            {
                std::set<WString> _all_dep_file_pathes;
                HashMap<String, ShaderBindResourceInfo> _bind_res_infos{};
                HashMap<String, ShaderBindResourceInfo> _temp_bind_res_infos{};
                std::set<String> _active_keywords;
            };
            ComputeShaderKernelId _id;
            String _name;
            Vector3UInt _thread_num;
            Vector<std::set<String>> _keywords;
            ShaderVariantMgr _variant_mgr;
            HashMap<ShaderVariantHash,KernelVariant> _variants;
            std::set<String> _active_keywords;
            ShaderVariantHash _active_variant;
        };

    public:
        inline static const u32 kCBufferSize = 1024u;
    public:
        AL_FORCE_INLINE static void EnableGlobalKeywords(const String &kw) 
        {
            s_global_active_keywords.insert(kw);
            for(auto& it : s_global_variant_update_map)
                it.second = true;
        };
        AL_FORCE_INLINE static void DisableGlobalKeywords(const String &kw) 
        {
            s_global_active_keywords.erase(kw);
            for(auto& it : s_global_variant_update_map)
                it.second = true;
        };
        AL_FORCE_INLINE static void SetGlobalBuffer(const String &name, ConstantBuffer *buf)
        {
            s_global_cbuffer_bind_info[name] = buf;
        };
        AL_FORCE_INLINE static void SetGlobalBuffer(const String &name, GPUBuffer *buf)
        {
            s_global_buffer_bind_info[name] = buf;
        };
        AL_FORCE_INLINE static void SetGlobalTexture(const String &name, Texture *texture)
        {
            if (!ValidatePersistentGlobalTexture(texture))
                return;
            s_global_textures_bind_info[name] = texture;
        };
        static void SetGlobalTexture(const String &name, RTHandle texture);
        AL_FORCE_INLINE static void SetGlobalFloat(const String &name, f32 value)
        {
            s_global_floats[name] = value;
        };
        AL_FORCE_INLINE static void SetGlobalInt(const String &name, i32 value)
        {
            s_global_ints[name] = value;
        };
        static bool ValidatePersistentGlobalTexture(Texture *texture);
    public:
        static Ref<ComputeShader> Create(const WString &sys_path);
        ComputeShader() = default;
        ComputeShader(const WString &sys_path);
        virtual ~ComputeShader() = default;
        virtual void Bind(RHICommandBuffer *cmd, ComputeShaderKernelId kernel);
        virtual void Bind(RHICommandBuffer *cmd, ComputeShaderKernelId kernel, const ComputeDispatchSnapshot &snapshot)
        { Bind(cmd, kernel); }
        void SetTexture(const String &name, Texture *texture);
        void SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture);
        void SetTexture(ComputeShaderKernelId kernel, const String &name, RTHandle handle);
        void SetTexture(ComputeShaderKernelId kernel, const String &name, RTHandle handle, ECubemapFace face,
                        u16 mipmap);
        void SetTexture(u8 bind_slot, Texture *texture);
        void SetTexture(const String &name, RTHandle handle);
        void SetTexture(const String &name, RTHandle handle, ECubemapFace face, u16 mipmap);
        void SetTexture(const String &name, Texture *texture, u16 mipmap);
        void SetTexture(const String &name, Texture *texture, ECubemapFace face, u16 mipmap);
        void SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture, u16 mipmap);
        void SetTexture(ComputeShaderKernelId kernel, const String &name, Texture *texture, ECubemapFace face,
                        u16 mipmap);
        //void SetTexture(const String &name, RDG::RGHandle handle);
        //void SetTexture(const String &name, RDG::RGHandle handle, ECubemapFace face, u16 mipmap);
        void SetFloat(const String &name, f32 value);
        void SetFloats(const String& name,Vector<f32> values);
        void SetBool(const String &name, bool value);
        void SetInt(const String &name, i32 value);
        void SetInts(const String &name, Vector<i32> values);
        void SetVector(const String &name, Vector4f vector);
        void SetVectorArray(const String& name,const Vector<Vector4f>& vectors);
        void SetVectorArray(const String& name,Vector4f* vectors,u16 num);
        /// @brief 为所有kernel的对应名称设置buffer
        /// @param name 
        /// @param buf 
        void SetBuffer(const String &name, ConstantBuffer *buf);
        /// @brief 为所有kernel的对应名称设置buffer
        /// @param name 
        /// @param buf 
        void SetBuffer(const String &name, GPUBuffer *buf);
        /// @brief 为指定kernel的对应名称设置buffer
        /// @param kernel 
        /// @param name 
        /// @param buf 
        void SetBuffer(ComputeShaderKernelId kernel,const String &name, ConstantBuffer *buf);
        /// @brief 为指定kernel的对应名称设置buffer
        /// @param kernel 
        /// @param name 
        /// @param buf 
        void SetBuffer(ComputeShaderKernelId kernel,const String &name, GPUBuffer *buf);
        void SetMatrix(const String& name,Matrix4x4f mat);
        void SetMatrixArray(const String& name,Vector<Matrix4x4f> matrix_arr);
        void GetThreadNum(ComputeShaderKernelId kernel, u16 &x, u16 &y, u16 &z) const;
        i16 NameToSlot(const String &name, ComputeShaderKernelId kernel,ShaderVariantHash variant_hash) const;
        String SlotToName(ComputeShaderKernelId kernel, u16 slot) const;
        void EnableKeyword(const String &kw);
        void DisableKeyword(const String &kw);
        std::tuple<u16, u16, u16> CalculateDispatchNum(ComputeShaderKernelId kernel, u16 task_num_x, u16 task_num_y, u16 task_num_z) const;
        ComputeShaderKernelId FindKernel(const String &kernel) const
        {
            const auto kernel_id = ComputeShaderKernelRegistry::Get().Find(kernel);
            return IsKernelValid(kernel_id) ? kernel_id : kInvalidComputeShaderKernelId;
        }
        /// @brief 检查所有核的所有变体是否依赖该文件
        /// @param sys_path 系统路径
        /// @return 
        bool IsDependencyFile(const WString &sys_path) const;
        /// @brief 预处理shader，必须在compile之前调用！
        bool Preprocess();
        bool Compile(bool is_load_cache = true);
        ShaderVariantHash ActiveVariant(ComputeShaderKernelId kernel) const { return ResolveActiveVariant(kernel); }
        bool IsKernelValid(ComputeShaderKernelId kernel) const;
        void PushState(ComputeShaderKernelId kernel = kInvalidComputeShaderKernelId);
        void CaptureDispatchState(ComputeShaderKernelId kernel, ComputeDispatchSnapshot &snapshot,
                                  const HashMap<ShaderPropertyId, CommandResourceBinding> *command_resources = nullptr);
    public:
        std::atomic<bool> _is_compiling = false;
    protected:
        bool Compile(u16 kernel_index, ShaderVariantHash variant_hash, bool is_load_cache = true);
        virtual bool RHICompileImpl(u16 kernel_index,ShaderVariantHash variant_hash, bool is_load_cache);
        u16 ResolveKernelIndex(ComputeShaderKernelId kernel) const;
        ShaderVariantHash ResolveActiveVariant(ComputeShaderKernelId kernel) const;
        void SetResourceBinding(ComputeShaderKernelId kernel, ShaderPropertyId property_id, GpuResource *resource,
                                const ComputeBindParams &params);
        const ComputeResourceBinding *FindResourceBinding(ComputeShaderKernelId kernel, ShaderPropertyId property_id) const;

    private:

    protected:
        struct BindState
        {
            ComputeShaderKernelId _kernel;
            u16 _max_bind_slot;
            ShaderVariantHash _variant_hash;
            Array<GpuResource*,32> _bind_res;
            Array<u16,32> _bind_res_priority;
            Array<ComputeBindParams,32> _bind_params;
        };
        inline static std::set<String> s_global_active_keywords;
        inline static HashMap<u32,bool> s_global_variant_update_map{};
        inline static Map<String, ConstantBuffer *> s_global_cbuffer_bind_info{};
        inline static Map<String, GPUBuffer *> s_global_buffer_bind_info{};
        inline static Map<String, Texture *> s_global_textures_bind_info{};
        inline static Map<String, f32> s_global_floats{};
        inline static Map<String, i32> s_global_ints{};
        Vector<KernelElement> _kernels;
        HashMap<ComputeShaderKernelId, u16> _kernel_id_to_index;
        HashMap<ComputeShaderKernelId, HashMap<ShaderPropertyId, ComputeResourceBinding>> _resource_bindings;
        std::set<String> _local_active_keywords;
        WString _src_file_path;
        Vector<Map<ShaderVariantHash, EShaderVariantState>> _variant_state;
        std::mutex _lock;
        ShaderVariantHash _active_variant = 0u;
        //bind_slot:cubemapface,mipmap
        std::unordered_map<u16, ComputeBindParams> _bind_params{};
        bool _is_valid;
        u8 _cbuf_data[kCBufferSize];
        u8 _cache_cbuf_data[kCBufferSize];//重编译之前复制一份缓存，用于恢复已有的数据
        String _internal_cbuf_name="";
        Queue<BindState> _bind_state;
        std::mutex _state_mutex;
    };
}// namespace Ailu


#endif// !SHADER_H__
