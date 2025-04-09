#ifndef __GPU_COMMAND__H_
#define __GPU_COMMAND__H_

#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "GpuResource.h"
#include "Framework/Common/Log.h"
#include "RenderConstants.h"
#include <functional>
#include <mutex>

namespace Ailu
{
    class RenderTexture;
    class VertexBuffer;
    class IndexBuffer;
    class GraphicsPipelineStateObject;
    class ComputeShader;
    class Material;
    class ConstantBuffer;
    enum class EGpuCommandType
    {
        kSetTarget,
        kClearTarget,
        kDraw,
        kDispatch,
        kResourceUpload,
        kTransResourceState,
        kAllocConstBuffer,
        kCommandProfiler,
        kReadBack,
        kPresent,
        kCustom
    };
#define CMD_CLASS_TYPE(type)                                                        \
    static EGpuCommandType GetStaticType() { return EGpuCommandType::type; }        \
    virtual EGpuCommandType GetCmdType() const override { return GetStaticType(); } \
    virtual const char *GetName() const override { return #type; }
    struct GfxCommand
    {
        virtual ~GfxCommand() = default;
        virtual EGpuCommandType GetCmdType() const = 0;
        virtual const char *GetName() const = 0;
    };

    struct CommandSetTarget : public GfxCommand
    {
        CMD_CLASS_TYPE(kSetTarget)
        Array<RenderTexture *, RenderConstants::kMaxMRTNum> _color_target;
        RenderTexture *_depth_target;
        u16 _color_target_num;
        Array<u16, RenderConstants::kMaxMRTNum> _color_indices;
        Array<Rect, RenderConstants::kMaxMRTNum> _viewports;
        u16 _depth_index;
    };
    enum EClearFlag
    {
        kColor = BIT(0),
        kDepth = BIT(1),
        kStencil = BIT(2),
        kAll = kColor | kDepth | kStencil
    };
    struct CommandClearTarget : public GfxCommand
    {
        CMD_CLASS_TYPE(kClearTarget);
        EClearFlag _flag;
        u16 _color_target_num;
        Array<Color, RenderConstants::kMaxMRTNum> _colors;
        f32 _depth;
        u8 _stencil;
    };
    struct CommandDraw : public GfxCommand
    {
        CMD_CLASS_TYPE(kDraw);
        VertexBuffer * _vb;
        IndexBuffer * _ib;
        Material *_mat;
        u16 _pass_index;
        ConstantBuffer *_per_obj_cb;
        //大于1则启用实例绘制
        u32 _instance_count;
        u16 _sub_mesh;
    };
    struct CommandDispatch : public GfxCommand
    {
        CMD_CLASS_TYPE(kDispatch);
        ComputeShader *_cs;
        u16 _kernel;
        u16 _group_num_x;
        u16 _group_num_y;
        u16 _group_num_z;
    };
    struct CommandGpuResourceUpload : public GfxCommand
    {
        CMD_CLASS_TYPE(kResourceUpload);
        GpuResource *_res;
        UploadParams* _params;
        ~CommandGpuResourceUpload()
        {
            DESTORY_PTR(_params);
        }
    };
    struct CommandTranslateState : public GfxCommand
    {
        CMD_CLASS_TYPE(kTransResourceState);
        GpuResource *_res;
        EResourceState _new_state;
        u32 _sub_res;
        CommandTranslateState() : _res(nullptr), _new_state(EResourceState::kCommon), _sub_res(UINT32_MAX) {}
        CommandTranslateState(GpuResource* res, EResourceState new_state, u32 sub_res = UINT32_MAX) :  _res(res), _new_state(new_state), _sub_res(sub_res) {}
    };
    struct CommandCustom : public GfxCommand
    {
        CMD_CLASS_TYPE(kCustom);
        std::function<void()> _func;
    };
    struct CommandAllocConstBuffer : public GfxCommand
    {
        CMD_CLASS_TYPE(kAllocConstBuffer);
        char _name[64];
        u8* _data;
        u32 _size;
        i16 _kernel;
        ~CommandAllocConstBuffer()
        {
            DESTORY_PTRARR(_data);
        }
    };
    //同时也会创建一个cpu profile
    struct CommandProfiler : public GfxCommand
    {
        CMD_CLASS_TYPE(kCommandProfiler);
        String _name;
        bool _is_start;
        u32 _cpu_index;//用于索引具体的profiler，处理begin事件时赋值，遇到end事件时，找最近的begin事件
        u32 _gpu_index;//用于索引具体的profiler，处理begin事件时赋值，遇到end事件时，找最近的begin事件
    };
    struct CommandPresent : public GfxCommand
    {
        CMD_CLASS_TYPE(kPresent);
    };

    struct CommandReadBack : public GfxCommand
    {
        CMD_CLASS_TYPE(kReadBack);
        GpuResource *_res;
        bool _is_buffer;
        u8* _dst;
        u32 _size;
    };

    class CommandPool
    {
    public:
        CommandPool() = default;
        ~CommandPool() = default;
        static CommandPool &Get();
        static void Init();
        static void Shutdown();
        template<typename T>
        T *Alloc()
        {
            ++_cmd_count;
            //std::lock_guard<std::mutex> lock(_mutex);
            if constexpr (std::is_same_v<T, CommandSetTarget>)
            {
                return new CommandSetTarget();
            }
            else if constexpr (std::is_same_v<T, CommandClearTarget>)
            {
                return new CommandClearTarget();
            }
            else if constexpr (std::is_same_v<T, CommandDraw>)
            {
                return new CommandDraw();
            }
            else if constexpr (std::is_same_v<T, CommandDispatch>)
            {
                return new CommandDispatch();
            }
            else if constexpr (std::is_same_v<T, CommandGpuResourceUpload>)
            {
                return new CommandGpuResourceUpload();
            }
            else if constexpr (std::is_same_v<T, CommandTranslateState>)
            {
                return new CommandTranslateState();
            }
            else if constexpr (std::is_same_v<T, CommandCustom>)
            {
                return new CommandCustom();
            }
            else if constexpr (std::is_same_v<T, CommandAllocConstBuffer>)
            {
                return new CommandAllocConstBuffer();
            }
            else if constexpr (std::is_same_v<T, CommandProfiler>)
            {
                return new CommandProfiler();
            }
            else if constexpr (std::is_same_v<T, CommandPresent>)
            {
                return new CommandPresent();
            }
            else
            {
                return nullptr;
            }
        }
        template<typename T>
        void Free(T *cmd)
        {
            //std::lock_guard<std::mutex> lock(_mutex);
            DESTORY_PTR(cmd);
            --_cmd_count;
        }
        void EndFrame()
        {
            // if (_cmd_count > 0u)
            //     LOG_INFO("CommandPool::EndFrame: {} cmd not been release!",_cmd_count.load());
            // _cmd_count = 0u;
        }

    private:
        //Array<GfxCommand *, 100> _cmd_pool;
        std::mutex _mutex;
        std::atomic<u64> _cmd_count = 0u;
    };
}// namespace Ailu

#endif// __GPU_COMMAND__H_