#include "Framework/Common/TaskGroup.h"

#include "Framework/Common/Log.h"

namespace Ailu
{
    void TaskGroup::Close()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _closed = true;
    }

    void TaskGroup::Wait()
    {
        std::vector<std::future<void>> tasks;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            tasks.swap(_tasks);
        }

        for (std::future<void> &task : tasks)
        {
            try
            {
                task.get();
            }
            catch (const std::exception &exception)
            {
                LOG_ERROR("TaskGroup task failed during wait: {}", exception.what());
            }
            catch (...)
            {
                LOG_ERROR("TaskGroup task failed during wait with an unknown exception");
            }
        }
    }
}
