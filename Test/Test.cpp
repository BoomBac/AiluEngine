// Test.cpp

#include "Test.h"

#include <Framework/Common/Allocator.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Ailu;

namespace
{
    struct TestResult
    {
        u32 _passed = 0u;
        u32 _failed = 0u;
    };

    struct AllocationRecord
    {
        u8 *_ptr = nullptr;
        u64 _size = 0u;
        u8 _pattern = 0u;
    };

    struct LifecycleObject
    {
        LifecycleObject(i32 value, std::string name) : _value(value), _name(std::move(name))
        {
            ++s_constructor_count;
        }

        ~LifecycleObject()
        {
            ++s_destructor_count;
        }

        i32 _value = 0;
        std::string _name;

        inline static std::atomic<u32> s_constructor_count = 0u;
        inline static std::atomic<u32> s_destructor_count = 0u;
    };

    struct alignas(16) Aligned16
    {
        std::array<u8, 16u> _data{};
    };

    struct alignas(32) Aligned32
    {
        std::array<u8, 32u> _data{};
    };

    struct alignas(64) Aligned64
    {
        std::array<u8, 64u> _data{};
    };

    void PrintTestResult(const char *name, bool passed)
    {
        std::cout << "[" << (passed ? "PASS" : "FAIL") << "] " << name << '\n';
    }

    template<typename TestFunction>
    void RunTest(TestResult &result, const char *name, TestFunction &&test_function)
    {
        bool passed = false;

        try
        {
            passed = test_function();
        }
        catch (const std::exception &exception)
        {
            std::cerr << "Exception: " << exception.what() << '\n';
            passed = false;
        }
        catch (...)
        {
            std::cerr << "Unknown exception\n";
            passed = false;
        }

        PrintTestResult(name, passed);

        if (passed)
            ++result._passed;
        else
            ++result._failed;
    }

    bool IsAligned(const void *ptr, u64 alignment)
    {
        return reinterpret_cast<uintptr_t>(ptr) % alignment == 0u;
    }

    void FillPattern(void *ptr, u64 size, u8 pattern)
    {
        std::memset(ptr, pattern, static_cast<size_t>(size));
    }

    bool CheckPattern(const void *ptr, u64 size, u8 pattern)
    {
        const auto *data = static_cast<const u8 *>(ptr);

        for (u64 i = 0u; i < size; ++i)
        {
            if (data[i] != pattern)
                return false;
        }

        return true;
    }

    bool TestBasicAllocation()
    {
        constexpr u64 kSize = 128u;
        constexpr u8 kPattern = 0x5au;

        u8 *ptr = AL_ALLOC(u8, kSize);
        if (ptr == nullptr)
            return false;

        FillPattern(ptr, kSize, kPattern);

        if (!CheckPattern(ptr, kSize, kPattern))
        {
            AL_FREE(ptr);
            return false;
        }

        AL_FREE(ptr);
        return ptr == nullptr;
    }

    bool TestObjectLifecycle()
    {
        LifecycleObject::s_constructor_count.store(0u);
        LifecycleObject::s_destructor_count.store(0u);

        LifecycleObject *object = AL_NEW(LifecycleObject, 42, "allocator_test");
        if (object == nullptr)
            return false;

        if (object->_value != 42 || object->_name != "allocator_test")
        {
            AL_DELETE(object);
            return false;
        }

        if (LifecycleObject::s_constructor_count.load() != 1u)
        {
            AL_DELETE(object);
            return false;
        }

        AL_DELETE(object);

        return object == nullptr && LifecycleObject::s_destructor_count.load() == 1u;
    }

    template<typename T>
    bool TestTypeAlignment()
    {
        T *ptr = AL_NEW(T);
        if (ptr == nullptr)
            return false;

        const bool aligned = IsAligned(ptr, alignof(T));
        AL_DELETE(ptr);

        return aligned && ptr == nullptr;
    }

