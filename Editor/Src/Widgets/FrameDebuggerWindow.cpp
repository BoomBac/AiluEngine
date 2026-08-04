#include "Widgets/FrameDebuggerWindow.h"
#include "Render/FrameDebugger/FrameCaptureService.h"
#include "UI/Element/UITableElement.h"
#include "UI/TreeView.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UISlot.h"
#include "Framework/Common/Log.h"
#include <format>

using namespace Ailu::Render::FrameDebugger;
using namespace Ailu::UI;

namespace Ailu::Editor
{
namespace
{
    struct FilterDesc
    {
        const char *_name;
        u32 _bit;
    };
    const FilterDesc kFilterDescs[] = {
        {"Draw", (u32)EFrameEventFilter::kDraw},
        {"Disp", (u32)EFrameEventFilter::kDispatch},
        {"Barr", (u32)EFrameEventFilter::kBarrier},
        {"Skip", (u32)EFrameEventFilter::kSkipped},
        {"PsoDirty", (u32)EFrameEventFilter::kPsoDirty},
        {"PsoMiss", (u32)EFrameEventFilter::kPsoCacheMiss},
        {"MatMiss", (u32)EFrameEventFilter::kMaterialCacheMiss},
        {"Bind", (u32)EFrameEventFilter::kRootSlotBind},
        {"Inher", (u32)EFrameEventFilter::kInheritedBinding},
        {"Warn", (u32)EFrameEventFilter::kWarnings},
    };
    constexpr u32 kFilterCount = sizeof(kFilterDescs) / sizeof(kFilterDescs[0]);

    String ResourceTypeDisplayName(u32 type)
    {
        switch (static_cast<Ailu::Render::EBindResDescType>(type))
        {
            case Ailu::Render::EBindResDescType::kConstBuffer: return "ConstantBuffer";
            case Ailu::Render::EBindResDescType::kConstBufferRaw: return "RawConstantBuffer";
            case Ailu::Render::EBindResDescType::kTexture2D: return "Texture2D";
            case Ailu::Render::EBindResDescType::kTexture2DArray: return "Texture2DArray";
            case Ailu::Render::EBindResDescType::kCubeMap: return "CubeMap";
            case Ailu::Render::EBindResDescType::kUAVTexture2D: return "RWTexture2D";
            case Ailu::Render::EBindResDescType::kTexture3D: return "Texture3D";
            case Ailu::Render::EBindResDescType::kRWTexture3D: return "RWTexture3D";
            case Ailu::Render::EBindResDescType::kBuffer: return "Buffer";
            case Ailu::Render::EBindResDescType::kRWBuffer: return "RWBuffer";
            case Ailu::Render::EBindResDescType::kSampler: return "Sampler";
            case Ailu::Render::EBindResDescType::kAccelerationStructure: return "AccelerationStructure";
            default: return "Unknown";
        }
    }

    String ResourceStateDisplayName(u32 state)
    {
        if (state == 0u)
            return "Common";
        String result;
        const auto append = [&result](bool active, const char *name)
        {
            if (!active) return;
            if (!result.empty()) result += " | ";
            result += name;
        };
        append((state & 0x1u) != 0u, "VertexAndConstBuffer");
        append((state & 0x2u) != 0u, "IndexBuffer");
        append((state & 0x4u) != 0u, "RenderTarget");
        append((state & 0x8u) != 0u, "UnorderedAccess");
        append((state & 0x10u) != 0u, "DepthWrite");
        append((state & 0x20u) != 0u, "DepthRead");
        append((state & 0x40u) != 0u, "NonPS");
        append((state & 0x80u) != 0u, "PS");
        append((state & 0x100u) != 0u, "StreamOut");
        append((state & 0x200u) != 0u, "IndirectArgument");
        append((state & 0x400u) != 0u, "CopyDest");
        append((state & 0x800u) != 0u, "CopySource");
        append((state & 0x1000u) != 0u, "ResolveDest");
        append((state & 0x2000u) != 0u, "ResolveSource");
        append((state & 0x400000u) != 0u, "RayTracingAS");
        append((state & 0x1000000u) != 0u, "ShadingRateSource");
        return result.empty() ? std::format("0x{:X}", state) : result;
    }
}

    class FrameDebuggerWindow::EventTreeDataSource final : public UI::ITreeViewDataSource
    {
    public:
        void SetCapture(Ref<const FrameCapture> capture)
        {
            _capture = std::move(capture);
            _roots.clear();
            if (_capture)
            {
                const u32 n = (u32)_capture->Events().size();
                _parents.assign(n, ~0u);
                Vector<u32> index_from_id(n);
                for (u32 i = 0u; i < n; ++i)
                {
                    const u32 event_id = _capture->Events()[i]._event_id;
                    if (event_id < n) index_from_id[event_id] = i;
                }
                u32 active_pass = ~0u;
                for (u32 i = 0u; i < n; ++i)
                {
                    const auto &event = _capture->Events()[i];
                    // RenderGraph pass capture is the actual execution boundary and owns the command group.
                    if (event._parent_event_id != kInvalidFrameEventId && event._parent_event_id < n)
                    {
                        _parents[i] = index_from_id[event._parent_event_id];
                        if (event._type == EFrameEventType::kCommandGroup || event._type == EFrameEventType::kRenderGraphPass)
                            active_pass = i;
                    }
                    else if (event._type == EFrameEventType::kRenderGraphPass || event._type == EFrameEventType::kCommandGroup)
                    {
                        active_pass = i;
                        _roots.push_back(i);
                    }
                    else if ((event._type == EFrameEventType::kDraw || event._type == EFrameEventType::kDispatch ||
                              event._type == EFrameEventType::kResourceBarrier) && active_pass != ~0u)
                    {
                        _parents[i] = active_pass;
                    }
                    else
                    {
                        _roots.push_back(i);
                    }
                }
                if (_roots.empty() && n != 0u)
                {
                    LOG_WARNING("FrameDebugger: capture contains {} events but no tree roots; displaying a flat event list.", n);
                    _roots.reserve(n);
                    for (u32 i = 0u; i < n; ++i)
                    {
                        _parents[i] = ~0u;
                        _roots.push_back(i);
                    }
                }
                LOG_INFO("FrameDebugger: built event tree with {} events and {} roots.", n, _roots.size());
            }
            RecomputeVisibility();
        }

