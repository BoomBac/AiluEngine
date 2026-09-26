#pragma once
#ifndef __RAY_TRACING_SHADER_H__
#define __RAY_TRACING_SHADER_H__
#include "../Shader.h"
#include "generated/RayTracingShader.gen.h"
namespace Ailu::Render
{
    class Camera;
    class RayTracingScene;
    class RHICommandBuffer;
    ACLASS()
    class AILU_API RayTracingShader : public GpuResource
    {
        GENERATED_BODY()
    public:
        inline static const u32 kCBufferSize = 1024u;
        static Ref<RayTracingShader> Create(const WString &sys_path,const String& name);
        AL_FORCE_INLINE static void SetGlobalBuffer(const String &name, ConstantBuffer *buf)
        {
            s_global_res_registry.SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buf);
        };
        AL_FORCE_INLINE static void SetGlobalBuffer(const String &name, GPUBuffer *buf)
        {
            s_global_res_registry.SetBuffer(ShaderPropertyRegistry::Get().Intern(name), buf);
        };
        AL_FORCE_INLINE static void SetGlobalTexture(const String &name, Texture *texture)
        {
            s_global_res_registry.SetTexture(ShaderPropertyRegistry::Get().Intern(name), texture);
        };
        static void SetGlobalTexture(const String &name, RTHandle texture);
        AL_FORCE_INLINE static void SetGlobalFloat(const String &name, f32 value)
        {
            s_global_floats[ShaderPropertyRegistry::Get().Intern(name)] = value;
        };
        AL_FORCE_INLINE static void SetGlobalInt(const String &name, i32 value)
        {
            s_global_ints[ShaderPropertyRegistry::Get().Intern(name)] = value;
        };
    public:
        RayTracingShader() = default;
        RayTracingShader(const WString &sys_path);
        ~RayTracingShader();
        static Vector<RayTracingShader *> GetLiveInstances();
        void SetScene(RayTracingScene* scene);
        void SetTexture(const String &name, Texture *texture);
        void SetTexture(ShaderPropertyId property_id, Texture *texture);
        void SetTexture(const String &name, RTHandle handle);
        void SetBuffer(const String &name, ConstantBuffer *buf);
        void SetBuffer(ShaderPropertyId property_id, ConstantBuffer *buf);
        void SetBuffer(const String &name, GPUBuffer *buf);
        void SetBuffer(ShaderPropertyId property_id, GPUBuffer *buf);
        void SetFloat(const String &name, f32 value);
        void SetFloat(ShaderPropertyId property_id, f32 value);
        void SetFloats(const String &name, Vector<f32> values);
        void SetFloats(ShaderPropertyId property_id, const Vector<f32> &values);
        void SetBool(const String &name, bool value);
        void SetBool(ShaderPropertyId property_id, bool value);
        void SetInt(const String &name, i32 value);
        void SetInt(ShaderPropertyId property_id, i32 value);
        void SetInts(const String &name, Vector<i32> values);
        void SetInts(ShaderPropertyId property_id, const Vector<i32> &values);
        void SetVector(const String &name, Vector4f vector);
        void SetVector(ShaderPropertyId property_id, Vector4f vector);
        void SetVectorArray(const String &name, const Vector<Vector4f> &vectors);
        void SetVectorArray(ShaderPropertyId property_id, const Vector<Vector4f> &vectors);
        void SetVectorArray(const String &name, Vector4f *vectors, u16 num);
        void SetMatrix(const String &name, Matrix4x4f mat);
        void SetMatrix(ShaderPropertyId property_id, Matrix4x4f mat);
        void SetMatrixArray(const String &name, Vector<Matrix4x4f> matrix_arr);
        void SetMatrixArray(ShaderPropertyId property_id, const Vector<Matrix4x4f> &matrix_arr);
        bool IsDependencyFile(const WString &sys_path) const;
        void CaptureDispatchState(RayTracingScene *scene, DispatchBindingSnapshot &snapshot,
                                  const HashMap<ShaderPropertyId, CommandResourceBinding> *command_resources = nullptr);
        virtual void DispatchRays(RHICommandBuffer *cmd, u32 width, u32 height, u32 depth,
                                  const DispatchBindingSnapshot &snapshot) {};
        bool Compile(EShaderCompilePolicy policy = EShaderCompilePolicy::kUseArtifact);
        void EnableKeyword(const String &keyword);
        void DisableKeyword(const String &keyword);
        ShaderVariantHash ActiveVariant() const { return _active_variant.load(); }
        const std::unordered_map<String, ShaderBindResourceInfo> &GetBindResInfo() const
        {
            const auto it = _variants.find(_active_variant.load());
            return it != _variants.end() && it->second._is_ready ? it->second._bind_res_infos : _bind_res_infos;
        }
    public:
        std::atomic<bool> _is_compiling = false;
    protected:
        struct Variant
        {
            std::set<String> _active_keywords;
            ShaderReflectionResourceMap _bind_res_infos;
            Ref<const ShaderBindingLayout> _binding_layout;
            bool _is_ready = false;
        };
        bool PreprocessVariants();
        void RegisterDependencyFile(const WString &path);
        virtual bool RHICompileImpl(ShaderVariantHash variant_hash, EShaderCompilePolicy policy);
    protected:
        inline static ShaderGlobalResourceRegistry s_global_res_registry;
        inline static HashMap<ShaderPropertyId, f32> s_global_floats{};
        inline static HashMap<ShaderPropertyId, i32> s_global_ints{};
        inline static std::mutex s_live_instances_mutex{};
        inline static Vector<RayTracingShader *> s_live_instances{};
        RayTracingScene* _scene = nullptr;
        WString _src_file_path;
        std::set<WString> _all_dep_file_pathes;
        ShaderVariantMgr _variant_mgr;
        HashMap<ShaderVariantHash, Variant> _variants;
        std::set<String> _active_keywords;
        std::atomic<ShaderVariantHash> _active_variant{0u};
        std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos;
        Ref<const ShaderBindingLayout> _binding_layout;
        u32 _binding_layout_version = 1u;
        HashMap<ShaderPropertyId, ShaderResourceBinding> _resource_bindings;
        u8 _cbuf_data[kCBufferSize]{};
        u8 _cache_cbuf_data[kCBufferSize]{};
        String _internal_cbuf_name = "";
        bool _is_valid = false;
    };
}
#endif// !__RAY_TRACING_SHADER_H__
