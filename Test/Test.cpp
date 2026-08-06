// Test.cpp

#include "Test.h"

#include <Framework/Common/Allocator.hpp>
#include <Framework/Common/Log.h>
#include <Framework/Common/TimeMgr.h>
#include <Framework/Math/Guid.h>
#include <Assets/AssetDocument.h>
#include <Graph/GraphDocument.h>
#include <Input/InputSystem.h>
#include <Objects/JsonArchive.h>
#include <Objects/Type.h>
#include <Render/2D/SpriteBatcher.h>
#include <Scene/EntityReference.h>

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

namespace Ailu::Editor::AutomationCoreTests
{
    bool TestAutomationValuePrimitives();
    bool TestAutomationValueArrayObject();
    bool TestAutomationValueEquality();
    bool TestAutomationRegistry();
    bool TestAutomationServiceSubmitTick();
}

namespace Ailu::Editor::AutomationReadModelTests
{
    bool TestTypeNameNormalization();
    bool TestValueConversions();
    bool TestComponentStableName();
    bool TestComponentDescriptorRead();
}

namespace Ailu::Editor::AutomationAdapterTests
{
    bool TestAdapterResolve();
    bool TestAdapterDescribeComponent();
    bool TestAdapterReadComponent();
}

namespace Ailu::Editor::AutomationTransportTests
{
    bool TestJsonRoundtrip();
    bool TestRequestResultJson();
    bool TestSessionFile();
    bool TestPipeRoundTrip();
}

namespace Ailu::Editor::AutomationCommandTests
{
    bool TestCommandManagerLifecycle();
    bool TestCommandEventNotification();
    bool TestSetPropertyCommandValidation();
    bool TestAdapterWriteComponent();
    bool TestValueConversions();
    bool TestPermissionGate();
}