        void SetFilter(u32 filter)
        {
            _filter = filter;
            RecomputeVisibility();
        }

        Vector<UI::TreeItemId> GetRootItems() const override
        {
            Vector<UI::TreeItemId> items;
            items.reserve(_roots.size());
            for (u32 idx : _roots)
                if (IsVisible(idx))
                    items.push_back(static_cast<UI::TreeItemId>(idx + 1u));
            return items;
        }

        Vector<UI::TreeItemId> GetChildren(UI::TreeItemId parent) const override
        {
            Vector<UI::TreeItemId> items;
            if (!_capture) return items;
            u32 parent_idx = static_cast<u32>(static_cast<u64>(parent)) - 1u;
            for (u32 i = 0u; i < _parents.size(); ++i)
                if (_parents[i] == parent_idx && IsVisible(i))
                    items.push_back(static_cast<UI::TreeItemId>(i + 1u));
            return items;
        }

        UI::TreeItemId GetParent(UI::TreeItemId item) const override
        {
            if (!_capture) return 0u;
            u32 idx = static_cast<u32>(static_cast<u64>(item)) - 1u;
            if (idx >= _capture->Events().size()) return 0u;
            if (!IsVisible(idx)) return 0u;
            return _parents[idx] == ~0u ? static_cast<UI::TreeItemId>(0u)
                                        : static_cast<UI::TreeItemId>(_parents[idx] + 1u);
        }

        UI::TreeItemPresentation GetPresentation(UI::TreeItemId item) const override
        {
            UI::TreeItemPresentation pres;
            if (!_capture) return pres;
            u32 idx = static_cast<u32>(static_cast<u64>(item)) - 1u;
            if (idx >= _capture->Events().size()) return pres;

            const auto &event = _capture->Events()[idx];
            String name = String(_capture->Strings().Get(event._name));
            if (name.empty())
            {
                switch (event._type)
                {
                case EFrameEventType::kFrame: name = "Frame"; break;
                case EFrameEventType::kRenderGraphPass: name = "RG Pass"; break;
                case EFrameEventType::kCommandGroup: name = "CmdGroup"; break;
                case EFrameEventType::kDraw: name = "Draw"; break;
                case EFrameEventType::kDispatch: name = "Dispatch"; break;
                case EFrameEventType::kResourceBarrier: name = "Barrier"; break;
                default: name = "Event"; break;
                }
            }

            pres._label = std::format("[{}] {}", event._submission_index, name);

            if (event._execution_result != EFrameEventExecutionResult::kExecuted)
                pres._text_color = Color(1.0f, 0.3f, 0.3f, 1.0f);

            return pres;
        }

        bool IsValid(UI::TreeItemId item) const override
        {
            if (!_capture) return false;
            u32 idx = static_cast<u32>(static_cast<u64>(item)) - 1u;
            return idx < _capture->Events().size();
        }

    private:
        bool IsVisible(u32 idx) const
        {
            return idx < _visible.size() && _visible[idx] != 0u;
        }

        void RecomputeVisibility()
        {
            _visible.assign(_capture ? _capture->Events().size() : 0u, 1u);
            if (!_capture || _filter == 0u)
                return;
            for (u32 i = 0u; i < _capture->Events().size(); ++i)
                _visible[i] = SelfMatches(i) ? 1u : 0u;
            for (i32 i = static_cast<i32>(_visible.size()) - 1; i >= 0; --i)
            {
                if (_visible[i] != 0u && _parents[i] != ~0u)
                    _visible[_parents[i]] = 1u;
            }
        }

        bool SelfMatches(u32 idx) const
        {
            const auto &event = _capture->Events()[idx];
            const u32 f = _filter;
            if ((f & (u32)EFrameEventFilter::kDraw) != 0u && event._type == EFrameEventType::kDraw) return true;
            if ((f & (u32)EFrameEventFilter::kDispatch) != 0u && event._type == EFrameEventType::kDispatch) return true;
            if ((f & (u32)EFrameEventFilter::kBarrier) != 0u && event._type == EFrameEventType::kResourceBarrier) return true;
            if ((f & (u32)EFrameEventFilter::kSkipped) != 0u && event._execution_result != EFrameEventExecutionResult::kExecuted) return true;
            if (event._type != EFrameEventType::kDraw)
                return false;
            if (event._payload_index >= _capture->Draws().size())
                return false;
            const auto &draw = _capture->Draws()[event._payload_index];
            if ((f & (u32)EFrameEventFilter::kPsoDirty) != 0u && draw._pso_dirty_reasons != 0u) return true;
            if ((f & (u32)EFrameEventFilter::kPsoCacheMiss) != 0u && draw._pso_lookup_result == (u8)EPsoLookupResult::kCacheMiss) return true;
            if ((f & (u32)EFrameEventFilter::kMaterialCacheMiss) != 0u && draw._material_binding_result != (u8)EMaterialBindingResolveResult::kCacheHit) return true;
            if ((f & (u32)EFrameEventFilter::kRootSlotBind) != 0u || (f & (u32)EFrameEventFilter::kInheritedBinding) != 0u || (f & (u32)EFrameEventFilter::kWarnings) != 0u)
            {
                const u32 range_begin = draw._binding_range_begin;
                for (u16 b = 0u; b < draw._binding_count && range_begin + b < _capture->Bindings().size(); ++b)
                {
                    const auto &binding = _capture->Bindings()[range_begin + b];
                    if ((f & (u32)EFrameEventFilter::kRootSlotBind) != 0u && binding._cache_result == (u8)EBindingCacheResult::kBound) return true;
                    if ((f & (u32)EFrameEventFilter::kInheritedBinding) != 0u && binding._cache_result == (u8)EBindingCacheResult::kInherited) return true;
                    if ((f & (u32)EFrameEventFilter::kWarnings) != 0u && binding._invalid_reasons != 0u) return true;
                }
            }
            return false;
        }

        Ref<const FrameCapture> _capture;
        Vector<u32> _roots;
        Vector<u32> _parents;
        Vector<u8> _visible;
        u32 _filter = 0u;
    };