    bool TestExplicitAlignments()
    {
        constexpr std::array<u64, 6u> kAlignments = {
            8u,
            16u,
            32u,
            64u,
            128u,
            256u,
        };

        for (u64 alignment : kAlignments)
        {
            u8 *ptr = AL_ALIGN_ALLOC(u8, 257u, alignment);
            if (ptr == nullptr)
                return false;

            const bool aligned = IsAligned(ptr, alignment);
            FillPattern(ptr, 257u, static_cast<u8>(alignment));

            if (!aligned || !CheckPattern(ptr, 257u, static_cast<u8>(alignment)))
            {
                AL_FREE(ptr);
                return false;
            }

            AL_FREE(ptr);

            if (ptr != nullptr)
                return false;
        }

        return true;
    }

    bool TestBoundarySizes()
    {
        // 覆盖常见 Bin 分界和小对象/系统分配边界。
        constexpr std::array<u64, 43u> kSizes = {
            1u,    2u,    3u,    7u,    8u,    9u,    15u,   16u,   17u,   31u,   32u,
            33u,   63u,   64u,   65u,   127u,  128u,  129u,  159u,  160u,  161u,  191u,
            192u,  193u,  255u,  256u,  257u,  511u,  512u,  513u,  575u,  576u,  577u,
            1023u, 1024u, 1025u, 2047u, 2048u, 2049u, 4087u, 4095u, 4096u, 4097u,
        };

        for (u64 size : kSizes)
        {
            u8 *ptr = AL_ALLOC(u8, size);
            if (ptr == nullptr)
                return false;

            const u8 pattern = static_cast<u8>((size * 31u) & 0xffu);
            FillPattern(ptr, size, pattern);

            if (!CheckPattern(ptr, size, pattern))
            {
                AL_FREE(ptr);
                return false;
            }

            AL_FREE(ptr);

            if (ptr != nullptr)
                return false;
        }

        return true;
    }

    bool TestMultipleLiveAllocations()
    {
        constexpr u32 kAllocationCount = 5000u;

        std::vector<AllocationRecord> records;
        records.reserve(kAllocationCount);

        for (u32 i = 0u; i < kAllocationCount; ++i)
        {
            const u64 size = 1u + i % 2048u;
            const u8 pattern = static_cast<u8>((i * 17u) & 0xffu);

            u8 *ptr = AL_ALLOC(u8, size);
            if (ptr == nullptr)
                return false;

            FillPattern(ptr, size, pattern);
            records.push_back({ptr, size, pattern});
        }

        for (const AllocationRecord &record : records)
        {
            if (!CheckPattern(record._ptr, record._size, record._pattern))
            {
                for (AllocationRecord &cleanup_record : records)
                    AL_FREE(cleanup_record._ptr);

                return false;
            }
        }

        for (AllocationRecord &record : records)
            AL_FREE(record._ptr);

        for (const AllocationRecord &record : records)
        {
            if (record._ptr != nullptr)
                return false;
        }

        return true;
    }

    bool TestPageExpansionAndReuse()
    {
        // 64 字节对象一页大约可容纳 1000 个左右。
        // 12000 个对象足以迫使 Bin 创建多页，并在全部释放时触发空 Page 裁剪。
        constexpr u32 kAllocationCount = 12000u;
        constexpr u64 kAllocationSize = 64u;

        std::vector<u8 *> pointers;
        pointers.reserve(kAllocationCount);

        for (u32 round = 0u; round < 4u; ++round)
        {
            for (u32 i = 0u; i < kAllocationCount; ++i)
            {
                u8 *ptr = AL_ALLOC(u8, kAllocationSize);
                if (ptr == nullptr)
                    return false;

                const u8 pattern = static_cast<u8>((round * 53u + i) & 0xffu);
                FillPattern(ptr, kAllocationSize, pattern);
                pointers.push_back(ptr);
            }

            for (u32 i = 0u; i < kAllocationCount; ++i)
            {
                const u8 pattern = static_cast<u8>((round * 53u + i) & 0xffu);
                if (!CheckPattern(pointers[i], kAllocationSize, pattern))
                    return false;
            }

            // 采用乱序释放，避免只验证简单的 LIFO free-list 路径。
            std::mt19937 random_engine(0x12345678u + round);
            std::shuffle(pointers.begin(), pointers.end(), random_engine);

            for (u8 *&ptr : pointers)
                AL_FREE(ptr);

            pointers.clear();
        }

        return true;
    }

