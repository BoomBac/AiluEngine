#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include "Buffer.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Reflect.h"
#include "Framework/Math/ALMath.hpp"
#include "Objects/Object.h"
#include "PipelineState.h"
#include "Texture.h"
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>


namespace Ailu
{
    enum class EShaderType : u8
    {
        kVertex,
        kPixel
    };

    struct ShaderPropertyType
    {
        inline static String Vector = "Vector";
        inline static String Float = "Float";
        inline static String Uint = "Uint";
        inline static String Color = "Color";
        inline static String Texture2D = "Texture2D";
        inline static String CubeMap = "CubeMap";
    };

    struct ShaderBindResourceInfo
    {
        inline static const u8 kBindFlagPerObject = 0x01;
        inline static const u8 kBindFlagPerMaterial = 0x02;
        inline static const u8 kBindFlagPerPass = 0x04;
        inline static const u8 kBindFlagPerFrame = 0x08;
        static u8 GetBindResourceFlag(const char *name)
        {
            const static String kPerObj = "ScenePerObjectData";
            const static String kPerMat = "ScenePerMaterialData";
            const static String kPerPass = "ScenePerPassData";
            const static String kPerFrame = "ScenePerFrameData";
            String name_str(name);
            if (name_str == kPerObj)
                return kBindFlagPerObject;
            else if (name_str == kPerMat)
                return kBindFlagPerMaterial;
            else if (name_str == kPerPass)
                return kBindFlagPerPass;
            else if (name_str == kPerFrame)
                return kBindFlagPerFrame;
            else
            {
                AL_ASSERT(true);
            }
        }
        inline const static std::set<String> s_reversed_res_name{
                "SceneObjectBuffer",
                "_MatrixWorld",
                "SceneMaterialBuffer",
                "SceneStatetBuffer",
                "_MatrixV",
                "_MatrixP",
                "_MatrixVP",
                "_CameraPos",
                "_DirectionalLights",
                "_PointLights",
                "_SpotLights",
                "padding",
                "padding0",
                "padding1"};
        inline static u16 GetVariableSize(const ShaderBindResourceInfo &info) { return info._cbuf_member_offset & 0XFFFF; }
        inline static u16 GetVariableOffset(const ShaderBindResourceInfo &info) { return info._cbuf_member_offset >> 16; }
        ShaderBindResourceInfo() = default;
        ShaderBindResourceInfo(EBindResDescType res_type, u32 slot_or_offset, u8 bind_slot, const String &name)
            : _res_type(res_type), _bind_slot(bind_slot), _name(name)
        {
            if (res_type & EBindResDescType::kCBufferAttribute)
                _cbuf_member_offset = slot_or_offset;
            else
                _res_slot = slot_or_offset;
        }
        bool operator==(const ShaderBindResourceInfo &other) const
        {
            return _name == other._name;
        }
        EBindResDescType _res_type;
        union
        {
            u32 _res_slot;
            u32 _cbuf_member_offset;
        };
        u8 _bind_slot;
        String _name;
        void *_p_res = nullptr;
        ShaderBindResourceInfo *_p_root_cbuf;
        //1 for per obj,2for per mat,4 for per pass,8 for per frame
        u8 _bind_flag = 0u;
    };

    // Hash function for ShaderBindResourceInfo
    struct ShaderBindResourceInfoHash
    {
        std::size_t operator()(const ShaderBindResourceInfo &info) const
        {
            return std::hash<String>{}(info._name);
        }
    };

    // Equality function for ShaderBindResourceInfo
    struct ShaderBindResourceInfoEqual
    {
        bool operator()(const ShaderBindResourceInfo &lhs, const ShaderBindResourceInfo &rhs) const
        {
            return lhs._name == rhs._name;
        }
    };

    struct ShaderPropertyInfo
    {
        String _value_name;
        String _prop_name;
        ESerializablePropertyType _prop_type;
        Vector4f _prop_param;
    };

    struct ShaderCommand
    {
        inline static const String kName = "name";
        inline static const String kVSEntry = "Vert";
        inline static const String kPSEntry = "Pixel";
        inline static const String kCull = "Cull";
        inline static const String kQueue = "Queue";
        inline static const String kTopology = "Topology";
        inline static const String kBlend = "Blend";
        inline static const String KFill = "Fill";
        inline static const String kZTest = "ZTest";
        inline static const String kZWrite = "ZWrite";
        inline static const String kStencil = "Stencil";
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
            inline static const String kLEqual = "LEqual";
            inline static const String kEqual = "Equal";
            inline static const String kAlways = "Always";
            inline static const String kGreater = "Greater";
            inline static const String kNotEqual = "NotEqual";
            inline static const String kGEqual = "GEqual";
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
    };