    class FrameDebuggerWindow::BindingCacheTableDataSource final : public UI::TableDataSource
    {
    public:
        void SetCapture(Ref<const FrameCapture> capture) { _capture = std::move(capture); }
        void SetBindings(u32 range_begin, u16 count)
        {
            _binding_indices.clear();
            if (!_capture)
                return;
            const auto &bindings = _capture->Bindings();
            for (u16 i = 0u; i < count && range_begin + i < bindings.size(); ++i)
                _binding_indices.push_back(range_begin + i);
        }

        i32 GetRowCount() const override { return static_cast<i32>(_binding_indices.size()); }

        String GetCellText(i32 row, i32 column) const override
        {
            if (row < 0 || row >= static_cast<i32>(_binding_indices.size()))
                return {};
            return GetCellTextByBindingIndex(_binding_indices[static_cast<u32>(row)], column);
        }

        void Sort(i32 column, bool ascending) override
        {
            std::sort(_binding_indices.begin(), _binding_indices.end(), [this, column, ascending](u32 lhs, u32 rhs)
            {
                const String lhs_text = GetCellTextByBindingIndex(lhs, column);
                const String rhs_text = GetCellTextByBindingIndex(rhs, column);
                return ascending ? lhs_text < rhs_text : lhs_text > rhs_text;
            });
        }

    private:
        String GetCellTextByBindingIndex(u32 idx, i32 column) const
        {
            if (!_capture)
                return {};
            const auto &bindings = _capture->Bindings();
            if (idx >= bindings.size())
                return {};
            const auto &binding = bindings[idx];
            switch (column)
            {
                case 0: return std::to_string(binding._slot);
                case 1: return ShaderResourceTypeName(binding._shader_resource_type);
                case 2:
                {
                    const String name(_capture->Strings().Get(binding._slot_name));
                    return name.empty() ? String("<unnamed>") : name;
                }
                case 3: return ShaderResourceTypeName(binding._key._resource_type);
                case 4: return ObjectName(binding._key._resource_id);
                case 5:
                {
                    const char *source_name = binding._source == (u8)EBindingSource::kGlobal ? "Global" :
                                              binding._source == (u8)EBindingSource::kMaterial ? "Material" :
                                              binding._source == (u8)EBindingSource::kCommand ? "Command" :
                                              binding._source == (u8)EBindingSource::kInherited ? "Inherited" :
                                              binding._source == (u8)EBindingSource::kShader ? "Shader" : "Unknown";
                    return source_name;
                }
                default: return {};
            }
        }

        static String ShaderResourceTypeName(u32 type)
        {
            return ResourceTypeDisplayName(type);
        }

        String ObjectName(CaptureObjectId id) const
        {
            const auto *info = _capture->Objects().Get(id);
            if (info == nullptr) return String("<none>");
            const String name(_capture->Strings().Get(info->_name));
            return name.empty() ? std::format("0x{:X}", info->_runtime_instance_id) : name;
        }

    private:
        Ref<const FrameCapture> _capture;
        Vector<u32> _binding_indices;
    };

    class FrameDebuggerWindow::BindingInvalidationTableDataSource final : public UI::TableDataSource
    {
    public:
        void SetCapture(Ref<const FrameCapture> capture) { _capture = std::move(capture); }

        void SetBindings(u32 range_begin, u16 count)
        {
            _rows.clear();
            if (!_capture)
                return;
            const auto &bindings = _capture->Bindings();
            for (u16 i = 0u; i < count && range_begin + i < bindings.size(); ++i)
            {
                const auto &binding = bindings[range_begin + i];
                if (binding._invalid_reasons != 0u)
                    _rows.push_back({binding._slot, ReasonText(binding._invalid_reasons)});
            }
        }

        i32 GetRowCount() const override { return static_cast<i32>(_rows.size()); }

        String GetCellText(i32 row, i32 column) const override
        {
            if (row < 0 || row >= static_cast<i32>(_rows.size()))
                return {};
            const auto &entry = _rows[static_cast<u32>(row)];
            return column == 0 ? std::to_string(entry._slot) : entry._reason;
        }

        void Sort(i32 column, bool ascending) override
        {
            std::sort(_rows.begin(), _rows.end(), [column, ascending](const InvalidationRow &lhs, const InvalidationRow &rhs)
            {
                if (column == 0)
                    return ascending ? lhs._slot < rhs._slot : lhs._slot > rhs._slot;
                return ascending ? lhs._reason < rhs._reason : lhs._reason > rhs._reason;
            });
        }

    private:
        static String ReasonText(u32 reasons)
        {
            String result;
            const auto append = [&result](bool active, const char *text)
            {
                if (!active) return;
                if (!result.empty()) result += ", ";
                result += text;
            };
            append((reasons & (u32)EBindingInvalidReason::kSlotUninitialized) != 0u, "SlotUninitialized");
            append((reasons & (u32)EBindingInvalidReason::kPsoChanged) != 0u, "PSOChanged");
            append((reasons & (u32)EBindingInvalidReason::kCommandListReset) != 0u, "CommandListReset");
            append((reasons & (u32)EBindingInvalidReason::kResourceChanged) != 0u, "ResourceChanged");
            append((reasons & (u32)EBindingInvalidReason::kResourceTypeChanged) != 0u, "ResourceTypeChanged");
            append((reasons & (u32)EBindingInvalidReason::kGpuAddressChanged) != 0u, "GpuAddressChanged");
            append((reasons & (u32)EBindingInvalidReason::kNativeResourceChanged) != 0u, "NativeResourceChanged");
            append((reasons & (u32)EBindingInvalidReason::kViewIndexChanged) != 0u, "ViewIndexChanged");
            append((reasons & (u32)EBindingInvalidReason::kSubResourceChanged) != 0u, "SubResourceChanged");
            append((reasons & (u32)EBindingInvalidReason::kDescriptorHeapChanged) != 0u, "DescriptorHeapChanged");
            return result;
        }

        struct InvalidationRow
        {
            u16 _slot;
            String _reason;
        };

        Ref<const FrameCapture> _capture;
        Vector<InvalidationRow> _rows;
    };

    FrameDebuggerWindow::FrameDebuggerWindow() : DockWindow("Frame Debugger")
    {
        _data_source = MakeScope<EventTreeDataSource>();
        _binding_table_source = MakeScope<BindingCacheTableDataSource>();
        _invalidation_table_source = MakeScope<BindingInvalidationTableDataSource>();
        BuildUI();
        BindTreeEvents();
    }