    bool TestRandomStress()
    {
        constexpr u32 kOperationCount = 300000u;
        constexpr u32 kMaxLiveAllocations = 5000u;

        std::mt19937_64 random_engine(0xbadc0ffeeull);
        std::uniform_int_distribution<u64> size_distribution(1u, 8192u);
        std::uniform_int_distribution<u32> operation_distribution(0u, 99u);
        std::uniform_int_distribution<u32> pattern_distribution(0u, 255u);

        std::vector<AllocationRecord> records;
        records.reserve(kMaxLiveAllocations);

        for (u32 operation_index = 0u; operation_index < kOperationCount; ++operation_index)
        {
            const bool should_allocate =
                records.empty() ||
                (records.size() < kMaxLiveAllocations && operation_distribution(random_engine) < 60u);

            if (should_allocate)
            {
                const u64 size = size_distribution(random_engine);
                const u8 pattern = static_cast<u8>(pattern_distribution(random_engine));

                u8 *ptr = AL_ALLOC(u8, size);
                if (ptr == nullptr)
                    return false;

                FillPattern(ptr, size, pattern);
                records.push_back({ptr, size, pattern});
            }
            else
            {
                std::uniform_int_distribution<size_t> index_distribution(0u, records.size() - 1u);
                const size_t index = index_distribution(random_engine);
                AllocationRecord &record = records[index];

                if (!CheckPattern(record._ptr, record._size, record._pattern))
                    return false;

                AL_FREE(record._ptr);

                records[index] = records.back();
                records.pop_back();
            }

            if ((operation_index % 4096u) == 0u)
            {
                for (const AllocationRecord &record : records)
                {
                    if (!CheckPattern(record._ptr, record._size, record._pattern))
                        return false;
                }
            }
        }

        for (AllocationRecord &record : records)
        {
            if (!CheckPattern(record._ptr, record._size, record._pattern))
                return false;

            AL_FREE(record._ptr);
        }

        return true;
    }

    bool TestMultiThreadAllocation()
    {
        constexpr u32 kThreadCount = 8u;
        constexpr u32 kIterationCount = 50000u;

        std::atomic<bool> succeeded = true;
        std::vector<std::thread> threads;
        threads.reserve(kThreadCount);

        for (u32 thread_index = 0u; thread_index < kThreadCount; ++thread_index)
        {
            threads.emplace_back(
                [thread_index, &succeeded]()
                {
                    std::mt19937 random_engine(0x1000u + thread_index);
                    std::uniform_int_distribution<u32> size_distribution(1u, 4096u);

                    for (u32 i = 0u; i < kIterationCount && succeeded.load(); ++i)
                    {
                        const u64 size = size_distribution(random_engine);
                        const u8 pattern = static_cast<u8>((thread_index * 31u + i) & 0xffu);

                        u8 *ptr = AL_ALLOC(u8, size);
                        if (ptr == nullptr)
                        {
                            succeeded.store(false);
                            return;
                        }

                        FillPattern(ptr, size, pattern);

                        if (!CheckPattern(ptr, size, pattern))
                        {
                            succeeded.store(false);
                            AL_FREE(ptr);
                            return;
                        }

                        AL_FREE(ptr);

                        if (ptr != nullptr)
                        {
                            succeeded.store(false);
                            return;
                        }
                    }
                });
        }

        for (std::thread &thread : threads)
            thread.join();

        return succeeded.load();
    }

