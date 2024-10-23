//
// Created by 22292 on 2024/10/11.
//
#include "Framework/Common/JobSystem.h"

namespace Ailu
{
    JobSystem::JobSystem(ThreadPool *thread_pool) : _thread_pool(thread_pool)
    {
    }

    JobSystem::~JobSystem()
    {
        for (auto job: _all_jobs)
            DESTORY_PTR(job);
    }
    void JobSystem::AddDependency(Job *job, Job *dependency_job)
    {
        std::unique_lock<std::mutex> lock(_dep_mutex);
        if (job != nullptr && dependency_job != nullptr)
        {
            job->_remaining_dependencies++;
            dependency_job->_dependents.push_back(job);
        }
    }
    void JobSystem::Dispatch(Job *job)
    {
        if (job->_remaining_dependencies == 0)
        {
            _thread_pool->Enqueue(job->_name, [this, job]()
                                  {
                                     job->_task();
                                     OnJobCompleted(job); });
        }
    }
    void JobSystem::Dispatch()
    {
        for (auto job: _all_jobs)
            Dispatch(job);
    }
    void JobSystem::OnJobCompleted(Job *job)
    {
        for (auto dependent: job->_dependents)
        {
            if (--dependent->_remaining_dependencies == 0)
                Dispatch(dependent);
        }
        if (job->_is_temp)
            RemoveJob(job);
    }

    void JobSystem::RemoveJob(Job *job)
    {
        std::unique_lock<std::mutex> lock(_job_mutex);
        auto it = std::find(_all_jobs.begin(), _all_jobs.end(), job);
        if (it != _all_jobs.end())
        {
            _all_jobs.erase(it);
            _temp_jobs_count--;
            //LOG_INFO("[JobSystem] Remove Job {},_temp_jobs_count={}", job->_name, _temp_jobs_count.load());
            if (_temp_jobs_count == 0)
                _temp_jobs_cv.notify_all();
        }
    }
    void JobSystem::Wait()
    {
        std::unique_lock<std::mutex> lock(_temp_jobs_mutex);
        _temp_jobs_cv.wait(lock, [this] { return _temp_jobs_count == 0; });
    }
}// namespace Ailu