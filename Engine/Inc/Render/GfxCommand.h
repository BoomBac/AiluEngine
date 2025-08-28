#ifndef __GPU_COMMAND__H_
#define __GPU_COMMAND__H_

#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Container.hpp"
#include "Framework/Common/Log.h"
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "GpuResource.h"
#include "RenderConstants.h"
#include <functional>
#include <mutex>

namespace Ailu::Render
{
    class RenderTexture;
    class VertexBuffer;
    class IndexBuffer;
    class GraphicsPipelineStateObject;
    class ComputeShader;
    class Material;
    class ConstantBuffer;
    class GPUBuffer;
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
        kCopyCounter,
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
        virtual void Reset() = 0;
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
        void Reset() final 
        {
            _depth_target= nullptr;
            _color_target_num = 0u;
        }
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
        void Reset() final {}
    };
    struct CommandDraw : public GfxCommand
    {
        CMD_CLASS_TYPE(kDraw);
        VertexBuffer *_vb;
        IndexBuffer *_ib;
        Material *_mat;
        u16 _pass_index;
        ConstantBuffer *_per_obj_cb;
        u32 _instance_count;
        u16 _sub_mesh;
        u32 _index_start;
        GPUBuffer* _arg_buffer;
        u32 _arg_offset;
        void Reset() final 
        {
            _arg_buffer = nullptr;
            _arg_offset = 0u;
        }
    };
    struct CommandDispatch : public GfxCommand
    {
        CMD_CLASS_TYPE(kDispatch);
        ComputeShader *_cs;
        u16 _kernel;
        u16 _group_num_x;
        u16 _group_num_y;
        u16 _group_num_z;
        GPUBuffer* _arg_buffer;
        u16 _arg_offset;
        void Reset() final 
        {
            _cs = nullptr;
            _arg_buffer = nullptr;
        }
    };
    struct CommandGpuResourceUpload : public GfxCommand
    {
        CMD_CLASS_TYPE(kResourceUpload);
        GpuResource *_res;
        UploadParams *_params;
        ~CommandGpuResourceUpload()
        {
            DESTORY_PTR(_params);
        }
        void Reset() final {}
    };
    struct CommandTranslateState : public GfxCommand
    {
        CMD_CLASS_TYPE(kTransResourceState);
        GpuResource *_res;
        EResourceState _new_state;
        u32 _sub_res;
        CommandTranslateState() : _res(nullptr), _new_state(EResourceState::kCommon), _sub_res(UINT32_MAX) {}
        CommandTranslateState(GpuResource *res, EResourceState new_state, u32 sub_res = UINT32_MAX) : _res(res), _new_state(new_state), _sub_res(sub_res) {}
        void Reset() final {}
    };
    struct CommandCustom : public GfxCommand
    {
        CMD_CLASS_TYPE(kCustom);
        std::function<void()> _func;
        void Reset() final {}
    };
    struct CommandAllocConstBuffer : public GfxCommand
    {
        CMD_CLASS_TYPE(kAllocConstBuffer);
        char _name[64];
        u8 *_data;
        u32 _size;
        i16 _kernel;
        ~CommandAllocConstBuffer()
        {
            AL_FREE(_data);
        }
        void Reset() final {
            AL_FREE(_data);
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
        void Reset() final {}
    };

    struct CommandCopyCounter : public GfxCommand
    {
        CMD_CLASS_TYPE(kCopyCounter);
        GPUBuffer* _src;
        GPUBuffer* _dst;
        u32 _dst_offset;
        void Reset() final {}
    };

    struct CommandPresent : public GfxCommand
    {
        CMD_CLASS_TYPE(kPresent);
        void Reset() final {}
    };
    using ReadbackCallback = std::function<void(const u8*,u32)>;
    struct CommandReadBack : public GfxCommand
    {
        CMD_CLASS_TYPE(kReadBack);
        GpuResource *_res;
        bool _is_buffer;
        bool _is_counter_value;
        u32 _size;
        ReadbackCallback _callback;
        void Reset() final 
        {
            _res = nullptr;
            _callback = nullptr;
        }
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
            if constexpr (std::is_same_v<T, CommandSetTarget>)
            {
                return _pool_set_target.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandClearTarget>)
            {
                return _pool_clear_target.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandDraw>)
            {
                return _pool_draw.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandDispatch>)
            {
                return _pool_dispatch.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandGpuResourceUpload>)
            {
                return _pool_resource_upload.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandTranslateState>)
            {
                return _pool_resource_translate.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandCustom>)
            {
                return _pool_custom.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandAllocConstBuffer>)
            {
                return _pool_alloc_const_buffer.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandProfiler>)
            {
                return _pool_profiler.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandCopyCounter>)
            {
                return _pool_cp_counter.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandPresent>)
            {
                return _pool_present.Pop().value_or(nullptr);
            }
            else if constexpr (std::is_same_v<T, CommandReadBack>)
            {
                return _pool_rb.Pop().value_or(nullptr);
            }
            else {}
            return nullptr;
        }
        void DeAlloc(GfxCommand *cmd)
        {
            cmd->Reset();
            if (cmd->GetCmdType() == EGpuCommandType::kSetTarget)
            {
                _pool_set_target.Push(static_cast<CommandSetTarget*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kClearTarget)
            {
                _pool_clear_target.Push(static_cast<CommandClearTarget*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kDraw)
            {
                _pool_draw.Push(static_cast<CommandDraw*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kDispatch)
            {
                _pool_dispatch.Push(static_cast<CommandDispatch*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kResourceUpload)
            {
                _pool_resource_upload.Push(static_cast<CommandGpuResourceUpload*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kTransResourceState)
            {
                _pool_resource_translate.Push(static_cast<CommandTranslateState*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kCustom)
            {
                _pool_custom.Push(static_cast<CommandCustom*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kAllocConstBuffer)
            {
                _pool_alloc_const_buffer.Push(static_cast<CommandAllocConstBuffer*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kCommandProfiler)
            {
                _pool_profiler.Push(static_cast<CommandProfiler*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kCopyCounter)
            {
               _pool_cp_counter.Push(static_cast<CommandCopyCounter*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kPresent)
            {
                _pool_present.Push(static_cast<CommandPresent*>(cmd));
            }
            else if (cmd->GetCmdType() == EGpuCommandType::kReadBack)
            {
                _pool_rb.Push(static_cast<CommandReadBack*>(cmd));
            }
            else {}
        }

    private:
        Core::LockFreeQueue<CommandSetTarget *, 256> _pool_set_target;
        Core::LockFreeQueue<CommandClearTarget *, 128> _pool_clear_target;
        Core::LockFreeQueue<CommandDraw *, 512> _pool_draw;
        Core::LockFreeQueue<CommandDispatch *, 128> _pool_dispatch;
        Core::LockFreeQueue<CommandGpuResourceUpload *, 512> _pool_resource_upload;
        Core::LockFreeQueue<CommandTranslateState *, 64> _pool_resource_translate;
        Core::LockFreeQueue<CommandCustom *, 256> _pool_custom;
        Core::LockFreeQueue<CommandAllocConstBuffer *, 128> _pool_alloc_const_buffer;
        Core::LockFreeQueue<CommandProfiler *, 256> _pool_profiler;
        Core::LockFreeQueue<CommandPresent *, 32> _pool_present;
        Core::LockFreeQueue<CommandCopyCounter *, 32> _pool_cp_counter;
        Core::LockFreeQueue<CommandReadBack *, 32> _pool_rb;
    };
}// namespace Ailu

#endif// __GPU_COMMAND__H_