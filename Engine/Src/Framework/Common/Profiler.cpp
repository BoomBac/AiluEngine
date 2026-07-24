#include "Framework/Common/Profiler.h"
#include "Framework/Common/Application.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "pch.h"

using namespace Ailu::Render;

namespace Ailu
{
    namespace
    {
        constexpr u32 kInvalidProfileIndex = u32(-1);

        bool CanRecycleProfileSlot(const ProfileData &profile, u64 current_frame)
        {
            return !profile._is_start &&
                   profile._is_finished &&
                   !profile._is_active &&
                   profile._record.empty() &&
                   (profile._last_used_frame + 1u) < current_frame;
        }

        void ResetProfileSlot(ProfileData &profile, const String &name, bool is_cpu_profile, u64 current_frame)
        {
            profile = ProfileData{};
            profile.Name = name;
            profile._is_cpu_profile = is_cpu_profile;
            profile._last_used_frame = current_frame;
        }

        template <size_t N>
        u32 FindProfileSlot(Array<ProfileData, N> &profiles, u32 used_count, const String &name, u64 current_frame)
        {
            u32 reusable_idx = kInvalidProfileIndex;
            for (u32 i = 0; i < used_count; ++i)
            {
                auto &profile = profiles[i];
                if (profile.Name == name)
                {
                    return i;
                }
                if (reusable_idx == kInvalidProfileIndex && CanRecycleProfileSlot(profile, current_frame))
                {
                    reusable_idx = i;
                }
            }
            if (reusable_idx != kInvalidProfileIndex)
            {
                return reusable_idx;
            }
            AL_ASSERT(used_count < N);
            return used_count;
        }
    }

    static Profiler g_Profiler;
    std::thread::id s_main_thread_id;
    Profiler &Profiler::Get()
    {
        return g_Profiler;
    }

