#ifndef __MISC_H__
#define __MISC_H_

namespace Ailu
{
    namespace Core
    {
        class NonCopyble
        {
        public:
            NonCopyble() = default;
            NonCopyble(const NonCopyble&) = delete;
            NonCopyble& operator=(const NonCopyble&) = delete;
            virtual ~NonCopyble() = default;
        };
    }
}

#endif//__MISC_H_