    FrameDebuggerWindow::~FrameDebuggerWindow()
    {
        if (_event_tree) _event_tree->SetDataSource(nullptr);
    }

    void FrameDebuggerWindow::BuildUI()
    {
        auto *vb = _content_root->AddChild<UI::VerticalBox>();
        vb->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);

        {
            auto *toolbar = vb->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 24.0f));

            _capture_button = toolbar->AddChild<UI::Button>("Capture");
            _capture_button->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(70.0f, 24.0f));
            _capture_button->OnMouseClick() += [this](UI::UIEvent &) { OnCaptureClicked(); };

            _status_text = toolbar->AddChild<UI::Text>("Idle");
            _status_text->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
        }

        {
            auto *filter_bar = vb->AddChild<UI::HorizontalBox>();
            filter_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 24.0f));
            for (const auto &desc : kFilterDescs)
            {
                auto *btn = filter_bar->AddChild<UI::Button>(desc._name);
                btn->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed)
                    .Size(Vector2f(64.0f, 22.0f)).Margin(Padding(0.0f, 0.0f, 4.0f, 0.0f));
                btn->OnMouseClick() += [this, bit = desc._bit](UI::UIEvent &) { SetEventFilter(_event_filter ^ bit); };
                _filter_buttons.push_back(btn);
            }
        }

        {
            _summary_text = vb->AddChild<UI::Text>("No capture");
            _summary_text->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 20.0f));
        }

        {
            auto *main_split = vb->AddChild<UI::SplitView>();
            main_split->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            main_split->SetRatio(0.30f);

            auto *tree_panel = main_split->AddChild<UI::Border>();
            tree_panel->_bg_color = Color(0.055f, 0.06f, 0.075f, 1.0f);
            tree_panel->Thickness(0.0f);
            tree_panel->SlotPadding() = Padding(4.0f);
            auto *tree_layout = tree_panel->AddChild<UI::VerticalBox>();
            tree_layout->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            auto *tree_title = tree_layout->AddChild<UI::Text>("Frame 1234  |  Event Tree");
            tree_title->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 22.0f});
            tree_title->_color = Color(0.68f, 0.72f, 0.82f, 1.0f);
            _event_tree = tree_layout->AddChild<UI::TreeView>();
            _event_tree->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            _event_tree->SetExpandOnRowClick(true);

            auto *right_panel = main_split->AddChild<UI::Border>();
            right_panel->_bg_color = Color(0.055f, 0.06f, 0.075f, 1.0f);
            right_panel->Thickness(0.0f);
            right_panel->SlotPadding() = Padding(0.0f, 2.0f, 0.0f, 0.0f);
            auto *right_layout = right_panel->AddChild<UI::VerticalBox>();
            right_layout->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            auto *event_title = right_layout->AddChild<UI::Text>("Event #--: Select an event");
            event_title->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 26.0f});
            event_title->_color = Color(0.78f, 0.82f, 0.92f, 1.0f);
            auto *tab_clip = right_layout->AddChild<UI::ScrollView>();
            tab_clip->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 30.0f});
            auto *tabs = tab_clip->AddChild<UI::HorizontalBox>();
            const char *tab_names[] = {"Summary", "Pipeline State", "Geometry", "Resource Bindings", "Binding Cache", "Statistics"};
            const f32 tab_widths[] = {78.0f, 112.0f, 78.0f, 132.0f, 112.0f, 82.0f};
            for (u32 i = 0u; i < 6u; ++i)
            {
                auto *frame = tabs->AddChild<UI::Border>();
                frame->_bg_color = Color(0.10f, 0.11f, 0.14f, 1.0f);
                frame->_border_color = i == 0u ? Color(0.35f, 0.60f, 1.0f, 1.0f) : Color(0.20f, 0.23f, 0.30f, 1.0f);
                frame->Thickness(i == 0u ? 1.0f : 0.0f);
                frame->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed)
                    .Size({tab_widths[i], 26.0f}).Margin(Padding(0.0f, 0.0f, 3.0f, 0.0f));
                auto *tab = frame->AddChild<UI::Button>(tab_names[i]);
                tab->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
                tab->OnMouseClick() += [this, i](UI::UIEvent &) { SetActiveTab(i); };
                _tab_buttons.push_back(tab);
                _tab_frames.push_back(frame);
            }
            _detail_scroll = right_layout->AddChild<UI::ScrollView>();
            _detail_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            _detail_content = _detail_scroll->AddChild<UI::VerticalBox>();
            _binding_scroll = right_layout->AddChild<UI::ScrollView>();
            _binding_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 0.0f});
            _binding_content = _binding_scroll->AddChild<UI::VerticalBox>();

            _binding_table = _binding_content->AddChild<UI::UITableElement>();
            _binding_table->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 0.0f})
            .Margin(Padding(5.0f, 10.0f, 5.0f, 10.0f));
            {
                UI::TableColumn column;
                column._name = "Slot";
                column._width = 50.0f;
                column._sortable = true;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _binding_table->AddColumn(column);
                column._name = "Shader Type";
                column._width = 120.0f;
                column._sortable = true;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _binding_table->AddColumn(column);
                column._name = "Shader Resource Name";
                column._width = 160.0f;
                column._sortable = true;
                column._stretch = true;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _binding_table->AddColumn(column);
                column._name = "Bound Type";
                column._width = 150.0f;
                column._sortable = true;
                column._stretch = false;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _binding_table->AddColumn(column);
                column._name = "Bound Resource Name";
                column._width = 240.0f;
                column._sortable = true;
                column._stretch = true;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _binding_table->AddColumn(column);
                column._name = "Source";
                column._width = 90.0f;
                column._sortable = true;
                column._stretch = false;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _binding_table->AddColumn(column);
            }
            _binding_table->SetMultiSelectEnabled(true);
            _binding_table->SetDataSource(_binding_table_source.get());

            _invalidation_table = _binding_content->AddChild<UI::UITableElement>();
            _invalidation_table->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 0.0f})
            .Margin(Padding(5.0f, 10.0f, 5.0f, 10.0f));;
            {
                UI::TableColumn column;
                column._name = "Slot";
                column._width = 60.0f;
                column._sortable = true;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _invalidation_table->AddColumn(column);
                column._name = "Invalidation Reason";
                column._width = 420.0f;
                column._sortable = true;
                column._alignment = UI::ETableColumnAlignment::kCenter;
                _invalidation_table->AddColumn(column);
            }
            _invalidation_table->SetMultiSelectEnabled(true);
            _invalidation_table->SetDataSource(_invalidation_table_source.get());

            _diagnostic_content = _binding_content->AddChild<UI::VerticalBox>();
            _diagnostic_content->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            _binding_scroll->SetVisible(false);

            auto *no_sel = _detail_content->AddChild<UI::Text>("Select an event to view its summary");
            no_sel->_color = Color(0.4f, 0.4f, 0.4f, 1.0f);
        }

        _content_root->InvalidateLayout();
    }

    void FrameDebuggerWindow::BindTreeEvents()
    {
        _event_tree->_on_selection_changed += [this](UI::TreeItemId item)
        {
            u32 idx = static_cast<u32>(static_cast<u64>(item)) - 1u;
            if (_capture && idx < _capture->Events().size())
            {
                _selected_event_id = _capture->Events()[idx]._event_id;
                RefreshSelectedEventDetails(_selected_event_id);
            }
        };
    }

    void FrameDebuggerWindow::OnCaptureClicked()
    {
        FrameCaptureOptions opts;
        opts._output_mode = EFrameCaptureOutputMode::kMetadataOnly;
        FrameCaptureService::RequestCapture(opts);
        _status_text->SetText("Requested capture...");
    }

    void FrameDebuggerWindow::Update(f32 dt)
    {
        DockWindow::Update(dt);
        EFrameCaptureState current_state = FrameCaptureService::State();
        if (current_state != _observed_state)
        {
            _observed_state = current_state;
            switch (current_state)
            {
            case EFrameCaptureState::kIdle: _status_text->SetText("Idle"); break;
            case EFrameCaptureState::kArmed:
                _status_text->SetText("Armed");
                _capture.reset();
                _event_tree->SetDataSource(nullptr);
                _detail_content->ClearChildren();
                _binding_table_source->SetCapture(nullptr);
                _invalidation_table_source->SetCapture(nullptr);
                _binding_table->Refresh();
                _invalidation_table->Refresh();
                _binding_table->GetSlotAs<UI::LinearSlot>().Size({0.0f, 0.0f});
                _invalidation_table->GetSlotAs<UI::LinearSlot>().Size({0.0f, 0.0f});
                _diagnostic_content->ClearChildren();
                _detail_content->AddChild<UI::Text>("Waiting for the next captured frame...");
                _summary_text->SetText("Waiting for next frame capture...");
                break;
            case EFrameCaptureState::kCapturing: _status_text->SetText("Capturing..."); break;
            case EFrameCaptureState::kFinalizing: _status_text->SetText("Finalizing..."); break;
            case EFrameCaptureState::kReady:
                _status_text->SetText("Ready");
                RefreshCapture();
                break;
            }
        }
    }

    void FrameDebuggerWindow::SetEventFilter(u32 filter)
    {
        _event_filter = filter;
        if (_data_source) _data_source->SetFilter(_event_filter);
        if (_event_tree)
        {
            _event_tree->Refresh();
            _event_tree->ExpandAll();
        }
        for (u32 i = 0u; i < _filter_buttons.size() && i < kFilterCount; ++i)
        {
            const bool is_active = (_event_filter & kFilterDescs[i]._bit) != 0u;
            auto &style = _filter_buttons[i]->GetStyleOverride();
            style.ClearAllOverrides();
            if (is_active)
            {
                UIControlVisual active_visual;
                active_visual._background._type = EUIBrushType::kColor;
                active_visual._background._tint = Color(0.12f, 0.18f, 0.30f, 1.0f);
                active_visual._border_color = Color(0.35f, 0.60f, 1.0f, 1.0f);
                active_visual._border_width = Vector4f(1.0f);
                style.SetNormal(active_visual);
                style.SetHovered(active_visual);
                style.SetPressed(active_visual);
            }
            _filter_buttons[i]->InvalidateStyle();
        }
    }

    void FrameDebuggerWindow::RefreshCapture()
    {
        _capture = FrameCaptureService::LatestCapture();
        if (!_capture) return;

        _data_source->SetCapture(_capture);
        _data_source->SetFilter(_event_filter);
        _event_tree->EnsureStyleResolved();
        _event_tree->SetDataSource(_data_source.get());
        _event_tree->Refresh();
        _event_tree->ExpandAll();
        _selected_event_id = ~0u;

        const auto &stats = _capture->Statistics();
        _summary_text->SetText(std::format("Capture #{} | Events: {} | Draws: {} | Dispatches: {} | Barriers: {} | Mem: {} KB{}",
            stats._capture_revision, stats._event_count, stats._draw_count, stats._dispatch_count,
            stats._barrier_count, stats._capture_cpu_memory / 1024u,
            stats._is_truncated ? " [TRUNCATED]" : ""));
    }

    void FrameDebuggerWindow::RefreshEventTree()
    {
        _event_tree->Refresh();
    }

    void FrameDebuggerWindow::SetActiveTab(u32 tab_index)
    {
        _active_tab = tab_index;
        const char *tab_names[] = {"Summary", "Pipeline State", "Geometry", "Resource Bindings", "Binding Cache", "Statistics"};
        for (u32 i = 0u; i < _tab_buttons.size(); ++i)
        {
            const bool is_active = i == _active_tab;
            _tab_buttons[i]->SetText(tab_names[i], false);
            _tab_frames[i]->_border_color = is_active ? Color(0.35f, 0.60f, 1.0f, 1.0f) : Color(0.20f, 0.23f, 0.30f, 1.0f);
            _tab_frames[i]->Thickness(is_active ? 1.0f : 0.0f);
            auto &style = _tab_buttons[i]->GetStyleOverride();
            style.ClearAllOverrides();
            if (is_active)
            {
                UIControlVisual active_visual;
                active_visual._background._type = EUIBrushType::kColor;
                active_visual._background._tint = Color(0.12f, 0.18f, 0.30f, 1.0f);
                active_visual._border_color = Color(0.35f, 0.60f, 1.0f, 1.0f);
                active_visual._border_width = Vector4f(1.0f);
                style.SetNormal(active_visual);
                style.SetHovered(active_visual);
                style.SetPressed(active_visual);
            }
            _tab_buttons[i]->InvalidateStyle();
        }

        const bool show_binding_page = _active_tab == 3u || _active_tab == 4u;
        _detail_scroll->SetVisible(!show_binding_page);
        _binding_scroll->SetVisible(show_binding_page);
        if (show_binding_page)
        {
            _detail_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 0.0f});
            _binding_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
        }
        else
        {
            _detail_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            _binding_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, 0.0f});
        }
    }

    void FrameDebuggerWindow::RefreshSelectedEventDetails(u32 event_id)
    {
        if (!_capture) return;
        _detail_scroll->ResetScrollOffset();
        _detail_content->ClearChildren();
        _binding_scroll->ResetScrollOffset();
        _diagnostic_content->ClearChildren();

        const auto &events = _capture->Events();
        u32 evt_idx = ~0u;
        for (u32 i = 0u; i < events.size(); ++i)
        {
            if (events[i]._event_id == event_id) { evt_idx = i; break; }
        }
        if (evt_idx == ~0u) return;

        const auto &event = events[evt_idx];
        if (event._type != EFrameEventType::kDraw && event._type != EFrameEventType::kDispatch)
            ClearBindingCacheDetails();

        auto add_line = [this](const String &label, const String &value)
        {
            auto *row = _detail_content->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 20.0f)).Margin(Padding(10.0f, 0.0f, 6.0f, 0.0f));
            auto *lbl = row->AddChild<UI::Text>(label);
            lbl->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kAuto).Size(Vector2f(120.0f, 0.0f));
            lbl->_color = Color(0.6f, 0.6f, 0.8f, 1.0f);
            auto *val = row->AddChild<UI::Text>(value);
            val->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
        };

        auto object_name = [this](CaptureObjectId id)
        {
            const auto *info = _capture->Objects().Get(id);
            if (info == nullptr) return String("<none>");
            String name(_capture->Strings().Get(info->_name));
            return name.empty() ? std::format("0x{:X}", info->_runtime_instance_id) : name;
        };

        auto add_flags = [&add_line](const String &label, u32 flags, std::initializer_list<std::pair<u32, const char *>> names)
        {
            String value;
            for (const auto &[bit, name] : names)
            {
                if ((flags & bit) == 0u) continue;
                if (!value.empty()) value += " | ";
                value += name;
            }
            add_line(label, value.empty() ? "None" : value);
        };

        const char *type_names[] = {"Frame", "RGPass", "CmdGrp", "Profiler", "SetRT", "Clear", "Draw", "Dispatch",
                                    "Rays", "BuildAS", "Upload", "Barrier", "UAVBarr", "Copy", "Readback", "Present", "Int"};

        add_line("Type", type_names[(u8)event._type]);
        add_line("EventID", std::to_string(event._event_id));
        add_line("Submission", std::to_string(event._submission_index));
        add_line("CmdIndex", std::to_string(event._command_index));
        String evt_name = String(_capture->Strings().Get(event._name));
        if (!evt_name.empty()) add_line("Name", evt_name);

        if (event._type == EFrameEventType::kDraw)
        {
            u32 payload_idx = event._payload_index;
            if (payload_idx < _capture->Draws().size())
            {
                const auto &draw = _capture->Draws()[payload_idx];
                RefreshBindingCacheDetails(draw._binding_range_begin, draw._binding_count);
                const auto format_flags = [](u32 flags, std::initializer_list<std::pair<u32, const char *>> names)
                {
                    String result;
                    for (const auto &[bit, name] : names)
                    {
                        if ((flags & bit) == 0u) continue;
                        if (!result.empty()) result += " | ";
                        result += name;
                    }
                    return result.empty() ? String("None") : result;
                };
                AddDiagnosticText(draw._pso_dirty_reasons == 0u ? "PSO Dirty: none" : std::format(
                    "PSO Dirty: {} (0x{:X})", format_flags(draw._pso_dirty_reasons,
                    {{(u32)EPsoDirtyReason::kShaderChanged, "Shader"}, {(u32)EPsoDirtyReason::kShaderPassChanged, "Pass"},
                     {(u32)EPsoDirtyReason::kShaderVariantChanged, "Variant"}, {(u32)EPsoDirtyReason::kVertexLayoutChanged, "Vertex layout"},
                     {(u32)EPsoDirtyReason::kBlendStateChanged, "Blend"}, {(u32)EPsoDirtyReason::kRasterizerStateChanged, "Rasterizer"},
                     {(u32)EPsoDirtyReason::kDepthStencilStateChanged, "Depth/stencil"}, {(u32)EPsoDirtyReason::kRenderTargetStateChanged, "Render target"}}),
                    draw._pso_dirty_reasons),
                    draw._pso_dirty_reasons == 0u ? Color(0.35f, 0.85f, 0.50f, 1.0f) : Color(0.95f, 0.60f, 0.36f, 1.0f));
                AddDiagnosticText(draw._material_binding_invalid_reasons == 0u ? "Material Cache: hit" : std::format(
                    "Material Cache miss: {} (0x{:X})", format_flags(draw._material_binding_invalid_reasons,
                    {{(u32)EMaterialBindingInvalidReason::kMaterialResourceChanged, "Material resources"},
                     {(u32)EMaterialBindingInvalidReason::kBindingLayoutChanged, "Binding layout"},
                     {(u32)EMaterialBindingInvalidReason::kShaderVariantChanged, "Shader variant"},
                     {(u32)EMaterialBindingInvalidReason::kGlobalLayoutChanged, "Global layout"},
                     {(u32)EMaterialBindingInvalidReason::kGlobalBindingChanged, "Global bindings"}}),
                    draw._material_binding_invalid_reasons),
                    draw._material_binding_invalid_reasons == 0u ? Color(0.35f, 0.85f, 0.50f, 1.0f) : Color(0.95f, 0.60f, 0.36f, 1.0f));
                add_line("", "");
                add_line("--- Draw Info ---", "");
                add_line("Indexed", draw._is_indexed ? "Yes" : "No");
                add_line("Indirect", draw._is_indirect ? "Yes" : "No");
                add_line("Procedural", draw._is_procedural ? "Yes" : "No");
                add_line("Vertices", std::to_string(draw._vertex_count));
                add_line("Indices", std::to_string(draw._index_count));
                add_line("Instances", std::to_string(draw._instance_count));
                add_line("PassIdx", std::to_string(draw._pass_index));
                add_line("Variant", std::format("0x{:X}", draw._variant_hash));
                add_line("Material", object_name(draw._material_id));
                add_line("Shader", object_name(draw._shader_id));
                add_line("PSO", object_name(draw._pso_id));
                add_line("VertexBuffer", object_name(draw._vertex_buffer_id));
                add_line("IndexBuffer", object_name(draw._index_buffer_id));
                add_line("PSO Lookup", draw._pso_lookup_result == (u8)EPsoLookupResult::kCacheHit ? "Library hit" :
                                       draw._pso_lookup_result == (u8)EPsoLookupResult::kCacheMiss ? "Library miss" :
                                       draw._pso_lookup_result == (u8)EPsoLookupResult::kCreationRequested ? "Creation requested" : "Not required");
                add_line("Native PSO Bind", draw._pso_bind_reason == (u8)EPsoBindReason::kCacheHit ? "Skipped" : "Bound");
                add_flags("PSO Dirty", draw._pso_dirty_reasons,
                          {{(u32)EPsoDirtyReason::kShaderChanged, "Shader"}, {(u32)EPsoDirtyReason::kShaderPassChanged, "Pass"},
                           {(u32)EPsoDirtyReason::kShaderVariantChanged, "Variant"}, {(u32)EPsoDirtyReason::kVertexLayoutChanged, "Vertex layout"},
                           {(u32)EPsoDirtyReason::kBlendStateChanged, "Blend"}, {(u32)EPsoDirtyReason::kRasterizerStateChanged, "Rasterizer"},
                           {(u32)EPsoDirtyReason::kDepthStencilStateChanged, "Depth/stencil"}, {(u32)EPsoDirtyReason::kRenderTargetStateChanged, "Render target"}});
                add_line("Material Cache", draw._material_binding_result == (u8)EMaterialBindingResolveResult::kCacheHit ? "Cache hit" :
                                          draw._material_binding_result == (u8)EMaterialBindingResolveResult::kGlobalBindingRefresh ? "Global refresh" : "Full rebuild");
                add_flags("Material Miss", draw._material_binding_invalid_reasons,
                          {{(u32)EMaterialBindingInvalidReason::kMaterialResourceChanged, "Material resources"},
                           {(u32)EMaterialBindingInvalidReason::kBindingLayoutChanged, "Binding layout"},
                           {(u32)EMaterialBindingInvalidReason::kShaderVariantChanged, "Shader variant"},
                           {(u32)EMaterialBindingInvalidReason::kGlobalLayoutChanged, "Global layout"},
                           {(u32)EMaterialBindingInvalidReason::kGlobalBindingChanged, "Global bindings"}});

                add_line("Bindings", std::to_string(draw._binding_count));
                add_line("Barriers", std::to_string(draw._barrier_count));

                for (u16 b = 0u; b < draw._binding_count && (draw._binding_range_begin + b) < _capture->Bindings().size(); ++b)
                {
                    const auto &binding = _capture->Bindings()[draw._binding_range_begin + b];
                    const char *result = binding._cache_result == (u8)EBindingCacheResult::kBound ? "Bind" :
                                         binding._cache_result == (u8)EBindingCacheResult::kSkipped ? "Skip" :
                                         binding._cache_result == (u8)EBindingCacheResult::kInherited ? "Inherited" : "Unbound";
                    const char *source_name = binding._source == (u8)EBindingSource::kGlobal ? "Global" :
                                              binding._source == (u8)EBindingSource::kMaterial ? "Material" :
                                              binding._source == (u8)EBindingSource::kCommand ? "Command" :
                                              binding._source == (u8)EBindingSource::kInherited ? "Inherited" :
                                              binding._source == (u8)EBindingSource::kShader ? "Shader" : "Unknown";
                    add_line("Root Slot " + std::to_string(binding._slot),
                             std::format("{} | source={} | resource={}", result, source_name, object_name(binding._key._resource_id)));
                    add_flags("  Invalidation", binding._invalid_reasons,
                              {{(u32)EBindingInvalidReason::kSlotUninitialized, "Slot uninitialized"},
                               {(u32)EBindingInvalidReason::kPsoChanged, "PSO changed"},
                               {(u32)EBindingInvalidReason::kCommandListReset, "Command list reset"},
                               {(u32)EBindingInvalidReason::kResourceChanged, "Resource changed"},
                               {(u32)EBindingInvalidReason::kResourceTypeChanged, "Resource type changed"},
                               {(u32)EBindingInvalidReason::kGpuAddressChanged, "GPU address changed"},
                               {(u32)EBindingInvalidReason::kNativeResourceChanged, "Native resource changed"},
                               {(u32)EBindingInvalidReason::kViewIndexChanged, "View changed"},
                               {(u32)EBindingInvalidReason::kSubResourceChanged, "Subresource changed"},
                               {(u32)EBindingInvalidReason::kDescriptorHeapChanged, "Descriptor heap changed"}});
                    if (binding._inherited_from_event != 0u)
                        add_line("  Inherited from", std::to_string(binding._inherited_from_event));
                }

                }
            }
        else if (event._type == EFrameEventType::kDispatch)
        {
            u32 payload_idx = event._payload_index;
            if (payload_idx < _capture->Dispatches().size())
            {
                const auto &disp = _capture->Dispatches()[payload_idx];
                RefreshBindingCacheDetails(disp._binding_range_begin, disp._binding_count);
                add_line("", "");
                add_line("Groups", std::format("{}x{}x{}", disp._group_num_x, disp._group_num_y, disp._group_num_z));
                add_line("Indirect", disp._is_indirect ? "Yes" : "No");
                add_line("Bindings", std::to_string(disp._binding_count));
                for (u16 b = 0u; b < disp._binding_count && (disp._binding_range_begin + b) < _capture->Bindings().size(); ++b)
                {
                    const auto &binding = _capture->Bindings()[disp._binding_range_begin + b];
                    const char *source_name = binding._source == (u8)EBindingSource::kGlobal ? "Global" :
                                              binding._source == (u8)EBindingSource::kMaterial ? "Material" :
                                              binding._source == (u8)EBindingSource::kCommand ? "Command" :
                                              binding._source == (u8)EBindingSource::kInherited ? "Inherited" :
                                              binding._source == (u8)EBindingSource::kShader ? "Shader" : "Unknown";
                    add_line("  Slot " + std::to_string(binding._slot),
                             std::format("{} | type={} | resource={}",
                                         source_name,
                                         ResourceTypeDisplayName(binding._key._resource_type),
                                         object_name(binding._key._resource_id)));
                }
            }
        }
        else if (event._type == EFrameEventType::kResourceBarrier)
        {
            u32 payload_idx = event._payload_index;
            if (payload_idx < _capture->Barriers().size())
            {
                const auto &barrier = _capture->Barriers()[payload_idx];
                add_line("", "");
                add_line("--- Barrier Info ---", "");
                add_line("Resource", object_name(barrier._resource_id));
                if (barrier._is_uav)
                {
                    add_line("Before", "N/A (UAV barrier)");
                    add_line("After", "N/A (UAV barrier)");
                }
                else
                {
                    add_line("Before", std::format("{} (0x{:X})", ResourceStateDisplayName(barrier._before), barrier._before));
                    add_line("After", std::format("{} (0x{:X})", ResourceStateDisplayName(barrier._after), barrier._after));
                }
                add_line("SubResource", std::to_string(barrier._sub_resource));
                add_line("UAV", barrier._is_uav ? "Yes" : "No");
            }
        }
        else if (event._type == EFrameEventType::kRenderGraphPass)
        {
            u32 payload_idx = event._payload_index;
            if (payload_idx < _capture->RGPasses().size())
            {
                const auto &pass = _capture->RGPasses()[payload_idx];
                add_line("", "");
                add_line("--- Pass Info ---", "");
                add_line("Name", String(_capture->Strings().Get(pass._name)));
                add_line("Pass Type", std::to_string(pass._pass_type));
                add_line("Submission", std::to_string(pass._submission_index));
                add_line("Parallel Recording", pass._allow_parallel_recording ? "Yes" : "No");
                add_line("Inputs", std::to_string(pass._input_count));
                add_line("Outputs", std::to_string(pass._output_count));
                add_line("Pre Barriers", std::to_string(pass._pre_barrier_count));
                add_line("Post Barriers", std::to_string(pass._post_barrier_count));
                for (u16 i = 0u; i < pass._input_count && (pass._input_range_begin + i) < _capture->RGResourceAccesses().size(); ++i)
                {
                    const auto &access = _capture->RGResourceAccesses()[pass._input_range_begin + i];
                    String res_name(_capture->Strings().Get(access._resource_name));
                    add_line("  In " + std::to_string(i), std::format("{} usage=0x{:X} ver={}.{}", res_name, access._usage, access._handle_id, access._handle_version));
                }
                for (u16 i = 0u; i < pass._output_count && (pass._output_range_begin + i) < _capture->RGResourceAccesses().size(); ++i)
                {
                    const auto &access = _capture->RGResourceAccesses()[pass._output_range_begin + i];
                    String res_name(_capture->Strings().Get(access._resource_name));
                    add_line("  Out " + std::to_string(i), std::format("{} usage=0x{:X} ver={}.{}", res_name, access._usage, access._handle_id, access._handle_version));
                }
            }
        }

        auto *bottom_padding = _detail_content->AddChild<UI::Text>("");
        bottom_padding->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 12.0f));
    }

    void FrameDebuggerWindow::ClearBindingCacheDetails()
    {
        _binding_table_source->SetCapture(_capture);
        _binding_table_source->SetBindings(0u, 0u);
        _binding_table->Refresh();
        _binding_table->GetSlotAs<UI::LinearSlot>().Size({0.0f, 0.0f});

        _invalidation_table_source->SetCapture(_capture);
        _invalidation_table_source->SetBindings(0u, 0u);
        _invalidation_table->Refresh();
        _invalidation_table->GetSlotAs<UI::LinearSlot>().Size({0.0f, 0.0f});

        _diagnostic_content->ClearChildren();
        auto *row = _diagnostic_content->AddChild<UI::Text>("Select a draw or dispatch event to view its resource bindings.");
        row->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed)
            .Size({0.0f, 20.0f}).Margin(Padding(4.0f, 0.0f, 4.0f, 0.0f));
        row->_color = Color(0.55f, 0.58f, 0.65f, 1.0f);
    }

    void FrameDebuggerWindow::AddDiagnosticText(const String &text, Color color)
    {
        auto *row = _diagnostic_content->AddChild<UI::Text>(text);
        row->GetSlotAs<UI::LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed)
            .Size({0.0f, 20.0f}).Margin(Padding(4.0f, 0.0f, 4.0f, 0.0f));
        row->_color = color;
    }

    void FrameDebuggerWindow::RefreshBindingCacheDetails(u32 binding_range_begin, u16 binding_count)
    {
        _binding_table_source->SetCapture(_capture);
        _binding_table_source->SetBindings(binding_range_begin, binding_count);
        _binding_table->EnsureStyleResolved();
        _binding_table->Refresh();
        const f32 binding_row_count = static_cast<f32>(_binding_table_source->GetRowCount());
        const f32 binding_table_height = binding_row_count > 0.0f
            ? _binding_table->GetStyle()._header_height + binding_row_count * _binding_table->GetStyle()._row_height
            : 0.0f;
        _binding_table->GetSlotAs<UI::LinearSlot>().Size({0.0f, binding_table_height});

        _invalidation_table_source->SetCapture(_capture);
        _invalidation_table_source->SetBindings(binding_range_begin, binding_count);
        _invalidation_table->EnsureStyleResolved();
        _invalidation_table->Refresh();
        const f32 invalidation_row_count = static_cast<f32>(_invalidation_table_source->GetRowCount());
        const f32 invalidation_table_height = invalidation_row_count > 0.0f
            ? _invalidation_table->GetStyle()._header_height + invalidation_row_count * _invalidation_table->GetStyle()._row_height
            : 0.0f;
        _invalidation_table->GetSlotAs<UI::LinearSlot>().Size({0.0f, invalidation_table_height});

        if (binding_count == 0u)
            AddDiagnosticText("No bindings were recorded for this event.", Color(0.55f, 0.58f, 0.65f, 1.0f));
    }

} // namespace Ailu::Editor