    void Profiler::Initialize()
    {
        g_Profiler._p_gpu_timer = GraphicsContext::Get().GetTimer();
        for(const auto& [id,name] : GetAllThreadNameMap())
        {
            g_Profiler._cpu_profiler_queue[id] = List<BlockMark>();
        }
        s_main_thread_id = std::this_thread::get_id();
    }
    void Profiler::Shutdown()
    {
    }
    u32 Profiler::StartGpuProfile(RHICommandBuffer *cmdList, const String &name)
    {
        //std::lock_guard<std::mutex> lock(_lock);
        u32 profileIdx = FindProfileSlot(_gpu_profiles, numProfiles, name, _cur_frame);
        if (profileIdx == numProfiles)
        {
            AL_ASSERT(numProfiles < kMaxProfileNum);
            ++numProfiles;
        }
        auto &profile_data = _gpu_profiles[profileIdx];
        if (profile_data.Name != name)
        {
            ResetProfileSlot(profile_data, name, false, _cur_frame);
        }
        AL_ASSERT(profile_data._is_start != true);
        AL_ASSERT(profile_data._is_finished != false);
        profile_data._is_cpu_profile = false;
        profile_data._is_active = true;
        profile_data._last_used_frame = _cur_frame;
        _p_gpu_timer->Start(cmdList, profileIdx);
        profile_data._is_start = true;
        profile_data._is_finished = false;
        profile_data._start_time = TimeMgr::Get().GetElapsedSinceCurrentTick();
        return profileIdx;
    }
    void Profiler::EndGpuProfile(RHICommandBuffer *cmdList, u32 idx)
    {
        //std::lock_guard<std::mutex> lock(_lock);
        AL_ASSERT(idx < numProfiles);
        ProfileData &profile_data = _gpu_profiles[idx];
        AL_ASSERT(profile_data._is_start != false);
        AL_ASSERT(profile_data._is_finished != true);
        _p_gpu_timer->Stop(cmdList, idx);
        profile_data._time = (f32)_p_gpu_timer->GetElapsedMS(idx);
        profile_data._is_start = false;
        profile_data._is_finished = true;
        profile_data._last_used_frame = _cur_frame;
        // if (_is_record_mode)
        //     profile_data.PushRecord(_cur_frame);
    }
    u32 Profiler::StartCPUProfile(const String &name)
    {
        std::lock_guard<std::mutex> lock(_lock);
        u32 profileIdx = FindProfileSlot(_cpu_profiles, numCPUProfiles, name, _cur_frame);
        if (profileIdx == numCPUProfiles)
        {
            AL_ASSERT(numCPUProfiles < kMaxProfileNum);
            ++numCPUProfiles;
        }
        auto &profile_data = _cpu_profiles[profileIdx];
        if (profile_data.Name != name)
        {
            ResetProfileSlot(profile_data, name, true, _cur_frame);
        }
        AL_ASSERT(profile_data._is_start != true);
        AL_ASSERT(profile_data._is_finished != false);
        profile_data.Name = name;
        profile_data._is_cpu_profile = true;
        profile_data._is_active = true;
        profile_data._last_used_frame = _cur_frame;
        TimeMgr::Get().Mark();
        // TimeMgr local_cpu_timer.Mark();
        profile_data._is_start = true;
        profile_data._is_finished = false;
        profile_data._start_time = TimeMgr::Get().GetElapsedSinceLaunch();
        //LOG_INFO("Start CPU Profile: {} at frame {} with name {}", profileIdx,g_pGfxContext->GetFrameCount(),name);
        return profileIdx;
    }
    void Profiler::EndCPUProfile(u32 idx)
    {
        std::lock_guard<std::mutex> lock(_lock);
        AL_ASSERT(idx < numCPUProfiles);
        ProfileData &profile_data = _cpu_profiles[idx];
        AL_ASSERT(profile_data._is_start != false);
        AL_ASSERT(profile_data._is_finished != true);
        profile_data._time = TimeMgr::Get().GetElapsedSinceLastMark();
        profile_data._is_start = false;
        profile_data._is_finished = true;
        profile_data._last_used_frame = _cur_frame;
        if (_is_record_mode)
            profile_data.PushRecord(_cur_frame);
    }
    void Profiler::AddCPUProfilerHierarchy(bool begin_profiler, u32 index)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto tid = std::this_thread::get_id();
        _cpu_profiler_queue[tid].emplace_back(begin_profiler, index, tid,_cpu_profiles[index].Name,_cur_frame);
    }
    void Profiler::AddGPUProfilerHierarchy(bool begin_profiler, u32 index)
    {
        // if (!_is_record_mode)
        // 	return;
        // std::lock_guard<std::mutex> lock(_lock);
        _gpu_profiler_queue.emplace(begin_profiler, index, std::this_thread::get_id(), _gpu_profiles[index].Name,_cur_frame);
    }

    void Profiler::CollectGPUTimeData()
    {
        while (!Profiler::Get()._gpu_profiler_queue.empty())
        {
            const auto &marker = _gpu_profiler_queue.front();
            if (marker._is_start && !marker._name.empty())
            {
                const auto &profiler = Profiler::Get().GetGpuProfileData(marker._profiler_index);
                _gpu_time[marker._name] = profiler->_avg_time;
            }
            _gpu_profiler_queue.pop();
        }
    }

    static List<Profiler::BlockMark> CleanCompletedProfiles(const List<Profiler::BlockMark> &queue)
    {
        List<Profiler::BlockMark> mark_not_finished;
        std::set<u32> processed_profile;
        for(auto it = queue.rbegin(); it != queue.rend(); ++it)
        {
            if (processed_profile.contains(it->_profiler_index))
                continue;
            if (it->_is_start)
            {
                mark_not_finished.push_back(*it);
                //LOG_INFO("Frame {}: profile {} not finish",Application::Get().GetFrameCount(),it->_name);
            }
            processed_profile.insert(it->_profiler_index);
        }
        mark_not_finished.reverse();
        return mark_not_finished;
    }
    void Profiler::BeginFrame()
    {
        _is_record_mode = _is_start_record;
        _cur_frame = Application::Get().GetFrameCount();
    }

    void Profiler::EndFrame()
    {
        static auto update_profile = [](ProfileData &profile)
        {
            if (profile._is_start && !profile._is_finished)
            {
                return;
            }
            profile._time_samples[profile._index] = profile._time;
            profile._index = (profile._index + 1) % ProfileData::kFilterSize;
            f32 maxTime = 0.0;
            f32 avgTime = 0.0;
            u64 avgTimeSamples = 0;
            for (UINT i = 0; i < ProfileData::kFilterSize; ++i)
            {
                if (profile._time_samples[i] <= 0.0)
                    continue;
                maxTime = std::max<f32>(profile._time_samples[i], maxTime);
                avgTime += profile._time_samples[i];
                ++avgTimeSamples;
            }

            if (avgTimeSamples > 0)
                avgTime /= avgTimeSamples;
            profile._avg_time = avgTime;
            profile._max_time = maxTime;
            profile._is_active = false;
        };
        for (u32 i = 0; i < numCPUProfiles; i++)
        {
            update_profile(_cpu_profiles[i]);
        }
        for (u32 i = 0; i < numProfiles; i++)
        {
            update_profile(_gpu_profiles[i]);
        }
        if (_is_record_mode)
        {
            if (_is_clear_cache)//保证存储的为连续帧
            {
                _cache_data.clear();
                _is_clear_cache = false;
            }
            else
            {
                while (_cache_data.size() > kMaxCacheDataNum)
                    _cache_data.pop_front();
            }
            ProfileFrameData frame_data;
            frame_data._frame_index = Application::Get().GetFrameCount();
            frame_data._start_time = TimeMgr::Get().TickTimeSinceLoad * 1000;//s->ms
            frame_data._end_time = 1.0f;
            {
                std::lock_guard<std::mutex> lock(_lock);
                for (auto &[tid,block_list]: _cpu_profiler_queue)
                {
                    i16 call_depth = 0;
                    for (auto &block: block_list)
                    {
                        auto &profile_data = _cpu_profiles[block._profiler_index];
                        call_depth = block._is_start ? call_depth + 1 : call_depth - 1;
                        if (!block._is_start)
                        {
                            //AL_ASSERT(profile_data._record.size() < 3);
                            if (auto record = profile_data.PopRecord();record.has_value())
                            {
                                if (tid == s_main_thread_id)
                                    frame_data._end_time = std::max<f32>(record.value()._duration, frame_data._end_time);
                                frame_data._data_threads[tid].emplace_back(profile_data.Name, record.value()._start, record.value()._duration, (u16) call_depth);
                            }
                            else
                            {
                                AL_ASSERT(true);
                            }
                        }
                    }
                }
                frame_data._end_time += TimeMgr::Get().TickTimeSinceLoad * 1000;//s->ms
                _cache_data.push_back(frame_data);
                for(auto& it : _cpu_profiler_queue)
                {
                    it.second = CleanCompletedProfiles(it.second);
                }
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(_lock);
            for(auto& it : _cpu_profiler_queue)
            {
                it.second = CleanCompletedProfiles(it.second);
            }
        }
    }
    //----------------------------------------------------------------------------------GpuProfileBlock------------------------------------------------------------------------------
    GpuProfileBlock::GpuProfileBlock(CommandBuffer *cmdList, const String &name) : _cmdList(cmdList), _name(name)
    {
// #if defined(TRACY_ENABLE)
//         TracyCZone(_tracy_ctx, true);
//         TracyCZoneName(_tracy_ctx, _name.c_str(), _name.size());
// #endif
        //cmdList->BeginProfiler(name);
        //		idx = Profiler::Get().StartGpuProfile(cmdList, name);
        //		Profiler::Get().AddGPUProfilerHierarchy(true, idx);
    }
    GpuProfileBlock::~GpuProfileBlock()
    {
        //_cmdList->EndProfiler();
        //		Profiler::Get().EndGpuProfile(cmdList, idx);
        //		Profiler::Get().AddGPUProfilerHierarchy(false, idx);

// #if defined(TRACY_ENABLE)
//         TracyCZoneEnd(_tracy_ctx);
// #endif
    }
    //----------------------------------------------------------------------------------GpuProfileBlock------------------------------------------------------------------------------

    //----------------------------------------------------------------------------------CPUProfileBlock------------------------------------------------------------------------------
    CPUProfileBlock::CPUProfileBlock(const String &name)
    {
#if defined(TRACY_ENABLE)
        _owner_thread_id = std::this_thread::get_id();
        static const tracy::SourceLocationData tracy_cpu_profileblock_srcloc{ "CPUProfileBlock", "CPUProfileBlock", __FILE__, (uint32_t)__LINE__, 0 };
        _tracy_zone.emplace(&tracy_cpu_profileblock_srcloc, true);
        _tracy_zone->Name(name.c_str(), name.size());
#endif
        //std::lock_guard<std::mutex> lock(_mutex);
        //idx = Profiler::Get().StartCPUProfile(name);
        //Profiler::Get().AddCPUProfilerHierarchy(true, idx);
    }
    CPUProfileBlock::~CPUProfileBlock()
    {
    #if defined(TRACY_ENABLE)
        // Tracy requires zone begin/end on the same thread.
        AL_ASSERT(_owner_thread_id == std::this_thread::get_id());
    #endif
        //std::lock_guard<std::mutex> lock(_mutex);
        //Profiler::Get().EndCPUProfile(idx);
        //Profiler::Get().AddCPUProfilerHierarchy(false, idx);

    #if defined(TRACY_ENABLE)
        // End the zone after CPU profiler bookkeeping.
        _tracy_zone.reset();
    #endif
    }
    //----------------------------------------------------------------------------------CPUProfileBlock------------------------------------------------------------------------------
}// namespace Ailu
