#include "Framework/Common/TimeMgr.h"
#include "Framework/Math/ALMath.hpp"
//#include "Framework/Common/Log.h"
#include "pch.h"

namespace Ailu
{
    f32 TimeMgr::s_time_scale = 1.0f;
    static std::mutex s_mark_mutex;
    int TimeMgr::Initialize()
    {
        int ret = 0;
        _init_stamp = std::chrono::high_resolution_clock::now();
        s_pre_tick_stamp = _init_stamp;
        _total_time = 0.f;
        _total_pause_time = 0.f;
        _b_stop = false;
        return ret;
    }
    void TimeMgr::Finalize() { _b_stop = true; }

    void TimeMgr::Tick(f32 delta_time)
    {
        _cur_tick_stamp = std::chrono::high_resolution_clock::now();
        DeltaTime = ALMSecond(_cur_tick_stamp - s_pre_tick_stamp).count();
        s_smooth_delta_time = Lerp(DeltaTime, s_smooth_delta_time, 0.95f);
        s_pre_tick_stamp = _cur_tick_stamp;
        if (!_b_stop)
            TimeSinceLoad += DeltaTime;
    }
    void TimeMgr::Pause()
    {
        if (!_b_stop)
        {
            _pause_stamp = std::chrono::high_resolution_clock::now();
            _last_pause_time = 0.0f;
            _b_stop = true;
        }
    }
    void TimeMgr::Resume()
    {
        if (_b_stop)
        {
            _total_pause_time +=
                    ALMSecond(std::chrono::high_resolution_clock::now() - _pause_stamp)
                            .count();
            _last_pause_time =
                    ALMSecond(std::chrono::high_resolution_clock::now() - _pause_stamp)
                            .count();
            _b_stop = false;
        }
    }
    void TimeMgr::Reset()
    {
        s_pre_tick_stamp = std::chrono::high_resolution_clock::now();
        while (!_mark_stamps.empty())
        {
            _mark_stamps.pop();
        }
    }

    void TimeMgr::Mark()
    {
        std::lock_guard<std::mutex> lock(s_mark_mutex);
        _mark_stamps.push(std::move(std::chrono::high_resolution_clock::now()));
    }
    float TimeMgr::GetElapsedSinceLastMark()
    {
        std::lock_guard<std::mutex> lock(s_mark_mutex);
        if (!_mark_stamps.empty())
        {
            float count = ALMSecond(std::chrono::high_resolution_clock::now() -_mark_stamps.top()).count();
            count -= _last_pause_time;
            _last_pause_time = 0;
            _mark_stamps.pop();
            return count;
        }
        else
        {
            //LOG_WARNING("TimeMgr hasn't mark brfore! will return 0.0");
            return 0.0f;
        }
    }

    float TimeMgr::GetScaledWorldTime(float scale, bool smooth_scale)
    {
        return smooth_scale ? TimeSinceLoad * scale * scale : TimeSinceLoad * scale;
    }

    String TimeMgr::CurrentTime(String format)
    {
        time_t time_seconds = time(0);
        struct tm now_time;
        localtime_s(&now_time, &time_seconds);
        std::stringstream ss;
        ss << std::put_time(&now_time, format.c_str());
        return ss.str();
    }
}// namespace Ailu
