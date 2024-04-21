#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include <string>
#include <set>
#include <fstream>
#include <map>
#include <unordered_map>
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/Path.h"
#include "Framework/Common/Reflect.h"
#include "Texture.h"
#include "Buffer.h"

#include "PipelineState.h"


namespace Ailu
{
	enum class EShaderType : u8
	{
		kVertex,kPixel
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
		inline const static std::set<String> s_reversed_res_name
		{
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
			"padding0"
		};
		inline static u16 GetVariableSize(const ShaderBindResourceInfo& info){ return info._cbuf_member_offset & 0XFFFF;}
		inline static u16 GetVariableOffset(const ShaderBindResourceInfo& info){ return info._cbuf_member_offset >> 16;}
		EBindResDescType _res_type;
		union
		{
			u32 _res_slot;
			u32 _cbuf_member_offset;
		};
		u8 _bind_slot;
		String _name;
		void* _p_res = nullptr;
		ShaderBindResourceInfo() = default;
		ShaderBindResourceInfo(EBindResDescType res_type, u32 slot_or_offset, u8 bind_slot, const String& name)
			: _res_type(res_type), _bind_slot(bind_slot), _name(name) 
		{
			if (res_type & EBindResDescType::kCBufferAttribute) 
				_cbuf_member_offset = slot_or_offset;
			else _res_slot = slot_or_offset;
		}
		bool operator==(const ShaderBindResourceInfo& other) const
		{
			return _name == other._name;
		}
	};

	// Hash function for ShaderBindResourceInfo
	struct ShaderBindResourceInfoHash
	{
		std::size_t operator()(const ShaderBindResourceInfo& info) const
		{
			return std::hash<String>{}(info._name);
		}
	};

	// Equality function for ShaderBindResourceInfo
	struct ShaderBindResourceInfoEqual
	{
		bool operator()(const ShaderBindResourceInfo& lhs, const ShaderBindResourceInfo& rhs) const
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
		static struct ZTest
		{
			inline static const String kOff = "Off";
			inline static const String kLEqual = "LEqual";
			inline static const String kEqual = "Equal";
			inline static const String kAlways = "Always";
		} kZTestValue;
		static struct ZWrite
		{
			inline static const String kOff = "Off";
			inline static const String kOn = "On";
		} kZWriteValue;
	};

	class Shader
	{
		DECLARE_PRIVATE_PROPERTY(name, Name, String)
		DECLARE_PRIVATE_PROPERTY(id, ID, u16)
		friend class Material;
	public:
		static Ref<Shader> Create(const String& file_name,String vert_entry = "", String pixel_entry = "");
		static void SetGlobalTexture(const String& name, Texture* texture);
		static void SetGlobalMatrix(const String& name, Matrix4x4f* matrix);
		static void SetGlobalMatrixArray(const String& name, Matrix4x4f* matrix, u32 num);
		static void ConfigurePerFrameConstBuffer(ConstantBuffer* cbuf);
		static ConstantBuffer* GetPerFrameConstBuffer() { return	s_p_per_frame_cbuffer; };

		Shader(const String& sys_path);
		virtual ~Shader() = default;
		virtual void Bind(u32 index);
		virtual bool Compile();
		virtual bool PreProcessShader();
		virtual void* GetByteCode(EShaderType type);

		const i8& GetPerMatBufferBindSlot() const {return _per_mat_buf_bind_slot;}
		const i8& GetPerFrameBufferBindSlot() const {return _per_frame_buf_bind_slot;}
		const i8& GetPrePassBufferBindSlot() const  {return _per_pass_buf_bind_slot;}
		const bool IsCompileError() const {return _is_compile_error;}

		const VertexInputLayout& PipelineInputLayout() const { return _pipeline_input_layout; };
		const RasterizerState& PipelineRasterizerState() const { return _pipeline_raster_state; };
		const DepthStencilState& PipelineDepthStencilState() const { return _pipeline_ds_state; };
		const ETopology& PipelineTopology() const { return _pipeline_topology; };
		const BlendState& PipelineBlendState() const { return _pipeline_blend_state; };

		Vector4f GetVectorValue(const String& name);
		float GetFloatValue(const String& name);


		const String& GetSrcPath() {return _src_file_path;}
		const std::set<String>& GetSourceFiles() {return _source_files;}
		const std::map<String, Vector<String>> GetKeywordGroups() {return _keywords;};
		Vector<class Material*> GetAllReferencedMaterials();
		void AddMaterialRef(class Material* mat);
		void RemoveMaterialRef(class Material* mat);
		const std::unordered_map<String, ShaderBindResourceInfo>& GetBindResInfo() {return	_bind_res_infos;}
		const List<ShaderPropertyInfo>& GetShaderPropertyInfos() {return _shader_prop_infos;}
	protected:
		virtual bool RHICompileImpl();
	protected:
		inline static u8* _p_cbuffer = nullptr;
		inline static u16 _s_global_shader_id = 0u;
		inline static ConstantBuffer* s_p_per_frame_cbuffer = nullptr;
		String _vert_entry, _pixel_entry;
		u8 _vertex_input_num = 0u;
		String _src_file_path;
		i8 _per_mat_buf_bind_slot = -1;
		i8 _per_frame_buf_bind_slot = -1;
		i8 _per_pass_buf_bind_slot = -1;
		bool _is_compile_error;
		VertexInputLayout _pipeline_input_layout;
		RasterizerState _pipeline_raster_state;
		DepthStencilState _pipeline_ds_state;
		ETopology _pipeline_topology;
		BlendState _pipeline_blend_state;
		std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos{};
		std::map<String, Vector<String>> _keywords;
		std::map<String, std::tuple<u8, u8>> _keywords_ids;
		std::set<uint64_t> _shader_variant;
		List<ShaderPropertyInfo> _shader_prop_infos;
		std::set<Material*> _reference_mats;
		std::set<String> _source_files;
	private:
		void ParserShaderProperty(String& line, List<ShaderPropertyInfo>& props);
		void ExtractValidShaderProperty();
	private:
		inline static std::map<String, Texture*> s_global_textures_bind_info{};
		inline static std::map<String, std::tuple<Matrix4x4f*,u32>> s_global_matrix_bind_info{};
	};