    using ShaderVariantHash = u64;

    struct ShaderPass
    {
        struct PassVariant
        {
            u8 _vertex_input_num = 0u;
            i8 _per_mat_buf_bind_slot = -1;
            i8 _per_frame_buf_bind_slot = -1;
            i8 _per_pass_buf_bind_slot = -1;
            VertexInputLayout _pipeline_input_layout;
            std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos;
            std::set<String> _active_keywords;
        };
        //pass info
        u16 _index;
        String _name;
        String _tag;
        //shader info
        String _vert_entry, _pixel_entry;
        RasterizerState _pipeline_raster_state;
        DepthStencilState _pipeline_ds_state;
        ETopology _pipeline_topology;
        BlendState _pipeline_blend_state;
        u32 _render_queue;
        std::set<WString> _source_files;
        Vector<std::set<String>> _keywords;
        //reocrd per keyword's group_id and inner_group_id
        std::map<String, std::tuple<u8, u8>> _keywords_ids;
        List<ShaderPropertyInfo> _shader_prop_infos;
        Map<ShaderVariantHash, PassVariant> _variants;
    };

    enum class EShaderVariantState
    {
        kNone,
        kCompiling,
        kError,
        kReady
    };
    class AILU_API Shader : public Object
    {
        friend class Material;

    public:
        inline static Shader *s_p_defered_standart_lit = nullptr;
        inline static const u16 kRenderQueueOpaque = 2000;
        inline static const u16 kRenderQueueAlphaTest = 2500;
        inline static const u16 kRenderQueueTransparent = 3000;
        inline static const u16 kRenderQueueEnd = 4000;

    public:
        static Ref<Shader> Create(const WString &sys_path);
        static u64 ConstructHash(const u32 &shader_id, const u16 &pass_hash, ShaderVariantHash variant_hash = 0u);
        //inline static const BitDesc kShader = {0,46}; 46bit hash
        static void ExtractInfoFromHash(const u64 &shader_hash, u32 &shader_id, u16 &pass_hash, ShaderVariantHash &variant_hash);

        static void SetGlobalTexture(const String &name, Texture *texture);
        static void SetGlobalTexture(const String &name, RTHandle handle);
        static void SetGlobalMatrix(const String &name, Matrix4x4f *matrix);
        static void SetGlobalMatrixArray(const String &name, Matrix4x4f *matrix, u32 num);

        static void ConfigurePerFrameConstBuffer(IConstantBuffer *cbuf);
        static IConstantBuffer *GetPerFrameConstBuffer() { return s_p_per_frame_cbuffer; };
        //0 is normal, 4001 is compiling, 4000 is error,same with render queue id
        static u32 GetShaderState(Shader *shader, u16 pass_index, ShaderVariantHash variant_hash);

        Shader(const WString &sys_path);
        virtual ~Shader() = default;
        virtual void Bind(u16 pass_index, ShaderVariantHash variant_hash);
        //call this before compile
        virtual bool PreProcessShader();
        virtual bool Compile(u16 pass_id, ShaderVariantHash variant_hash);
        //compile all pass's variants
        virtual bool Compile();
        virtual void *GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash);
        const WString &GetMainSourceFile() { return _src_file_path; }

        const ShaderPass &GetPassInfo(u16 pass_index) const
        {
            AL_ASSERT(pass_index >= _passes.size());
            return _passes[pass_index];
        }
        const u32 PassCount() const { return static_cast<u32>(_passes.size()); }
        i16 FindPass(String pass_name);
        //根据任意字符序列构建变体hash，并返回合法的序列
        ShaderVariantHash ConstructVariantHash(u16 pass_index, const std::set<String> &kw_seq, std::set<String> &active_kw_seq) const;
        ShaderVariantHash ConstructVariantHash(u16 pass_index, const std::set<String> &kw_seq) const;
        const i8 &GetPerMatBufferBindSlot(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._per_mat_buf_bind_slot; }
        const i8 &GetPerFrameBufferBindSlot(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._per_frame_buf_bind_slot; }
        const i8 &GetPerPassBufferBindSlot(u16 pass_index, ShaderVariantHash variant_hash) const { return _passes[pass_index]._variants.at(variant_hash)._per_pass_buf_bind_slot; }
        const std::unordered_map<String, ShaderBindResourceInfo> &GetBindResInfo(u16 pass_index, ShaderVariantHash variant_hash) { return _passes[pass_index]._variants.at(variant_hash)._bind_res_infos; }

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

