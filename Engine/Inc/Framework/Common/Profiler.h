#pragma once
#ifndef __PROFILER_H__
#define __PROFILER_H__
#include "Container.hpp"
#include "GlobalMarco.h"
#include "Render/RenderConstants.h"
#include "TimeMgr.h"
#include <optional>
#include <queue>

namespace Ailu
{
    struct ProfileData
    {
        static const u32 kFilterSize = 64u;
        String Name;

        bool _is_start = false;
        bool _is_finished = true;
        bool _is_active = false;

        bool _is_cpu_profile = false;
        f32 _time = 0.0f;
        f32 _max_time = 0.0f;
        f32 _avg_time = 0.0f;
        f32 _start_time = 0.0f;//since launch

        f32 _time_samples[kFilterSize] = {};
        u32 _index = 0u;
        struct Record
        {
            f32 _start;
            f32 _duration;
            u64 _frame;
        };
        void PushRecord(u64 frame)
        {
            _record.emplace(_start_time, _time, frame);
        };
        std::optional<Record> PopRecord()
        {
            if (_record.empty())
                return std::nullopt;
            auto ret = std::make_optional(_record.front());
            _record.pop();
            return ret;
        }
        std::queue<Record> _record;
    };

    struct ProfileFrameData
    {
        struct ProfileEditorData
        {
            String _name;
            f32 _start_time;//自当前帧
            f32 _duration;
            u16 _call_depth;
            ProfileEditorData(const String &name, f32 start_time, f32 duration, u16 call_depth) : _name(name), _start_time(start_time), _duration(duration), _call_depth(call_depth) {}
        };
        u64 _frame_index;
        f32 _start_time;//自程序启动
        f32 _end_time;  //自程序启动
        Map<std::thread::id, List<ProfileEditorData>> _data_threads;
    };
    namespace Render{class RHICommandBuffer;}
    class AILU_API Profiler
    {
        friend class GpuProfileBlock;
        friend class CPUProfileBlock;

    public:
        inline static constexpr u32 kMaxProfileNum = Render::RenderConstants::kMaxGpuTimerNum;
        inline static constexpr u32 kMaxCacheDataNum = 300;

        static Profiler &Get();
        static void Initialize();
        static void Shutdown();

        u32 StartGpuProfile(Render::RHICommandBuffer *cmdList, const String &name);
        void EndGpuProfile(Render::RHICommandBuffer *cmdList, u32 idx);

        u32 StartCPUProfile(const String &name);
        void EndCPUProfile(u32 idx);

        ProfileData *GetCPUProfileData(u32 idx) { return &_cpu_profiles[idx]; };
        ProfileData *GetGpuProfileData(u32 idx) { return &_gpu_profiles[idx]; };

        const List<ProfileFrameData> &GetFrameData()
        {
            _is_clear_cache = true;
            return _cache_data;
        }
        void BeginFrame();
        void EndFrame();
        void AddCPUProfilerHierarchy(bool begin_profiler, u32 index);
        void AddGPUProfilerHierarchy(bool begin_profiler, u32 index);
        struct BlockMark
        {
            bool _is_start;
            u32 _profiler_index;
            std::thread::id _tid;
            String _name;
            u64 _frame;
        };
        void StartRecord() { _is_start_record = true; };
        void StopRecord() { _is_start_record = false; };

        Queue<BlockMark> _gpu_profiler_queue;

    public:
    private:
    private:
        Array<ProfileData, kMaxProfileNum> _gpu_profiles;
        Array<ProfileData, kMaxProfileNum> _cpu_profiles;
        u32 numProfiles = 0;
        u32 numCPUProfiles = 0;
        TimeMgr _cpu_timer;
        IGPUTimer *_p_gpu_timer = nullptr;
        std::mutex _lock;
        List<ProfileFrameData> _cache_data;
        bool _is_clear_cache = true;
        bool _is_record_mode = false;
        bool _is_start_record = false;
        HashMap<std::thread::id, List<BlockMark>> _cpu_profiler_queue;
        u64 _cur_frame;
    };

    namespace Render
    {
        class CommandBuffer;
    }

    //command must been execute out of prifile scope
    class AILU_API GpuProfileBlock
    {
    public:
        GpuProfileBlock(Render::CommandBuffer *cmdList, const String &name);
        ~GpuProfileBlock();

    private:
        Render::CommandBuffer *_cmdList = nullptr;
        String _name;
    };

    class AILU_API CPUProfileBlock
    {
    public:
        explicit CPUProfileBlock(const String &name);
        ~CPUProfileBlock();

    private:
        u32 idx = u32(-1);
        inline static std::mutex _mutex;
    };
#define PROFILE_BLOCK_GPU(cmd, block_name) GpuProfileBlock block_name##_GPU(cmd, #block_name);
#define PROFILE_BLOCK_CPU(block_name) CPUProfileBlock block_name##_CPU(#block_name);
}// namespace Ailu


#endif// !PROFILER_H__
