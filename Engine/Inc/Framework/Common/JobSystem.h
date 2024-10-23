//
// Created by 22292 on 2024/10/11.
//

#ifndef AILU_JOBSYSTEM_H
#define AILU_JOBSYSTEM_H

#include "GlobalMarco.h"
#include "ThreadPool.h"
namespace Ailu
{
    struct Job
    {
        String _name;
        std::function<void()> _task;
        std::atomic<size_t> _remaining_dependencies;  // 记录该任务还有多少依赖任务未完成
        std::vector<Job*> _dependents;  // 依赖该任务的其他任务
        bool _is_temp;

        Job(String  name, std::function<void()> task,bool is_temp = true)
            : _name(std::move(name)), _task(std::move(task)), _remaining_dependencies(0) ,_is_temp(is_temp){}
    };
    class JobSystem
    {
    public:
        DISALLOW_COPY_AND_ASSIGN(JobSystem)
        explicit JobSystem(ThreadPool* thread_pool);
        ~JobSystem();
        template<typename Callable, typename... Args>
        Job* CreateJob(const String& name, bool is_temp,Callable&& task, Args&&... args)
        {
            auto job = new Job(name, std::bind(std::forward<Callable>(task), std::forward<Args>(args)...),is_temp);
            std::unique_lock<std::mutex> lock(_job_mutex);
            _all_jobs.push_back(job);
            if (job->_is_temp)
                _temp_jobs_count++;
            return job;
        }
        void AddDependency(Job* job, Job* dependency_job);
        void Dispatch(Job* job);
        void Dispatch();
        //Dispatch a temp job
        template<typename Callable, typename... Args>
        void Dispatch(Callable&& task, Args&&... args)
        {
            Dispatch(CreateJob("anonymity",true,task,std::forward<Args>(args)...));
        }
        //wait all temp job complete
        void Wait();
    private:
        void OnJobCompleted(Job* job);
        void RemoveJob(Job* job);
    private:
        std::mutex _job_mutex,_dep_mutex;
        ThreadPool* _thread_pool;
        Vector<Job*> _all_jobs;
        std::mutex _temp_jobs_mutex;
        std::condition_variable _temp_jobs_cv;
        std::atomic<size_t> _temp_jobs_count;
    };
    extern JobSystem* g_pJobSystem;
}
#endif//AILU_JOBSYSTEM_H