        Vector<class Material *> GetAllReferencedMaterials();
        void AddMaterialRef(class Material *mat);
        void RemoveMaterialRef(class Material *mat);
        std::tuple<String &, String &> GetShaderEntry(u16 pass_index = 0)
        {
            AL_ASSERT(pass_index >= _passes.size());
            return std::tie(_passes[pass_index]._vert_entry, _passes[pass_index]._pixel_entry);
        }
        EShaderVariantState GetVariantState(u16 pass_index, ShaderVariantHash hash)
        {
            AL_ASSERT(pass_index >= _variant_state.size());
            auto &info = _variant_state[pass_index];
            if (info.contains(hash))
                return info[hash];
            else
            {
                AL_ASSERT(true);
            }
        }

    protected:
        virtual bool RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash);

    protected:
        inline static u8 *_p_cbuffer = nullptr;
        inline static IConstantBuffer *s_p_per_frame_cbuffer = nullptr;
        WString _src_file_path;
        Vector<ShaderPass> _passes;
        std::set<Material *> _reference_mats;
        Vector<Map<ShaderVariantHash, EShaderVariantState>> _variant_state;
        std::atomic<bool> _is_pass_elements_init = false;

    private:
        void ParserShaderProperty(String &line, List<ShaderPropertyInfo> &props);
        //void ExtractValidShaderProperty(u16 pass_index,ShaderVariantHash variant_hash);
    private:
        inline static u16 s_global_shader_unique_id = 0u;
        inline static std::map<String, Texture *> s_global_textures_bind_info{};
        inline static std::map<String, std::tuple<Matrix4x4f *, u32>> s_global_matrix_bind_info{};
    };

    class ComputeShader : public Object
    {
        struct KernelElement
        {
			u16 _id;
			String _name;
            std::set<WString> _all_dep_file_pathes;
			std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos{};
        	std::unordered_map<String, ShaderBindResourceInfo> _temp_bind_res_infos{};
        };

    public:
        static Ref<ComputeShader> Create(const WString &sys_path);
        static Ref<ComputeShader> Get(const WString &asset_path);
        ComputeShader(const WString &sys_path);
        virtual ~ComputeShader() = default;
        virtual void Bind(CommandBuffer *cmd, u16 kernel,u16 thread_group_x, u16 thread_group_y, u16 thread_group_z);
        virtual void SetTexture(const String &name, Texture *texture);
        virtual void SetTexture(u8 bind_slot, Texture *texture);
        virtual void SetTexture(const String &name, RTHandle handle);
        virtual void SetTexture(const String &name, Texture *texture, ECubemapFace::ECubemapFace face, u16 mipmap);
        virtual void SetFloat(const String &name, f32 value);
        virtual void SetBool(const String &name, bool value);
        virtual void SetInt(const String &name, i32 value);
        virtual void SetVector(const String &name, Vector4f vector);
        virtual void SetBuffer(const String &name, IConstantBuffer *buf);
        const std::set<Texture *> &AllBindTexture() const { return _all_bind_textures; }
		u16 FindKernel(const String& kernel)
		{
			auto it = std::find_if(_kernels.begin(),_kernels.end(),[&](auto e)->bool{
				return e._name == kernel;
			});
			if (it != _kernels.end())
				return it->_id;
			else
				return (u16)-1;
		}
        bool IsDependencyFile(const WString& sys_path) const;
        bool Compile();
        std::atomic<bool> _is_compiling = false;
        static auto begin() {return s_cs_library.begin();}
        static auto end() {return s_cs_library.end();}
    protected:
        virtual bool RHICompileImpl(u16 kernel_index);

    private:
        void Preprocess();

    protected:
        inline static std::unordered_map<WString, Ref<ComputeShader>> s_cs_library{};
		Vector<KernelElement> _kernels;
        WString _src_file_path;
        /*维护一个纹理集合，用于标记temp rt的围栏值，常规用于图形管线的rt在set / clear时会进行标记，
		* 计算管线不用这两个接口，所以在commandbuffer dispatch时调用该接口标记
		*/
        std::set<Texture *> _all_bind_textures;
        //bind_slot:cubemapface,mipmap
        std::unordered_map<u16, std::tuple<ECubemapFace::ECubemapFace, u16>> _texture_addi_bind_info{};
        bool _is_valid;
        Scope<IConstantBuffer> _p_cbuffer;
    };
}// namespace Ailu


#endif// !SHADER_H__
