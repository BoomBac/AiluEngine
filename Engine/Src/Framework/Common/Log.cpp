#include "pch.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
    namespace Engine
    {
        int LogManager::Initialize()
        {
            int ret = 0;
            _appenders.push_back(new OutputAppender());
            return ret;
        }
        void LogManager::Finalize()
        {
            for (auto& logger : _appenders)
                delete logger;
        }
        void LogManager::Tick()
        {

        }
        void LogManager::AddAppender(IAppender* appender)
        {
            _appenders.push_back(appender);
        }
    }
}
