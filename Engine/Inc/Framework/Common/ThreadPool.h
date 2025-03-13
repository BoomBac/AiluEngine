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
#include <Windows.h>
#include <format>
#include <utility>

#include "TimeMgr.h"
#include "GlobalMarco.h"
#include "Framework/Common/Log.h"
#include "Container.hpp"

using std::unique_lock;
using std::packaged_task;
using std::mutex;
using std::bind;
using std::forward;
using std::make_shared;

namespace Ailu
{
    DECLARE_ENUM(EThreadStatus, kNotStarted, kRunning, kIdle);
	class AILU_API ThreadPool
	{
	public:
		DISALLOW_COPY_AND_ASSIGN(ThreadPool);
        struct Task
        {
            String _name;
			std::function<void()> _task;
        };
		explicit ThreadPool(u8 thread_num, std::string name = "ALThreadPool") : _initial_thread_count(thread_num), _pool_name(std::move(name)), _tasks(256)
		{
			s_total_thread_num += thread_num;
			if (s_total_thread_num > 1.5 * std::thread::hardware_concurrency())
			{
				LOG_WARNING("The number of threads currently running({}) is greater than the hardware recommendation, note the performance tradeoff", s_total_thread_num)
			}
			Start(thread_num);
            s_is_inited = true;
		}
        explicit ThreadPool(std::string name = "ALThreadPool") : ThreadPool(std::thread::hardware_concurrency(), std::move(name)) {};
		~ThreadPool()
		{
			Release();
		}
        u16 ThreadNum() const { return _initial_thread_count; }
		void ClearRecords() 
		{ 
			for (auto &record: _task_time_records)
                record.clear();
		}
		const Vector<EThreadStatus::EThreadStatus>& StatusView() const{return _thread_status;}
		EThreadStatus::EThreadStatus Status(u16 thread_id) const
		{
			if (thread_id >= _thread_status.size())
				return EThreadStatus::kNotStarted;
			return _thread_status[thread_id];
		}
		//task_name:work_start_time(since_cur_tick):work_duration
        const Vector<List<std::tuple<String,f32, f32>>> &TaskTimeRecordView() const { return _task_time_records; }
        const List<std::tuple<String, f32, f32>>& TaskTimeRecord(u16 thread_id) const
        {

            if (thread_id >= _task_time_records.size())
                return _task_time_records[0];
            return _task_time_records[thread_id];
        }
        template<typename Callable, typename... Args>
        auto Enqueue(const String& name,Callable &&task, Args &&...args) -> std::future<std::invoke_result_t<Callable, Args...>>
        {
            if (_b_stopping.load())
                throw std::runtime_error(std::format("Thread pool: {0} already been released!!", _pool_name));
            using return_type = typename std::invoke_result_t<Callable, Args...>;
            auto wrapper = make_shared<packaged_task<return_type()>>(bind(forward<Callable>(task), std::forward<Args>(args)...));
            std::future<return_type> ret = wrapper->get_future();
            auto new_task = Task{name, [=](){ (*wrapper)(); }};
            while(!_tasks.Enqueue(new_task))
                std::this_thread::yield();
            _task_cv.notify_one();
            return ret;
        }
		template<typename Callable, typename...Args>
		auto Enqueue(Callable&& task, Args&&... args) -> std::future<std::invoke_result_t<Callable, Args...>>
		{
            return Enqueue("no_name_task", std::forward<Callable>(task), std::forward<Args>(args)...);
		}
		void Release()
		{
			Stop();
			_threads.clear();
            _timers.clear();
            _thread_status.clear();
            _task_time_records.clear();
			s_total_thread_num -= _initial_thread_count;
			_initial_thread_count = 0;
		}
	private:
		void Start(size_t num_threads)
		{
			LOG_INFO("Thread pool {} is starting...", _pool_name);
			_timers.resize(num_threads, TimeMgr());
			_thread_status.resize(num_threads, EThreadStatus::kNotStarted);
            _task_time_records.resize(num_threads);
			for (auto i = 0u; i < num_threads; ++i)
			{
				std::wstring thread_name = std::format(L"Worker_{0}", _s_global_thread_id++);
				_threads.emplace_back([=, this]() {
					SetThreadDescription(GetCurrentThread(), thread_name.c_str());
					while (true)
					{
						Task task;
                        if (_tasks.Dequeue(task))
                        {
                            _thread_status[i] = EThreadStatus::kRunning;
                            task._task();
                            _thread_status[i] = EThreadStatus::kIdle;
                        }
                        else
                        {
                            std::unique_lock<std::mutex> lock(_wake_mutex);
                            _task_cv.wait(lock, [this] { return _b_stopping.load() || !_tasks.Empty(); });
                            if (_b_stopping.load() && _tasks.Empty()) {
                                break;
                            }
                        }
					}
					});
			}
		}
		void Stop() noexcept
		{
            _b_stopping.store(true);
			_task_cv.notify_all();
            for (auto& t : _threads)
            {
                if (t.joinable())
                    t.join();
            }
		}
	private:
		u8 _initial_thread_count = 0;
		inline static u8 _s_global_thread_id = 0u;
        inline static u8 s_total_thread_num = 0u;
		inline static bool s_is_inited = false;
        List<std::thread> _threads;
		std::condition_variable _task_cv;
		std::mutex _wake_mutex;
		std::atomic<bool> _b_stopping;
        LockFreeQueue<Task> _tasks;
        Vector<EThreadStatus::EThreadStatus> _thread_status;
		std::string _pool_name;
        Vector<TimeMgr> _timers;
		Vector<List<std::tuple<String,f32,f32>>> _task_time_records;
	};
	extern AILU_API Scope<ThreadPool> g_pThreadTool;
}


#endif // !THREAD_POOL_H__