namespace Ailu::Editor::AIAssistantTests
{
    bool TestAILoopReadAndPreview();
    bool TestAIReject();
    bool TestAIImmediateFinishForReadOnly();
}

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

    class FakeKeyboardDevice final : public InputDevice
    {
    public:
        FakeKeyboardDevice()
        {
            _device_id = 1u;
            _device_name = "Keyboard";

            InputControlDesc space_desc;
            space_desc._name = "space";
            space_desc._value_type = EInputValueType::kButton;
            space_desc._index = 0u;
            _control_descs.push_back(space_desc);
        }

        void SetSpacePressed(bool pressed) { _space_pressed = pressed; }
        void Poll() override {}
        EInputDeviceType GetDeviceType() const override { return EInputDeviceType::kKeyboard; }
        bool IsConnected() const override { return true; }

        InputValue ReadControl(u16 control_index) const override
        {
            if (control_index == 0u)
                return InputValue::MakeButton(_space_pressed);
            return InputValue{};
        }

    private:
        bool _space_pressed = false;
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
        std::cout << std::flush;

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

    bool TestSpriteBatchOffsetsForMultiTextureAndMaterial()
    {
        using namespace Ailu::Render;

        Vector<SpriteRenderData> render_data(5u);
        auto *material_a = reinterpret_cast<Material *>(static_cast<uintptr_t>(0x1000u));
        auto *material_b = reinterpret_cast<Material *>(static_cast<uintptr_t>(0x2000u));
        auto *texture_a = reinterpret_cast<Texture *>(static_cast<uintptr_t>(0x3000u));
        auto *texture_b = reinterpret_cast<Texture *>(static_cast<uintptr_t>(0x4000u));

        render_data[0]._material = material_a;
        render_data[0]._texture = texture_a;
        render_data[1]._material = material_b;
        render_data[1]._texture = texture_a;
        render_data[2]._material = material_b;
        render_data[2]._texture = texture_a;
        render_data[3]._material = material_b;
        render_data[3]._texture = texture_b;
        render_data[4]._material = material_a;
        render_data[4]._texture = texture_a;

        const auto batches = SpriteBatcher::BuildBatchesForTesting(render_data);
        if (batches.size() != 4u)
            return false;

        return batches[0]._instance_offset == 0u && batches[0]._instance_count == 1u &&
               batches[1]._instance_offset == 1u && batches[1]._instance_count == 2u &&
               batches[2]._instance_offset == 3u && batches[2]._instance_count == 1u &&
               batches[3]._instance_offset == 4u && batches[3]._instance_count == 1u;
    }

    bool TestSpriteBatchOffsetsRemainSplitAfterSorting()
    {
        using namespace Ailu::Render;

        Vector<SpriteRenderData> render_data(4u);
        auto *material = reinterpret_cast<Material *>(static_cast<uintptr_t>(0x5000u));
        auto *texture_a = reinterpret_cast<Texture *>(static_cast<uintptr_t>(0x6000u));
        auto *texture_b = reinterpret_cast<Texture *>(static_cast<uintptr_t>(0x7000u));

        render_data[0]._material = material;
        render_data[0]._texture = texture_a;
        render_data[1]._material = material;
        render_data[1]._texture = texture_a;
        render_data[2]._material = material;
        render_data[2]._texture = texture_b;
        render_data[3]._material = material;
        render_data[3]._texture = texture_b;

        const auto batches = SpriteBatcher::BuildBatchesForTesting(render_data);
        if (batches.size() != 2u)
            return false;

        return batches[0]._instance_offset == 0u && batches[0]._instance_count == 2u &&
               batches[1]._instance_offset == 2u && batches[1]._instance_count == 2u;
    }

    bool TestInputSystemButtonAction()
    {
        InputActionAsset asset("TestInputActions");

        InputAction jump_action("Jump");
        jump_action.SetId(1001u);
        jump_action.SetActionType(EInputActionType::kButton);
        jump_action.SetValueType(EInputValueType::kButton);

        InputBinding jump_binding;
        jump_binding._name = "KeyboardSpace";
        jump_binding._control_path = "<Keyboard>/space";
        jump_action.AddBinding(std::move(jump_binding));

        InputActionMap gameplay_map("Gameplay");
        gameplay_map.SetId(101u);
        gameplay_map.AddAction(std::move(jump_action));
        asset.AddActionMap(std::move(gameplay_map));

        InputContext gameplay_context("Gameplay");
        gameplay_context.SetPriority(10);
        gameplay_context.SetConsumeInput(true);
        gameplay_context.AddActionMap(MakeRef<InputActionMap>("Gameplay"));
        asset.AddContext(std::move(gameplay_context));

        InputSystem input_system;
        auto keyboard = MakeScope<FakeKeyboardDevice>();
        FakeKeyboardDevice *keyboard_ptr = keyboard.get();
        input_system.RegisterDevice(std::move(keyboard));
        input_system.LoadAsset(asset);
        input_system.PushContext("Gameplay");

        if (input_system.FindContext("Gameplay") == nullptr)
            return false;
        if (input_system.FindActionById(1001u) == nullptr)
            return false;

        u32 performed_count = 0u;
        input_system.AddActionEventListener(
            [&performed_count](const InputActionEvent &event)
            {
                if (event._action != nullptr && event._action->GetName() == "Jump" &&
                    event._phase == EInputActionPhase::kPerformed)
                {
                    ++performed_count;
                }
            });

        keyboard_ptr->SetSpacePressed(false);
        input_system.Update(1.0f / 60.0f);
        InputAction *jump = input_system.FindAction("Jump");
        if (jump == nullptr || jump->WasPerformedThisFrame())
            return false;

        keyboard_ptr->SetSpacePressed(true);
        input_system.Update(1.0f / 60.0f);
        if (!jump->WasPerformedThisFrame() || !jump->GetValue().AsButton())
            return false;

        input_system.Update(1.0f / 60.0f);
        if (jump->WasPerformedThisFrame())
            return false;

        keyboard_ptr->SetSpacePressed(false);
        input_system.Update(1.0f / 60.0f);
        if (jump->WasPerformedThisFrame())
            return false;

        return performed_count == 1u;
    }

    bool TestGraphDocumentAddLinkAndReplaceInput()
    {
        GraphAsset asset("GraphDocumentTest");
        GraphDocument document;
        if (!document.Open(&asset))
            return false;

        const Guid entry_id = document.AddNode("Flow.Entry", {0.0f, 0.0f});
        const Guid branch_id = document.AddNode("Flow.Branch", {240.0f, 0.0f});
        const Guid print_id = document.AddNode("Flow.Print", {480.0f, 0.0f});
        const Guid sequence_id = document.AddNode("Flow.Sequence", {-240.0f, 0.0f});
        if (entry_id == Guid::EmptyGuid() || branch_id == Guid::EmptyGuid() || print_id == Guid::EmptyGuid() ||
            sequence_id == Guid::EmptyGuid())
        {
            return false;
        }

        const GraphNodeData *entry = document.FindNode(entry_id);
        const GraphNodeData *branch = document.FindNode(branch_id);
        const GraphNodeData *print = document.FindNode(print_id);
        const GraphNodeData *sequence = document.FindNode(sequence_id);
        if (entry == nullptr || branch == nullptr || print == nullptr || sequence == nullptr)
            return false;

        const Guid entry_then = entry->_pins[0]._id;
        const Guid branch_exec = branch->_pins[0]._id;
        const Guid branch_true = branch->_pins[2]._id;
        const Guid print_exec = print->_pins[0]._id;
        const Guid sequence_then = sequence->_pins[1]._id;

        const Guid entry_to_branch = document.AddLink(entry_then, branch_exec);
        const Guid branch_to_print = document.AddLink(branch_true, print_exec);
        if (entry_to_branch == Guid::EmptyGuid() || branch_to_print == Guid::EmptyGuid() ||
            document.Links().size() != 2u)
            return false;

        const GraphConnectionResponse replace_response = document.CanConnect(sequence_then, branch_exec);
        if (replace_response._action != EGraphConnectionAction::kReplaceInput)
            return false;

        const Guid sequence_to_branch = document.AddLink(sequence_then, branch_exec);
        if (sequence_to_branch == Guid::EmptyGuid() || document.Links().size() != 2u)
            return false;

        for (const GraphLinkData &link : document.Links())
        {
            if (link._id == entry_to_branch)
                return false;
            if (link._input_pin == branch_exec && link._output_pin != sequence_then)
                return false;
        }
        return document.IsDirty();
    }

    bool TestGraphDocumentRejectsInvalidConnection()
    {
        GraphAsset asset("GraphInvalidConnectionTest");
        GraphDocument document;
        document.Open(&asset);

        const Guid branch_id = document.AddNode("Flow.Branch", {0.0f, 0.0f});
        const Guid literal_id = document.AddNode("Literal.Float", {240.0f, 0.0f});
        const GraphNodeData *branch = document.FindNode(branch_id);
        const GraphNodeData *literal = document.FindNode(literal_id);
        if (branch == nullptr || literal == nullptr)
            return false;

        const GraphPinData &condition = branch->_pins[1];
        const GraphPinData &float_value = literal->_pins[0];
        const GraphConnectionResponse response = document.CanConnect(float_value._id, condition._id);
        if (response._action != EGraphConnectionAction::kDisallow)
            return false;
        return document.AddLink(float_value._id, condition._id) == Guid::EmptyGuid() && document.Links().empty();
    }

    bool TestGraphDocumentRemoveNodeCleansLinks()
    {
        GraphAsset asset("GraphRemoveNodeTest");
        GraphDocument document;
        document.Open(&asset);

        const Guid entry_id = document.AddNode("Flow.Entry", {0.0f, 0.0f});
        const Guid branch_id = document.AddNode("Flow.Branch", {240.0f, 0.0f});
        const GraphNodeData *entry = document.FindNode(entry_id);
        const GraphNodeData *branch = document.FindNode(branch_id);
        if (entry == nullptr || branch == nullptr)
            return false;

        const Guid link_id = document.AddLink(entry->_pins[0]._id, branch->_pins[0]._id);
        if (link_id == Guid::EmptyGuid() || document.Links().size() != 1u)
            return false;

        const std::array<Guid, 1u> remove_ids = {branch_id};
        if (!document.RemoveNodes(remove_ids))
            return false;
        return document.FindNode(branch_id) == nullptr && document.Links().empty();
    }

    bool TestGraphDocumentRemoveLinks()
    {
        GraphAsset asset("GraphRemoveLinkTest");
        GraphDocument document;
        document.Open(&asset);

        const Guid entry_id = document.AddNode("Flow.Entry", {0.0f, 0.0f});
        const Guid print_id = document.AddNode("Flow.Print", {240.0f, 0.0f});
        const GraphNodeData *entry = document.FindNode(entry_id);
        const GraphNodeData *print = document.FindNode(print_id);
        if (entry == nullptr || print == nullptr)
            return false;

        const Guid link_id = document.AddLink(entry->_pins[0]._id, print->_pins[0]._id);
        if (link_id == Guid::EmptyGuid() || document.FindLink(link_id) == nullptr)
            return false;

        const std::array<Guid, 1u> remove_ids = {link_id};
        return document.RemoveLinks(remove_ids) && document.FindLink(link_id) == nullptr && document.Links().empty();
    }

    bool TestGraphDocumentSetNodePositions()
    {
        GraphAsset asset("GraphMoveNodeTest");
        GraphDocument document;
        document.Open(&asset);

        const Guid entry_id = document.AddNode("Flow.Entry", {0.0f, 0.0f});
        const Guid print_id = document.AddNode("Flow.Print", {240.0f, 0.0f});
        const std::array<GraphNodePosition, 2u> positions = {{{entry_id, {16.0f, 32.0f}},
                                                              {print_id, {304.0f, 48.0f}}}};
        if (!document.SetNodePositions(positions))
            return false;

        const GraphNodeData *entry = document.FindNode(entry_id);
        const GraphNodeData *print = document.FindNode(print_id);
        if (entry == nullptr || print == nullptr)
            return false;
        return entry->_position == Vector2f(16.0f, 32.0f) && print->_position == Vector2f(304.0f, 48.0f) &&
               document.IsDirty();
    }

    bool TestGraphCommandStackUndoRedo()
    {
        GraphAsset asset("GraphCommandStackTest");
        GraphDocument document;
        document.Open(&asset);

        auto add_entry = MakeScope<AddGraphNodeCommand>("Flow.Entry", Vector2f(0.0f, 0.0f));
        AddGraphNodeCommand *add_entry_ptr = add_entry.get();
        if (!document.Commands().Execute(std::move(add_entry)))
            return false;
        const Guid entry_id = add_entry_ptr->NodeId();
        if (entry_id == Guid::EmptyGuid() || document.FindNode(entry_id) == nullptr || !document.Commands().CanUndo())
            return false;

        document.Commands().Undo();
        if (document.FindNode(entry_id) != nullptr || !document.Commands().CanRedo())
            return false;
        document.Commands().Redo();
        if (document.FindNode(entry_id) == nullptr)
            return false;

        auto add_print = MakeScope<AddGraphNodeCommand>("Flow.Print", Vector2f(240.0f, 0.0f));
        AddGraphNodeCommand *add_print_ptr = add_print.get();
        if (!document.Commands().Execute(std::move(add_print)))
            return false;
        const Guid print_id = add_print_ptr->NodeId();
        const GraphNodeData *entry = document.FindNode(entry_id);
        const GraphNodeData *print = document.FindNode(print_id);
        if (entry == nullptr || print == nullptr)
            return false;

        if (!document.Commands().Execute(MakeScope<AddGraphLinkCommand>(entry->_pins[0]._id, print->_pins[0]._id)) ||
            document.Links().size() != 1u)
        {
            return false;
        }
        document.Commands().Undo();
        if (!document.Links().empty())
            return false;
        document.Commands().Redo();
        if (document.Links().size() != 1u)
            return false;

        Vector<GraphNodePosition> start_positions = {{entry_id, {0.0f, 0.0f}}};
        Vector<GraphNodePosition> target_positions = {{entry_id, {64.0f, 32.0f}}};
        if (!document.Commands().Execute(MakeScope<MoveGraphNodesCommand>(start_positions, target_positions)))
            return false;
        if (document.FindNode(entry_id)->_position != Vector2f(64.0f, 32.0f))
            return false;
        document.Commands().Undo();
        if (document.FindNode(entry_id)->_position != Vector2f(0.0f, 0.0f))
            return false;
        document.Commands().Redo();
        if (document.FindNode(entry_id)->_position != Vector2f(64.0f, 32.0f))
            return false;

        Vector<Guid> remove_nodes = {print_id};
        if (!document.Commands().Execute(MakeScope<RemoveGraphNodesCommand>(remove_nodes)))
            return false;
        if (document.FindNode(print_id) != nullptr || !document.Links().empty())
            return false;
        document.Commands().Undo();
        return document.FindNode(print_id) != nullptr && document.Links().size() == 1u;
    }

    bool TestGraphPasteCommandRemapsGuids()
    {
        GraphAsset asset("GraphPasteCommandTest");
        GraphDocument document;
        document.Open(&asset);

        const Guid entry_id = document.AddNode("Flow.Entry", {0.0f, 0.0f});
        const Guid print_id = document.AddNode("Flow.Print", {240.0f, 0.0f});
        const GraphNodeData *entry = document.FindNode(entry_id);
        const GraphNodeData *print = document.FindNode(print_id);
        if (entry == nullptr || print == nullptr)
            return false;

        GraphNodeData pasted_entry = *entry;
        GraphNodeData pasted_print = *print;
        const Guid old_entry_pin = pasted_entry._pins[0]._id;
        const Guid old_print_pin = pasted_print._pins[0]._id;
        pasted_entry._id = Guid::Generate();
        pasted_print._id = Guid::Generate();
        for (GraphPinData &pin : pasted_entry._pins)
            pin._id = Guid::Generate();
        for (GraphPinData &pin : pasted_print._pins)
            pin._id = Guid::Generate();
        pasted_entry._position += Vector2f(32.0f, 32.0f);
        pasted_print._position += Vector2f(32.0f, 32.0f);

        GraphLinkData pasted_link;
        pasted_link._id = Guid::Generate();
        pasted_link._output_pin = pasted_entry._pins[0]._id;
        pasted_link._input_pin = pasted_print._pins[0]._id;
        Vector<GraphNodeData> pasted_nodes = {pasted_entry, pasted_print};
        Vector<GraphLinkData> pasted_links = {pasted_link};

        auto paste_command = MakeScope<PasteGraphElementsCommand>(pasted_nodes, pasted_links);
        PasteGraphElementsCommand *paste_command_ptr = paste_command.get();
        if (!document.Commands().Execute(std::move(paste_command)))
            return false;
        if (paste_command_ptr->PastedNodeIds().size() != 2u || paste_command_ptr->PastedLinkIds().size() != 1u)
            return false;
        if (document.FindNode(pasted_entry._id) == nullptr || document.FindNode(pasted_print._id) == nullptr)
            return false;

        const GraphLinkData *link = document.FindLink(pasted_link._id);
        if (link == nullptr || link->_output_pin == old_entry_pin || link->_input_pin == old_print_pin)
            return false;
        if (!(link->_output_pin == pasted_entry._pins[0]._id) || !(link->_input_pin == pasted_print._pins[0]._id))
            return false;

        document.Commands().Undo();
        if (document.FindNode(pasted_entry._id) != nullptr || document.FindLink(pasted_link._id) != nullptr)
            return false;
        document.Commands().Redo();
        return document.FindNode(pasted_entry._id) != nullptr && document.FindLink(pasted_link._id) != nullptr;
    }

    bool TestGraphValidationFindsDuplicateIds()
    {
        GraphAsset asset("GraphValidationTest");
        GraphNodeData first;
        GraphNodeData second;
        first._id = Guid::Generate();
        second._id = first._id;
        first._node_type = "Flow.Entry";
        second._node_type = "Flow.Entry";
        first._flags = GraphFlag(EGraphNodeFlag::kEntryNode);
        second._flags = GraphFlag(EGraphNodeFlag::kEntryNode);
        asset.MutableNodes().push_back(first);
        asset.MutableNodes().push_back(second);

        GraphDocument document;
        document.Open(&asset);
        bool found_duplicate_node = false;
        for (const GraphValidationMessage &message : document.ValidationMessages())
        {
            if (message._severity == EGraphValidationSeverity::kError && message._message == "Duplicate node id.")
                found_duplicate_node = true;
        }
        return found_duplicate_node;
    }

    // =========================================================================
    // Entity GUID 基础单元测试（无 GPU 依赖，headless 可运行）
    // 覆盖任务包 1：Guid 有效性判断与通用哈希器。
    // =========================================================================
    bool TestGuidIsEmptyIsValid()
    {
        // 空字符串与 "null"（含空白修饰）都应被识别为空 GUID。
        if (!Guid::EmptyGuid().IsEmpty()) return false;
        if (Guid::EmptyGuid().IsValid()) return false;
        if (!Guid("").IsEmpty()) return false;
        if (!Guid("null").IsEmpty()) return false;
        if (!Guid("  null  ").IsEmpty()) return false;
        if (Guid("").IsValid()) return false;

        // 生成 GUID 非空且有效。
        Guid g = Guid::Generate();
        if (g.IsEmpty()) return false;
        if (!g.IsValid()) return false;
        if (g == Guid::EmptyGuid()) return false;

        // 不同生成的 GUID 不应相等。
        Guid g2 = Guid::Generate();
        if (g == g2) return false;
        return true;
    }

    bool TestGuidHasher()
    {
        GuidHasher hasher;
        Guid a = Guid::Generate();
        // 同一 GUID 哈希稳定。
        if (hasher(a) != hasher(a)) return false;
        // 从同一字符串构造的 GUID 哈希一致。
        Guid b(a.ToString());
        if (hasher(a) != hasher(b)) return false;
        // 空 GUID 也应有稳定哈希（不抛异常）。
        GuidHasher empty_hasher;
        (void)empty_hasher(Guid::EmptyGuid());
        return true;
    }

    // =========================================================================
    // SceneAssetDocument 文档级测试（无需 GPU）。
    // 覆盖任务包 4：V2 场景文档自定义序列化往返 + V1 旧格式兼容反序列化。
    // =========================================================================
    void SerializeDocumentToJson(JsonArchive &ar, SceneAssetDocument &doc)
    {
        // 与修复后的 SaveAssetDocument 一致：优先使用文档类自定义 Serialize。
        doc.Serialize(ar);
    }
    void DeserializeDocumentFromJson(JsonArchive &ar, SceneAssetDocument &doc)
    {
        // 与修复后的 LoadAssetDocument 一致：优先使用文档类自定义 Deserialize。
        doc.Deserialize(ar);
    }

    bool TestSceneDocumentV2Roundtrip()
    {
        SceneAssetDocument doc;
        doc._header._guid = "doc-guid";
        doc._header._asset_type = "Scene";
        doc._header._asset_name = "test_scene";
        doc._scene_format_version = SceneAssetDocument::kCurrentSceneFormatVersion;

        // 全部用索引/局部变量访问，不保存跨 std::vector 扩容的引用。
        const Guid e0_guid = Guid::Generate();
        const Guid e1_guid = Guid::Generate();
        const Vector3f e0_position{1.0f, 2.0f, 3.0f};
        const String e1_inv_matrix = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";

        SceneEntityDocument e0;
        e0._entity_guid = e0_guid;
        e0._tag_component._name = "Root";
        e0._tag_component._layer_mask = 7u;
        e0._has_transform_component = true;
        e0._transform_component._position = e0_position;
        e0._has_hierarchy_component = true;
        e0._hierarchy_component._parent_guid = Guid::EmptyGuid();
        e0._hierarchy_component._sibling_index = 0u;
        doc._entities.push_back(std::move(e0));

        SceneEntityDocument e1;
        e1._entity_guid = e1_guid;
        e1._tag_component._name = "Child";
        e1._has_hierarchy_component = true;
        e1._hierarchy_component._parent_guid = e0_guid;
        e1._hierarchy_component._sibling_index = 0u;
        e1._hierarchy_component._inv_matrix_attach = e1_inv_matrix;
        doc._entities.push_back(std::move(e1));

        JsonArchive ar;
        SerializeDocumentToJson(ar, doc);
        String json = ar.SaveToString();

        // V2 格式不得包含运行时句柄 `_entity_id`。
        if (json.find("_entity_id") != String::npos)
            return false;

        JsonArchive ar2;
        if (!ar2.LoadFromString(json))
            return false;
        SceneAssetDocument doc2;
        DeserializeDocumentFromJson(ar2, doc2);

        if (doc2._scene_format_version != doc._scene_format_version)
        {
            std::cout << "version mismatch: " << doc2._scene_format_version << " vs " << doc._scene_format_version << "\n";
            return false;
        }
        if (doc2._entities.size() != 2u)
        {
            std::cout << "entity count mismatch: " << doc2._entities.size() << "\n";
            return false;
        }
        const SceneEntityDocument &r0 = doc2._entities[0];
        const SceneEntityDocument &r1 = doc2._entities[1];
        if (!(r0._entity_guid == e0_guid))
        {
            std::cout << "e0 guid mismatch: '" << r0._entity_guid.ToString() << "' vs '" << e0_guid.ToString() << "'\n";
            return false;
        }
        if (r0._tag_component._name != "Root" || r0._tag_component._layer_mask != 7u)
        {
            std::cout << "e0 tag mismatch\n";
            return false;
        }
        if (!(r0._transform_component._position == e0_position))
        {
            std::cout << "e0 transform mismatch\n";
            return false;
        }
        if (!r0._hierarchy_component._parent_guid.IsEmpty())
        {
            std::cout << "e0 parent_guid not empty\n";
            return false;
        }
        if (!(r1._hierarchy_component._parent_guid == e0_guid))
        {
            std::cout << "e1 parent_guid mismatch: '" << r1._hierarchy_component._parent_guid.ToString() << "' vs '" << e0_guid.ToString() << "'\n";
            return false;
        }
        if (r1._hierarchy_component._sibling_index != 0u)
        {
            std::cout << "e1 sibling_index mismatch\n";
            return false;
        }
        if (r1._hierarchy_component._inv_matrix_attach != e1_inv_matrix)
        {
            std::cout << "e1 inv_matrix mismatch\n";
            return false;
        }
        return true;
    }

    bool TestSceneDocumentV1LegacyDeserialize()
    {
        // 模拟旧 V1 场景文档：使用 `_entity_id` 与层级链表字段，无 `_entity_guid`/`_parent_guid`。
        const String v1_json = R"({
            "_header": {"_guid": "v1-doc", "_asset_type": "Scene", "_asset_name": "legacy"},
            "_entities": [
                {
                    "_entity_id": 0,
                    "_tag_component": {"_name": "Root", "_layer_mask": 0},
                    "_has_hierarchy_component": true,
                    "_hierarchy_component": {"_first_child": 1, "_prev_sibling": 0, "_next_sibling": 0, "_parent": 0, "_children_num": 1, "_inv_matrix_attach": ""}
                },
                {
                    "_entity_id": 1,
                    "_tag_component": {"_name": "Child", "_layer_mask": 0},
                    "_has_hierarchy_component": true,
                    "_hierarchy_component": {"_first_child": 0, "_prev_sibling": 0, "_next_sibling": 0, "_parent": 0, "_children_num": 0, "_inv_matrix_attach": ""}
                }
            ]
        })";

        JsonArchive ar;
        if (!ar.LoadFromString(v1_json))
            return false;
        SceneAssetDocument doc;
        DeserializeDocumentFromJson(ar, doc);

        // 版本缺失默认 1u，进入 V1 迁移路径。
        if (doc._scene_format_version != 1u)
            return false;
        if (doc._entities.size() != 2u)
            return false;
        // legacy 字段被读入，V2 GUID 字段为空。
        if (doc._entities[0]._entity_id != 0u)
            return false;
        if (!doc._entities[0]._entity_guid.IsEmpty())
            return false;
        if (doc._entities[0]._hierarchy_component._first_child != 1u)
            return false;
        if (doc._entities[1]._hierarchy_component._parent != 0u)
            return false;
        return true;
    }

    // =========================================================================
    // EntityReference 持久引用测试（无需 GPU）。
    // 覆盖任务包 8：IsValid 语义 + GUID 序列化往返（不出现运行时 Entity 数值）。
    // =========================================================================
    bool TestEntityReferenceIsValid()
    {
        EntityReference ref;
        if (ref.IsValid())
            return false;
        ref._entity_guid = Guid::Generate();
        if (!ref.IsValid())
            return false;
        ref._entity_guid = Guid::EmptyGuid();
        if (ref.IsValid())
            return false;
        // 只有 entity_guid 有效即视为有效引用（同场景引用允许 scene_guid 为空）。
        ref._scene_guid = Guid::Generate();
        ref._entity_guid = Guid::Generate();
        if (!ref.IsValid())
            return false;
        return true;
    }

    bool TestEntityReferenceSerializationRoundtrip()
    {
        EntityReference ref;
        ref._scene_guid = Guid::Generate();
        ref._entity_guid = Guid::Generate();

        String name = "_ref";
        JsonArchive ar;
        SerializerWrapper<EntityReference>::Serialize(&ref, ar, &name);
        String json = ar.SaveToString();

        // 引用数据不得包含运行时 Entity 数值（只含两个 GUID 字段）。
        if (json.find("_entity_id") != String::npos)
            return false;

        JsonArchive ar2;
        if (!ar2.LoadFromString(json))
            return false;
        EntityReference out;
        SerializerWrapper<EntityReference>::Deserialize(&out, ar2, &name);
        if (!(out._scene_guid == ref._scene_guid))
            return false;
        if (!(out._entity_guid == ref._entity_guid))
            return false;
        return true;
    }

    void RunEntityGuidUnitTests()
    {
        TestResult result;

        RunTest(result, "Guid::IsEmpty/IsValid", TestGuidIsEmptyIsValid);
        RunTest(result, "GuidHasher stable", TestGuidHasher);
        RunTest(result, "SceneDocument V2 roundtrip", TestSceneDocumentV2Roundtrip);
        RunTest(result, "SceneDocument V1 legacy deserialize", TestSceneDocumentV1LegacyDeserialize);
        RunTest(result, "EntityReference IsValid", TestEntityReferenceIsValid);
        RunTest(result, "EntityReference serialization roundtrip", TestEntityReferenceSerializationRoundtrip);

        std::cout << "========================================\n";
        std::cout << "Entity GUID unit tests passed: " << result._passed << '\n';
        std::cout << "Entity GUID unit tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";

        if (result._failed != 0u)
            std::exit(EXIT_FAILURE);
    }

    void RunAutomationCoreTests()
    {
        using namespace Ailu::Editor::AutomationCoreTests;
        TestResult result;
        RunTest(result, "AutomationValue primitives", TestAutomationValuePrimitives);
        RunTest(result, "AutomationValue array/object", TestAutomationValueArrayObject);
        RunTest(result, "AutomationValue equality", TestAutomationValueEquality);
        RunTest(result, "AutomationRegistry register/invoke", TestAutomationRegistry);
        RunTest(result, "AutomationService submit/tick", TestAutomationServiceSubmitTick);

        std::cout << "========================================\n";
        std::cout << "Automation core tests passed: " << result._passed << '\n';
        std::cout << "Automation core tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";
    }

    void RunAutomationReadModelTests()
    {
        using namespace Ailu::Editor::AutomationReadModelTests;
        TestResult result;
        RunTest(result, "Type name normalization", TestTypeNameNormalization);
        RunTest(result, "Value conversions", TestValueConversions);
        RunTest(result, "Component stable name", TestComponentStableName);
        RunTest(result, "Component descriptor read", TestComponentDescriptorRead);

        std::cout << "========================================\n";
        std::cout << "Automation read-model tests passed: " << result._passed << '\n';
        std::cout << "Automation read-model tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";
    }

    void RunAutomationAdapterTests()
    {
        using namespace Ailu::Editor::AutomationAdapterTests;
        TestResult result;
        RunTest(result, "Adapter resolve", TestAdapterResolve);
        RunTest(result, "Adapter describe component", TestAdapterDescribeComponent);
        RunTest(result, "Adapter read component", TestAdapterReadComponent);

        std::cout << "========================================\n";
        std::cout << "Automation adapter tests passed: " << result._passed << '\n';
        std::cout << "Automation adapter tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";
    }

    void RunAutomationTransportTests()
    {
        using namespace Ailu::Editor::AutomationTransportTests;
        TestResult result;
        RunTest(result, "AutomationJson roundtrip", TestJsonRoundtrip);
        RunTest(result, "Request/Result JSON", TestRequestResultJson);
        RunTest(result, "Session file", TestSessionFile);
        RunTest(result, "Named pipe round trip", TestPipeRoundTrip);

        std::cout << "========================================\n";
        std::cout << "Automation transport tests passed: " << result._passed << '\n';
        std::cout << "Automation transport tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";
    }

    void RunAutomationCommandTests()
    {
        using namespace Ailu::Editor::AutomationCommandTests;
        TestResult result;
        RunTest(result, "Command manager lifecycle", TestCommandManagerLifecycle);
        RunTest(result, "Command event notification", TestCommandEventNotification);
        RunTest(result, "SetProperty validation", TestSetPropertyCommandValidation);
        RunTest(result, "Adapter write component", TestAdapterWriteComponent);
        RunTest(result, "Value conversions", TestValueConversions);
        RunTest(result, "Permission gate", TestPermissionGate);

        std::cout << "========================================\n";
        std::cout << "Automation command tests passed: " << result._passed << '\n';
        std::cout << "Automation command tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";
    }

    void RunAIAssistantTests()
    {
        using namespace Ailu::Editor::AIAssistantTests;
        TestResult result;
        RunTest(result, "AI loop read+preview+apply", TestAILoopReadAndPreview);
        RunTest(result, "AI reject", TestAIReject);
        RunTest(result, "AI immediate finish (read-only)", TestAIImmediateFinishForReadOnly);

        std::cout << "========================================\n";
        std::cout << "AI assistant tests passed: " << result._passed << '\n';
        std::cout << "AI assistant tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";
    }

    void RunAllocatorTests()
    {
        TestResult result;

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
        RunTest(result, "Sprite batch offsets with multi texture/material",
                TestSpriteBatchOffsetsForMultiTextureAndMaterial);
        RunTest(result, "Sprite batch offsets remain split after sorting",
                TestSpriteBatchOffsetsRemainSplitAfterSorting);
        RunTest(result, "InputSystem button action smoke", TestInputSystemButtonAction);
        RunTest(result, "GraphDocument add link and replace input", TestGraphDocumentAddLinkAndReplaceInput);
        RunTest(result, "GraphDocument rejects invalid connection", TestGraphDocumentRejectsInvalidConnection);
        RunTest(result, "GraphDocument remove node cleans links", TestGraphDocumentRemoveNodeCleansLinks);
        RunTest(result, "GraphDocument remove links", TestGraphDocumentRemoveLinks);
        RunTest(result, "GraphDocument set node positions", TestGraphDocumentSetNodePositions);
        RunTest(result, "GraphCommandStack undo redo", TestGraphCommandStackUndoRedo);
        RunTest(result, "Graph paste command remaps guids", TestGraphPasteCommandRemapsGuids);
        RunTest(result, "GraphValidation finds duplicate ids", TestGraphValidationFindsDuplicateIds);

        std::cout << "\nAllocator page state:\n";
        std::cout << Allocator::Get().GetPageMgr().Dump() << '\n';

        std::cout << "========================================\n";
        std::cout << "Allocator tests passed: " << result._passed << '\n';
        std::cout << "Allocator tests failed: " << result._failed << '\n';
        std::cout << "========================================\n";

        Allocator::Shutdown();
        LogMgr::Shutdown();

        if (result._failed != 0u)
            std::exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    // 统一初始化日志与分配器：文档反序列化路径中的 LOG_* 需要 LogMgr 就绪。
    LogMgr::Init();
    Allocator::Init();
    // DEBUG 构建下部分引擎系统（如 SceneMgr 构造的 TIMER_BLOCK）依赖 TimeMgr。
    TimeMgr::Init();

    RunAutomationCoreTests();
    RunAutomationReadModelTests();
    RunAutomationAdapterTests();
    RunAutomationTransportTests();
    RunAutomationCommandTests();
    RunAIAssistantTests();
    RunEntityGuidUnitTests();
    RunAllocatorTests();// 内部负责 Shutdown
    return EXIT_SUCCESS;
}
