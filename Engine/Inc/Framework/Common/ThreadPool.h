#pragma once
#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <windows.h>
#include <format>

#include "TimeMgr.h"
#include "GlobalMarco.h"

using std::unique_lock;
using std::packaged_task;
using std::mutex;
using std::bind;
using std::forward;
using std::make_shared;

namespace Ailu
{
	enum class EThreadStatus
	{
		kNotStarted,
		kRunning,
		kCompleted
	};
	class ThreadPool
	{
	public:
		inline static uint8_t _s_cur_thread_num = 0;
		using Task = std::function<void()>;
		ThreadPool(std::string name = "ALThreadPool") : _pool_name(name)
		{
			uint8_t hd_cy = std::thread::hardware_concurrency();
			_s_cur_thread_num += 4;
			if (_s_cur_thread_num > 1.5 * hd_cy)
			{			
				LOG_WARNING("The number of threads currently running({}) is greater than the hardware recommendation, note the performance tradeoff", _s_cur_thread_num)
			}
			Start(4);
		}
		ThreadPool(uint8_t thread_num, std::string name = "ALThreadPool") : _initial_thread_count(thread_num), _pool_name(name)
		{
			_s_cur_thread_num += thread_num;
			if (_s_cur_thread_num > 1.5 * std::thread::hardware_concurrency())
			{
				LOG_WARNING("The number of threads currently running({}) is greater than the hardware recommendation, note the performance tradeoff", _s_cur_thread_num)
			}
			Start(thread_num);
		}
		~ThreadPool()
		{
			Release();
		}
		EThreadStatus GetThreadStatus(uint8_t thread_index) const
		{
			if (thread_index < _threads.size()) {
				return _thread_status[thread_index];
			}
			return EThreadStatus::kNotStarted;
		}
		uint8_t GetIdleThreadNum() const
		{
			uint8_t count = 0;
			for (auto status : _thread_status)
			{
				if (status == EThreadStatus::kNotStarted) count++;
			}
			return count;
		}
		template<typename Callable, typename...Args>
		auto Enqueue(Callable&& task, Args&&... args) -> std::future<std::invoke_result_t<Callable, Args...>>
		{
			if (_b_stopping.load())
				throw std::runtime_error(std::format("Thread pool: {0} already been released!!",_pool_name));
			using return_type = typename std::invoke_result_t<Callable, Args...>;
			auto wrapper = make_shared<packaged_task<return_type()>>(bind(forward<Callable>(task), std::forward<Args>(args)...));
			std::future<return_type> ret = wrapper->get_future();
			{
				unique_lock<mutex> lock(_task_mutex);
				_tasks.emplace([=]() {
					try
					{
						(*wrapper)();
					}
					catch (const std::exception& e)
					{
						LOG_ERROR("Thread pool {0}: Exception in Task: {1}",_pool_name,e.what())
					}
					});
			}
			_task_cv.notify_one();
			return ret;
		}
		//template<typename Callable, typename...Args>
		//auto Enqueue(Callable&& task, Args&&... args) -> std::future<std::invoke_result_t<Callable, Args...>>
		//{
		//	using return_type = typename std::invoke_result_t<Callable, Args...>;
		//	auto wrapper = [task = std::forward<Callable>(task), args = std::make_tuple(std::forward<Args>(args)...)]() {
		//		std::apply(std::move(task), std::move(args));
		//		};
		//	std::future<return_type> ret;
		//	{
		//		std::unique_lock<std::mutex> lock(_task_mutex);
		//		ret = wrapper.get_future();
		//		_tasks.emplace(std::move(wrapper));
		//	}
		//	_task_cv.notify_one();
		//	return ret;
		//}

		void Release()
		{
			Stop();
			_threads.clear();
			_s_cur_thread_num -= _initial_thread_count;
			_initial_thread_count = 0;
		}
	private:
		void Start(size_t num_threads)
		{
			LOG_INFO("Thread pool {} is starting...", _pool_name);
			_timers.resize(num_threads, TimeMgr());
			_thread_status.resize(num_threads, EThreadStatus::kNotStarted);
			for (auto i = 0u; i < num_threads; ++i)
			{
				std::wstring thread_name = std::format(L"ALEngineWorkThread{0}", _s_global_thread_id++);
				_threads.emplace_back([=]() {
					SetThreadDescription(GetCurrentThread(), thread_name.c_str());
					while (true)
					{
						Task task;
						{
							std::unique_lock<std::mutex> lock(_task_mutex);
							_task_cv.wait(lock, [=]() {return _b_stopping.load() || !_tasks.empty(); });
							if (_b_stopping.load() && _tasks.empty())
								break;
							_thread_status[i] = EThreadStatus::kRunning;
							task = std::move(_tasks.front());
							_tasks.pop();
						}
						_timers[i].Mark();
						task();
						LOG_INFO(L"{} has finish task after {}ms!", thread_name.c_str(),_timers[i].GetElapsedSinceLastMark());
						_thread_status[i] = EThreadStatus::kCompleted;
					}
					});
			}
		}
		void Stop() noexcept
		{
			{
				std::unique_lock<std::mutex> lock(_task_mutex);
				_b_stopping.store(true);
			}
			_task_cv.notify_all();
			for (auto& t : _threads)
				t.join();
		}
	private:
		uint8_t _initial_thread_count = 0;
		inline static u8 _s_global_thread_id = 0u;
		std::list<std::thread> _threads;
		std::condition_variable _task_cv;
		std::mutex _task_mutex;
		std::atomic<bool> _b_stopping;
		std::queue<Task> _tasks;
		std::vector<EThreadStatus> _thread_status;
		std::string _pool_name;
		std::vector<TimeMgr> _timers;
	};
	extern Scope<ThreadPool> g_thread_pool;
}


#endif // !THREAD_POOL_H__
