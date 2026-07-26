#ifndef __GPU_COMMAND__H_
#define __GPU_COMMAND__H_

#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Container.hpp"
#include "Framework/Common/Log.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Array.h"
#include "GpuResource.h"
#include "RenderConstants.h"
#include <cstddef>
#include <functional>
#include <mutex>
#include <new>
#include <type_traits>

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
    struct RayTracingGeometryDesc;
    class RayTracingScene;
    class RayTracingShader;
    enum class EGpuCommandType : u8
    {
        kSetTarget,
        kClearTarget,
        kDraw,
        kDispatch,
        kResourceUpload,
        kTransResourceState,
        kUAVBarrier,
        kAllocConstBuffer,
        kCommandProfiler,
        kCopyCounter,
        kReadBack,
        kPresent,
        kScissorRect,
        kBuildAS,
        kDispatchRays,
        kCustom
    };

    const char *GfxCommandTypeName(EGpuCommandType type);


    struct GfxCommand
    {
        explicit constexpr GfxCommand(EGpuCommandType type) : _type(type) {}
        EGpuCommandType GetCmdType() const { return _type; }
        const char *GetName() const { return GfxCommandTypeName(_type); }

        EGpuCommandType _type;
    };

    template<EGpuCommandType Type>
    struct TypedGfxCommand : public GfxCommand
    {
        TypedGfxCommand() : GfxCommand(Type) {}
        static constexpr EGpuCommandType GetStaticType() { return Type; }
    };

    template<typename T, typename TBase = GfxCommand>
    inline void SafeResetCommand(T *cmd)
    {
        static_assert(std::is_base_of_v<TBase, T>, "T must inherit from GfxCommand");
        std::memset(
                reinterpret_cast<uint8_t *>(cmd) + sizeof(TBase),
                0,
                sizeof(T) - sizeof(TBase));
    }

    struct CommandSetTarget : public TypedGfxCommand<EGpuCommandType::kSetTarget>
    {
        Array<RenderTexture *, RenderConstants::kMaxMRTNum> _color_target;
        RenderTexture *_depth_target;
        u16 _color_target_num;
        Array<u16, RenderConstants::kMaxMRTNum> _color_indices;
        Array<Rect, RenderConstants::kMaxMRTNum> _viewports;
        u16 _depth_index;
        bool _is_scissor_rect;
        void Reset()
        {
            SafeResetCommand(this);
        }
    };
    enum EClearFlag
    {
        kColor = 1 << 0,
        kDepth = 1 << 1,
        kStencil = 1 << 2,
        kAll = kColor | kDepth | kStencil
    };
    struct CommandClearTarget : public TypedGfxCommand<EGpuCommandType::kClearTarget>
    {
        EClearFlag _flag;
        u16 _color_target_num;
        Array<Color, RenderConstants::kMaxMRTNum> _colors;
        f32 _depth;
        u8 _stencil;
        void Reset() {
            SafeResetCommand(this);
        }
    };
    struct CommandDraw : public TypedGfxCommand<EGpuCommandType::kDraw>
    {
        struct
        {
            u8* _data;
            u32 _size;
        } _material_property_block;
        VertexBuffer *_vb;
        IndexBuffer *_ib;
        Material *_mat;
        u16 _pass_index;
        ConstantBuffer *_per_obj_cb;
        u32 _instance_count;
        u32 _start_instance;
        u16 _sub_mesh;
        u32 _vertex_count;
        u32 _index_start;
        u32 _index_num;
        GPUBuffer* _arg_buffer;
        u32 _arg_offset;

        void Reset()
        {
            SafeResetCommand(this);
        }
    };
    struct CommandDispatch : public TypedGfxCommand<EGpuCommandType::kDispatch>
    {
        ComputeShader *_cs;
        u16 _kernel;
        u16 _group_num_x;
        u16 _group_num_y;
        u16 _group_num_z;
        GPUBuffer* _arg_buffer;
        u16 _arg_offset;
        void Reset()
        {
            SafeResetCommand(this);
        }
    };
    struct CommandGpuResourceUpload : public TypedGfxCommand<EGpuCommandType::kResourceUpload>
    {
        GpuResource *_res;
        UploadParams *_params;
        ~CommandGpuResourceUpload()
        {
            delete _params; _params = nullptr;
        }
        void Reset() {
            SafeResetCommand(this);
        }
    };
    struct CommandTranslateState : public TypedGfxCommand<EGpuCommandType::kTransResourceState>
    {
        GpuResource *_res;
        EResourceState _new_state;
        u32 _sub_res;
        CommandTranslateState() : _res(nullptr), _new_state(EResourceState::kCommon), _sub_res(UINT32_MAX) {}
        CommandTranslateState(GpuResource *res, EResourceState new_state, u32 sub_res = UINT32_MAX) : _res(res), _new_state(new_state), _sub_res(sub_res) {}
        void Reset() {
            SafeResetCommand(this);
        }
    };
    struct CommandUAVBarrier : public TypedGfxCommand<EGpuCommandType::kUAVBarrier>
    {
        GpuResource *_res;
        CommandUAVBarrier() : _res(nullptr) {}
        explicit CommandUAVBarrier(GpuResource *res) : _res(res) {}
        void Reset() {
            SafeResetCommand(this);
        }
    };

    struct CommandBuildAS : public TypedGfxCommand<EGpuCommandType::kBuildAS>
    {
        bool _is_update;
        bool _is_blas;
        // 通用
        GpuResource* _dst;      // 目标AS
        GpuResource* _src;      // update用（BLAS/TLAS都可能用到）

        u64 _scratch_size;

        // ===== BLAS =====
        struct
        {
            const RayTracingGeometryDesc* _geometries;
            u32 _geometry_count;
        } _blas;

        // ===== TLAS =====
        struct
        {
            GPUBuffer* _instance_buffer;
            u32 _instance_count;
        } _tlas;

        void Reset()
        {
            SafeResetCommand(this);
        }
    };

    struct CommandCustom : public TypedGfxCommand<EGpuCommandType::kCustom>
    {
        std::function<void()> _func;
        void Reset() {
            SafeResetCommand(this);
        }
    };
    struct CommandAllocConstBuffer : public TypedGfxCommand<EGpuCommandType::kAllocConstBuffer>
    {
        char _name[64];
        u8 *_data;
        u32 _size;
        i16 _kernel;
        ~CommandAllocConstBuffer()
        {
            AL_FREE(_data);
        }
        void Reset() {
            AL_FREE(_data);
            SafeResetCommand(this);
        }
    };
    //同时也会创建一个cpu profile
    struct CommandProfiler : public TypedGfxCommand<EGpuCommandType::kCommandProfiler>
    {
        String _name;
        bool _is_start;
        u32 _cpu_index;//用于索引具体的profiler，处理begin事件时赋值，遇到end事件时，找最近的begin事件
        u32 _gpu_index;//用于索引具体的profiler，处理begin事件时赋值，遇到end事件时，找最近的begin事件
        void Reset() {
            _name.clear();
            _is_start = false;
            _cpu_index = 0u;
            _gpu_index = 0u;
        }
    };

    struct CommandCopyCounter : public TypedGfxCommand<EGpuCommandType::kCopyCounter>
    {
        GPUBuffer* _src;
        GPUBuffer* _dst;
        u32 _dst_offset;
        void Reset() {
            SafeResetCommand(this);
        }
    };

    struct CommandPresent : public TypedGfxCommand<EGpuCommandType::kPresent>
    {
        void Reset() {}
    };

    struct CommandScissor : public TypedGfxCommand<EGpuCommandType::kScissorRect>
    {
        Array<Rect, RenderConstants::kMaxMRTNum> _rects;
        u16 _num;
        void Reset() {
            SafeResetCommand(this);
        }
    };

    using ReadbackCallback = std::function<void(const u8*,u32)>;
    struct CommandReadBack : public TypedGfxCommand<EGpuCommandType::kReadBack>
    {
        GpuResource *_res;
        bool _is_buffer;
        bool _is_counter_value;
        u32 _size;
        ReadbackCallback _callback;
        void Reset()
        {
            SafeResetCommand(this);
        }
    };

    struct CommandDispatchRays : public TypedGfxCommand<EGpuCommandType::kDispatchRays>
    {
        RayTracingShader *_shader;
        RayTracingScene *_scene;
        u16 _w;
        u16 _h;
        u16 _depth;
        void Reset()
        {
            SafeResetCommand(this);
        }
    };

    constexpr size_t MaxCommandValue(size_t lhs, size_t rhs)
    {
        return lhs > rhs ? lhs : rhs;
    }

    inline constexpr size_t kCommandPayloadSize = MaxCommandValue(sizeof(CommandSetTarget),
        MaxCommandValue(sizeof(CommandClearTarget),
        MaxCommandValue(sizeof(CommandDraw),
        MaxCommandValue(sizeof(CommandDispatch),
        MaxCommandValue(sizeof(CommandGpuResourceUpload),
        MaxCommandValue(sizeof(CommandTranslateState),
        MaxCommandValue(sizeof(CommandUAVBarrier),
        MaxCommandValue(sizeof(CommandCustom),
        MaxCommandValue(sizeof(CommandAllocConstBuffer),
        MaxCommandValue(sizeof(CommandProfiler),
        MaxCommandValue(sizeof(CommandCopyCounter),
        MaxCommandValue(sizeof(CommandPresent),
        MaxCommandValue(sizeof(CommandScissor),
        MaxCommandValue(sizeof(CommandDispatchRays),
        MaxCommandValue(sizeof(CommandReadBack), sizeof(CommandBuildAS))))))))))))))));

    inline constexpr size_t kCommandPayloadAlign = MaxCommandValue(alignof(CommandSetTarget),
        MaxCommandValue(alignof(CommandClearTarget),
        MaxCommandValue(alignof(CommandDraw),
        MaxCommandValue(alignof(CommandDispatch),
        MaxCommandValue(alignof(CommandGpuResourceUpload),
        MaxCommandValue(alignof(CommandTranslateState),
        MaxCommandValue(alignof(CommandUAVBarrier),
        MaxCommandValue(alignof(CommandCustom),
        MaxCommandValue(alignof(CommandAllocConstBuffer),
        MaxCommandValue(alignof(CommandProfiler),
        MaxCommandValue(alignof(CommandCopyCounter),
        MaxCommandValue(alignof(CommandPresent),
        MaxCommandValue(alignof(CommandScissor), 
        MaxCommandValue(alignof(CommandDispatchRays), 
        MaxCommandValue(alignof(CommandReadBack), alignof(CommandBuildAS))))))))))))))));

    struct alignas(kCommandPayloadAlign) CommandPayload
    {
        u8 _storage[kCommandPayloadSize];

        void *Data() { return _storage; }
        const void *Data() const { return _storage; }

        static CommandPayload *FromCommand(GfxCommand *cmd)
        {
            return reinterpret_cast<CommandPayload *>(cmd);
        }
    };

    static_assert(offsetof(CommandPayload, _storage) == 0, "Command payload storage must start at byte 0");

    template<typename T>
    inline T *ConstructCommand(CommandPayload *payload)
    {
        static_assert(std::is_base_of_v<GfxCommand, T>, "T must inherit from GfxCommand");
        static_assert(sizeof(T) <= sizeof(CommandPayload), "payload block is too small for command type");
        static_assert(alignof(T) <= alignof(CommandPayload), "payload block alignment is too small for command type");
        return new (payload->Data()) T();
    }

    void DestroyCommand(GfxCommand *cmd);


    class CommandPool
    {
    public:
        inline static constexpr u32 kCommandPoolPayloadCount = 5000u;
    public:
        CommandPool() = default;
        ~CommandPool() = default;
        static CommandPool &Get();
        static void Init();
        static void Shutdown();
        template<typename T>
        T *Alloc()
        {
            auto payload = _payload_pool.Pop().value_or(nullptr);
            if (payload == nullptr)
                return nullptr;
            return ConstructCommand<T>(payload);
        }
        void DeAlloc(GfxCommand *cmd)
        {
            if (cmd == nullptr)
                return;
            DestroyCommand(cmd);
            _payload_pool.Push(CommandPayload::FromCommand(cmd));
        }

    private:
        Core::ParallelQueue<CommandPayload *> _payload_pool;
    };
}// namespace Ailu

#endif// __GPU_COMMAND__H_
