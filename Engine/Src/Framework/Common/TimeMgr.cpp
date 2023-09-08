#include "pch.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
	int TimeMgr::Initialize()
	{
		int ret = 0;
		_init_stamp = std::chrono::high_resolution_clock::now();
		_mark_stamp = _init_stamp;
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
	void TimeMgr::Tick()
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
		_mark_stamp = std::chrono::high_resolution_clock::now();
	}
	float TimeMgr::GetElapsedSinceLastMark() const
	{
		return ALMSecond(std::chrono::high_resolution_clock::now() - _mark_stamp).count();
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
