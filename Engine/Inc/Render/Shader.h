#pragma once
#ifndef __SHADER_H__
#define __SHADER_H__
#include <string>
#include <set>
#include <fstream>
#include <map>
#include <unordered_map>
#include "Objects/Object.h"
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

	struct ShaderPass
	{
		//pass info
		u16 _index;
		String _name;
		String _tag;
		//shader info
		String _vert_entry, _pixel_entry;
		std::set<WString> _source_files;
		u8 _vertex_input_num = 0u;
		i8 _per_mat_buf_bind_slot = -1;
		i8 _per_frame_buf_bind_slot = -1;
		i8 _per_pass_buf_bind_slot = -1;
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
	};

	class Shader : public Object
	{
		friend class Material;
	public:
		inline static Shader* s_p_defered_standart_lit = nullptr;
	public:
		static Ref<Shader> Create(const WString& sys_path);
		static u32 ConstructHash(const u32& shader_id, const u16& pass_hash, u16 variant_hash = 0u);
		static void ExtractInfoFromHash(const u32& shader_hash, u32& shader_id, u16& pass_hash, u16& variant_hash);

		static void SetGlobalTexture(const String& name, Texture* texture);
		static void SetGlobalMatrix(const String& name, Matrix4x4f* matrix);
		static void SetGlobalMatrixArray(const String& name, Matrix4x4f* matrix, u32 num);

		static void ConfigurePerFrameConstBuffer(ConstantBuffer* cbuf);
		static ConstantBuffer* GetPerFrameConstBuffer() { return	s_p_per_frame_cbuffer; };

		Shader(const WString& sys_path);
		virtual ~Shader() = default;
		virtual void Bind(u16 pass_index, u16 variant_id);
		virtual bool Compile();
		virtual bool PreProcessShader();
		virtual void* GetByteCode(EShaderType type, u16 pass_index, u16 variant_id);
		const bool IsCompileError() const {return _is_compile_error;}
		const WString& GetMainSourceFile() { return _src_file_path; }

		const ShaderPass& GetPassInfo(u16 pass_index) const
		{
			AL_ASSERT(pass_index >= _passes.size());
			return _passes[pass_index];
		}
		const u32 PassCount() const { return static_cast<u32>(_passes.size()); }

		const i8& GetPerMatBufferBindSlot(u16 pass_index = 0) const {return _passes[pass_index]._per_mat_buf_bind_slot;}
		const i8& GetPerFrameBufferBindSlot(u16 pass_index = 0) const {return _passes[pass_index]._per_frame_buf_bind_slot;}
		const i8& GetPrePassBufferBindSlot(u16 pass_index = 0) const  {return _passes[pass_index]._per_pass_buf_bind_slot;}

		const VertexInputLayout& PipelineInputLayout(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_input_layout; };
		const RasterizerState& PipelineRasterizerState(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_raster_state; };
		const DepthStencilState& PipelineDepthStencilState(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_ds_state; };
		const ETopology& PipelineTopology(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_topology; };
		const BlendState& PipelineBlendState(u16 pass_index = 0) const { return _passes[pass_index]._pipeline_blend_state; };
		//const std::set<WString>& GetSourceFiles(u16 pass_index) {return _source_files;}
		const std::map<String, Vector<String>> GetKeywordGroups() {return _passshared_keywords;};
		const std::unordered_map<String, ShaderBindResourceInfo>& GetBindResInfo(u16 pass_index) {return _passes[pass_index]._bind_res_infos;}
		const List<ShaderPropertyInfo>& GetShaderPropertyInfos(u16 pass_index) {return _passes[pass_index]._shader_prop_infos;}

		Vector<class Material*> GetAllReferencedMaterials();
		void AddMaterialRef(class Material* mat);
		void RemoveMaterialRef(class Material* mat);
		std::tuple<String&, String&> GetShaderEntry(u16 pass_index = 0)
		{
			AL_ASSERT(pass_index >= _passes.size());
			return std::tie(_passes[pass_index]._vert_entry, _passes[pass_index]._pixel_entry);
		}
	protected:
		virtual bool RHICompileImpl();
	protected:
		inline static u8* _p_cbuffer = nullptr;
		inline static ConstantBuffer* s_p_per_frame_cbuffer = nullptr;
		bool _is_compile_error;
		std::atomic<bool> _is_compiling = false;
		WString _src_file_path;
		Vector<ShaderPass> _passes;
		std::map<String, Vector<String>> _passshared_keywords;
		std::set<Material*> _reference_mats;
	private:
		void ParserShaderProperty(String& line, List<ShaderPropertyInfo>& props);
		void ExtractValidShaderProperty(u16 pass_index);
	private:
		inline static std::map<String, Texture*> s_global_textures_bind_info{};
		inline static std::map<String, std::tuple<Matrix4x4f*,u32>> s_global_matrix_bind_info{};
	};

	class ComputeShader : public Object
	{
	public:
		static Ref<ComputeShader> Create(const WString& sys_path);
		static Ref<ComputeShader> Get(const WString& asset_path);
		ComputeShader(const WString& sys_path);
		virtual ~ComputeShader() = default;
		virtual void Bind(CommandBuffer* cmd,u16 thread_group_x, u16 thread_group_y, u16 thread_group_z);
		virtual void SetTexture(const String& name, Texture* texture);
		virtual void SetTexture(u8 bind_slot, Texture* texture);
		virtual void SetTexture(const String& name, Texture* texture, ECubemapFace::ECubemapFace face, u16 mipmap);
		virtual void SetFloat(const String& name, f32 value);
		virtual void SetBool(const String& name, bool value);
		virtual void SetInt(const String& name, i32 value);
		virtual void SetVector(const String& name, Vector4f vector);
		const String& KernelName(u16 index = 0) 
		{
			AL_ASSERT(index > _kernels.size());
			return _kernels[index];
		};
		bool Compile();
	protected:
		virtual bool RHICompileImpl();
	protected:
		inline static std::unordered_map<WString,Ref<ComputeShader>> s_cs_library{};
		Vector<String> _kernels;
		WString _src_file_path;
		std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos{};
		std::unordered_map<String, ShaderBindResourceInfo> _temp_bind_res_infos{};
		//bind_slot:cubemapface,mipmap
		std::unordered_map<u16,std::tuple<ECubemapFace::ECubemapFace, u16>> _texture_addi_bind_info{};
		bool _is_valid;
		Scope<ConstantBuffer> _p_cbuffer;
	};
}


#endif // !SHADER_H__

