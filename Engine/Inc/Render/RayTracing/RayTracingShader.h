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
    class RayTracingShader : public Object
    {
        GENERATED_BODY()
    public:
        inline static const u32 kCBufferSize = 1024u;
        static Ref<RayTracingShader> Create(const WString &sys_path,const String& name);
        FORCEINLINE static void SetGlobalBuffer(const String &name, ConstantBuffer *buf)
        {
            s_global_cbuffer_bind_info[name] = buf;
        };
        FORCEINLINE static void SetGlobalBuffer(const String &name, GPUBuffer *buf)
        {
            s_global_buffer_bind_info[name] = buf;
        };
        FORCEINLINE static void SetGlobalTexture(const String &name, Texture *texture)
        {
            s_global_textures_bind_info[name] = texture;
        };
        static void SetGlobalTexture(const String &name, RTHandle texture);
        FORCEINLINE static void SetGlobalFloat(const String &name, f32 value)
        {
            s_global_floats[name] = value;
        };
        FORCEINLINE static void SetGlobalInt(const String &name, i32 value)
        {
            s_global_ints[name] = value;
        };
    public:
        RayTracingShader() = default;
        RayTracingShader(const WString &sys_path);
        ~RayTracingShader() = default;
        void SetScene(RayTracingScene* scene);
        void SetTexture(const String &name, Texture *texture);
        void SetTexture(const String &name, RTHandle handle);
        void SetBuffer(const String &name, ConstantBuffer *buf);
        void SetBuffer(const String &name, GPUBuffer *buf);
        void SetFloat(const String &name, f32 value);
        void SetFloats(const String &name, Vector<f32> values);
        void SetBool(const String &name, bool value);
        void SetInt(const String &name, i32 value);
        void SetInts(const String &name, Vector<i32> values);
        void SetVector(const String &name, Vector4f vector);
        void SetVectorArray(const String &name, const Vector<Vector4f> &vectors);
        void SetVectorArray(const String &name, Vector4f *vectors, u16 num);
        void SetMatrix(const String &name, Matrix4x4f mat);
        void SetMatrixArray(const String &name, Vector<Matrix4x4f> matrix_arr);
        bool IsDependencyFile(const WString &sys_path) const;
        void PushState(RayTracingScene *scene = nullptr);
        virtual void DispatchRays(RHICommandBuffer* cmd,uint w, uint h, uint depth) {};
        bool Compile(bool is_load_cache = true);
        const std::unordered_map<String, ShaderBindResourceInfo> &GetBindResInfo() const { return _bind_res_infos; }
    public:
        std::atomic<bool> _is_compiling = false;
    protected:
        virtual bool RHICompileImpl(ShaderVariantHash variant_hash, bool is_load_cache);
    protected:
        struct RayTracingBindParams
        {
            ECubemapFace::ECubemapFace _face = ECubemapFace::kUnknown;
            u16 _mipmap = 0u;
            u16 _slice = 0u;
            u32 _sub_res = UINT32_MAX;
            u16 _view_index = static_cast<u16>(-1);
            bool _is_internal_cbuf = false;
        };
        struct BindState
        {
            u16 _max_bind_slot = 0u;
            Array<GpuResource*, 32> _bind_res{};
            Array<u16, 32> _bind_res_priority{};
            Array<RayTracingBindParams, 32> _bind_params{};
        };
        inline static Map<String, ConstantBuffer *> s_global_cbuffer_bind_info{};
        inline static Map<String, GPUBuffer *> s_global_buffer_bind_info{};
        inline static Map<String, Texture *> s_global_textures_bind_info{};
        inline static Map<String, f32> s_global_floats{};
        inline static Map<String, i32> s_global_ints{};
        RayTracingScene* _scene = nullptr;
        WString _src_file_path;
        std::set<WString> _all_dep_file_pathes;
        std::unordered_map<String, ShaderBindResourceInfo> _bind_res_infos;
        std::unordered_map<u16, RayTracingBindParams> _bind_params{};
        u8 _cbuf_data[kCBufferSize]{};
        u8 _cache_cbuf_data[kCBufferSize]{};
        String _internal_cbuf_name = "";
        Queue<BindState> _bind_state;
        std::mutex _state_mutex;
        bool _is_valid = false;
    };
}
#endif// !__RAY_TRACING_SHADER_H__