	class ShaderLibrary
	{
#define VAILD_SHADER_ID(id) id != ShaderLibrary::s_error_id
		friend class Shader;
	public:
		inline static const struct
		{
			inline static const String kDepthOnly = "Shaders/depth_only.hlsl__";
			inline static const String kDeferredStandardLit = "Shaders/defered_standard_lit.hlsl__";
		} kInternalShaderStringID;
		static Ref<Shader> Get(String string_id)
		{
			u32 shader_id = NameToId(string_id);
			if (VAILD_SHADER_ID(shader_id)) return s_shader_library.find(shader_id)->second;
			else
			{
				LOG_ERROR("Shader: {} dont's exist in ShaderLibrary!", string_id);
				return nullptr;
			}
		}
		static Ref<Shader> Get(u32 id)
		{
			if (VAILD_SHADER_ID(id)) 
				return s_shader_library.find(id)->second;
			else
			{
				LOG_ERROR("Shader: {} dont's exist in ShaderLibrary!", id);
				return nullptr;
			}
		}

		static bool Exist(const String& name)
		{
			return NameToId(name) != s_error_id;
		}
		static String GetShaderPath(u32 shader_id)
		{
			auto it = s_shader_path.find(shader_id);
			return it == s_shader_path.end() ? "" : it->second;
		}
		static Ref<Shader> Add(const String& sys_path, const String& name)
		{
			std::ofstream out(sys_path);
			static String shader_template{ R"(//info bein
//name: )" + name + R"(
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Properties
//{
//	color("Color",Color) = (1,1,1,1)
//}
//info end

#include "common.hlsl"

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
};

CBufBegin
	float4 color;
CBufEnd

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(color) ; 
})"};
			out << shader_template;
			out.close();
			return Load(sys_path);
		}
		//string_id: path_vs_ps
		static void Add(const String& string_id, Ref<Shader> shader)
		{
			s_shader_name.insert(std::make_pair(string_id, shader->ID()));
			s_shader_library.insert(std::make_pair(shader->ID(), shader));
			s_shaders.emplace_back(shader);
		}

		static Ref<Shader> Load(const String& path,const String vs_entry = "", const String ps_entry = "")
		{
			auto sys_path = PathUtils::IsSystemPath(path) ? path : PathUtils::GetResSysPath(path);
			String string_id = path + "_" + vs_entry + "_" + ps_entry;
			if (Exist(string_id)) 
				return s_shader_library.find(NameToId(string_id))->second;
			else
			{
				auto shader = Shader::Create(sys_path, vs_entry, ps_entry);
				s_shader_path.insert(std::make_pair(shader->ID(), path));
				Add(string_id, shader);
				return shader;
			}
		}

		inline static u32 NameToId(const String& name)
		{
			auto it = s_shader_name.find(name);
			if (it != s_shader_name.end()) return it->second;
			else return s_error_id;
		}
		static auto Begin()
		{
			return s_shaders.begin();
		}
		static auto End()
		{
			return s_shaders.end();
		}
	private:
		inline static u32 s_shader_id = 0u;
		inline static u32 s_error_id = 9999u;
		inline static std::unordered_map<u32, Ref<Shader>> s_shader_library;
		inline static Vector<Ref<Shader>> s_shaders; //just for imgui iterator
		inline static std::unordered_map<String, u32> s_shader_name;
		inline static std::unordered_map<u32, String> s_shader_path;
	};

	class ComputeShader
	{
		DECLARE_PRIVATE_PROPERTY(name, Name, String)
		DECLARE_PRIVATE_PROPERTY(id, ID, u16)
		DECLARE_PROTECTED_PROPERTY(src_file_path,Path,String)
	public:
		static Ref<ComputeShader> Create(const String& file_name);
		static Ref<ComputeShader> Get(const String& name);
		ComputeShader(const String& sys_path);
		virtual ~ComputeShader() = default;
		virtual void Bind(CommandBuffer* cmd,u16 thread_group_x, u16 thread_group_y, u16 thread_group_z);
		virtual void SetTexture(const String& name, Texture* texture);
		virtual void SetTexture(u8 bind_slot, Texture* texture);

		bool Compile();
	protected:
		virtual bool RHICompileImpl();
	protected:
		inline static std::unordered_map<String,Ref<ComputeShader>> s_cs_library{};
		std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos{};
		std::unordered_map<String, ShaderBindResourceInfo> _temp_bind_res_infos{};
		bool _is_valid;
	private:
		inline static u32 s_global_cs_id = 0u;
	};
}


#endif // !SHADER_H__

