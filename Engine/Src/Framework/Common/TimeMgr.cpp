#include "pch.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
	int TimeMgr::Initialize()
	{
		int ret = 0;
		_init_stamp = std::chrono::high_resolution_clock::now();
		_pre_stamp = _init_stamp;
		_total_time = 0.f;
		_pause_time = 0.f;
		_b_stop = false;
		return ret;
	}
	void TimeMgr::Finalize()
	{
		_b_stop = true;
	}

	void TimeMgr::Tick(const float& delta_time)
	{
		_cur_stamp = std::chrono::high_resolution_clock::now();
		DeltaTime = ALMSecond(_cur_stamp - _pre_stamp).count();
		_pre_stamp = _cur_stamp;
		if (!_b_stop)
		{
			TimeSinceLoad += DeltaTime;
		}
		else
		{

		}
	}
	void TimeMgr::Pause()
	{
		if (!_b_stop)
		{
			_pause_stamp = std::chrono::high_resolution_clock::now();
			_b_stop = true;
		}
	}
	void TimeMgr::Mark()
	{
		_mark_stamps.push(std::move(std::chrono::high_resolution_clock::now()));
	}
	float TimeMgr::GetElapsedSinceLastMark()
	{
		if (!_mark_stamps.empty())
		{
			auto pre_mark = _mark_stamps.top();
			_mark_stamps.pop();
			return ALMSecond(std::chrono::high_resolution_clock::now() - pre_mark).count();
		}
		else
		{
			LOG_WARNING("TimeMgr hasn't mark brfore! will return 0.0");
			return 0.0f;
		}

	}

	float TimeMgr::GetScaledWorldTime(float scale, bool smooth_scale)
	{
		return smooth_scale ? TimeSinceLoad * scale * scale : TimeSinceLoad * scale;
	}


	void TimeMgr::Resume()
	{
		if (_b_stop)
		{
			_pause_time += ALMSecond(std::chrono::high_resolution_clock::now() - _pause_stamp).count();
			_b_stop = false;
		}
	}
	void TimeMgr::Reset()
	{
		_pre_stamp = std::chrono::high_resolution_clock::now();
	}
}