    bool TestCrossThreadFree()
    {
        constexpr u32 kAllocationCount = 20000u;
        constexpr u64 kAllocationSize = 96u;

        std::vector<u8 *> pointers;
        pointers.reserve(kAllocationCount);

        for (u32 i = 0u; i < kAllocationCount; ++i)
        {
            u8 *ptr = AL_ALLOC(u8, kAllocationSize);
            if (ptr == nullptr)
                return false;

            FillPattern(ptr, kAllocationSize, static_cast<u8>(i & 0xffu));
            pointers.push_back(ptr);
        }

        std::atomic<bool> succeeded = true;
        std::thread free_thread(
            [&pointers, &succeeded]()
            {
                for (u32 i = 0u; i < pointers.size(); ++i)
                {
                    u8 *&ptr = pointers[i];

                    if (!CheckPattern(ptr, kAllocationSize, static_cast<u8>(i & 0xffu)))
                    {
                        succeeded.store(false);
                        return;
                    }

                    AL_FREE(ptr);

                    if (ptr != nullptr)
                    {
                        succeeded.store(false);
                        return;
                    }
                }
            });

        free_thread.join();

        if (!succeeded.load())
            return false;

        // 再次从主线程申请，验证跨线程归还的 Block 和 Page 状态可继续使用。
        for (u32 i = 0u; i < kAllocationCount; ++i)
        {
            u8 *ptr = AL_ALLOC(u8, kAllocationSize);
            if (ptr == nullptr)
                return false;

            FillPattern(ptr, kAllocationSize, 0xa5u);

            if (!CheckPattern(ptr, kAllocationSize, 0xa5u))
            {
                AL_FREE(ptr);
                return false;
            }

            AL_FREE(ptr);
        }

        return true;
    }

    bool TestAllocatedByteCount()
    {
        const u64 before = Allocator::Get().TotalAllocated();

        u8 *small_ptr = AL_ALLOC(u8, 256u);
        u8 *large_ptr = AL_ALLOC(u8, 8192u);

        if (small_ptr == nullptr || large_ptr == nullptr)
        {
            AL_FREE(small_ptr);
            AL_FREE(large_ptr);
            return false;
        }

        const u64 during = Allocator::Get().TotalAllocated();
        if (during < before + 256u + 8192u)
        {
            AL_FREE(small_ptr);
            AL_FREE(large_ptr);
            return false;
        }

        AL_FREE(small_ptr);
        AL_FREE(large_ptr);

        return Allocator::Get().TotalAllocated() == before;
    }

    void RunAllocatorTests()
    {
        TestResult result;

        Allocator::Init();

        RunTest(result, "Basic allocation", TestBasicAllocation);
        RunTest(result, "Object constructor/destructor", TestObjectLifecycle);
        RunTest(result, "alignas(16) object", TestTypeAlignment<Aligned16>);
        RunTest(result, "alignas(32) object", TestTypeAlignment<Aligned32>);
        RunTest(result, "alignas(64) object", TestTypeAlignment<Aligned64>);
        RunTest(result, "Explicit alignments", TestExplicitAlignments);
        RunTest(result, "Boundary sizes", TestBoundarySizes);
        RunTest(result, "Multiple live allocations", TestMultipleLiveAllocations);
        RunTest(result, "Page expansion and reuse", TestPageExpansionAndReuse);
        RunTest(result, "Random stress", TestRandomStress);
        RunTest(result, "Multi-thread allocation", TestMultiThreadAllocation);
        RunTest(result, "Cross-thread free", TestCrossThreadFree);
        RunTest(result, "Allocated byte count", TestAllocatedByteCount);

        std::cout << "\nAllocator page state:\n";
        std::cout << Allocator::Get().GetPageMgr().Dump() << '\n';

        std::cout << "========================================\n";
        std::cout << "Allocator tests passed: " << result._passed << '\n';
        std::cout << "Allocator tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";

        Allocator::Shutdown();

        if (result._failed != 0u)
            std::exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    RunAllocatorTests();
    return EXIT_SUCCESS;
}