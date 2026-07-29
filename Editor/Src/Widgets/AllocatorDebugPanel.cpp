#include "Widgets/AllocatorDebugPanel.h"

#include "Ext/imgui/imgui.h"
#include "Ext/implot/implot.h"
#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/MemoryDebugService.h"

#include <algorithm>
#include <format>
#include <cstdio>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            const char *PageStateToString(EAllocatorPageState state)
            {
                switch (state)
                {
                    case EAllocatorPageState::kPartial: return "Partial";
                    case EAllocatorPageState::kFull: return "Full";
                    case EAllocatorPageState::kEmpty:
                    default: return "Empty";
                }
            }

            const char *BlockStateToString(EAllocatorBlockState state)
            {
                switch (state)
                {
                    case EAllocatorBlockState::kUsed: return "Used";
                    case EAllocatorBlockState::kPendingRemoteFree: return "Pending Remote Free";
                    case EAllocatorBlockState::kCorrupted: return "Corrupted";
                    case EAllocatorBlockState::kFree:
                    default: return "Free";
                }
            }

            const char *EventTypeToString(EMemoryEventType type)
            {
                switch (type)
                {
                    case EMemoryEventType::kAllocate: return "Alloc";
                    case EMemoryEventType::kFree: return "Free";
                    case EMemoryEventType::kRemoteFree: return "Remote Free";
                    case EMemoryEventType::kPageCreate: return "Page Create";
                    case EMemoryEventType::kPageRelease: return "Page Release";
                    case EMemoryEventType::kInvalidFree: return "Invalid Free";
                    case EMemoryEventType::kDoubleFree: return "Double Free";
                    case EMemoryEventType::kGuardCorruption: return "Guard Corruption";
                    case EMemoryEventType::kLeak: return "Leak";
                    default: return "Unknown";
                }
            }

            String FormatBytes(u64 bytes)
            {
                constexpr f64 kKb = 1024.0;
                constexpr f64 kMb = 1024.0 * 1024.0;
                constexpr f64 kGb = 1024.0 * 1024.0 * 1024.0;
                char buffer[64];
                if (bytes >= static_cast<u64>(kGb))
                    std::snprintf(buffer, sizeof(buffer), "%.2f GB", bytes / kGb);
                else if (bytes >= static_cast<u64>(kMb))
                    std::snprintf(buffer, sizeof(buffer), "%.2f MB", bytes / kMb);
                else if (bytes >= static_cast<u64>(kKb))
                    std::snprintf(buffer, sizeof(buffer), "%.2f KB", bytes / kKb);
                else
                    std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
                return buffer;
            }

            void DrawSummaryCard(const char *label, const String &value, const ImVec4 &value_color = ImGui::GetStyleColorVec4(ImGuiCol_Text))
            {
                ImGui::BeginGroup();
                ImGui::TextDisabled("%s", label);
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                ImGui::TextUnformatted(value.c_str());
                ImGui::PopStyleColor();
                ImGui::EndGroup();
            }

            bool MatchesSearch(const char *text, const char *search)
            {
                if (search == nullptr || search[0] == '\0')
                    return true;
                if (text == nullptr)
                    return false;
                String lower_text = text;
                String lower_search = search;
                std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return lower_text.find(lower_search) != String::npos;
            }

            bool EventMatchesFilters(const MemoryDebugEvent &event, const char *search, u64 thread_filter, u64 min_size, u64 max_size)
            {
                if (thread_filter != 0u && event._thread_id != thread_filter)
                    return false;
                if (event._requested_size < min_size || event._requested_size > max_size)
                    return false;
                return MatchesSearch(event._file, search) || MatchesSearch(event._function, search) || MatchesSearch(event._tag, search);
            }

            void DrawMemoryTrend(const Vector<MemoryTimelineSample> &timeline)
            {
                if (timeline.empty())
                {
                    ImGui::TextDisabled("No samples yet");
                    return;
                }

                static Vector<f64> s_time;
                static Vector<f64> s_requested;
                static Vector<f64> s_reserved;
                s_time.resize(timeline.size());
                s_requested.resize(timeline.size());
                s_reserved.resize(timeline.size());
                const f64 latest_time = timeline.back()._time_sec;
                for (u32 i = 0u; i < timeline.size(); ++i)
                {
                    s_time[i] = timeline[i]._time_sec - latest_time;
                    s_requested[i] = timeline[i]._requested_bytes / (1024.0 * 1024.0);
                    s_reserved[i] = timeline[i]._reserved_bytes / (1024.0 * 1024.0);
                }

                if (ImPlot::BeginPlot("Memory Trend", ImVec2(-1.0f, 210.0f)))
                {
                    ImPlot::SetupAxes("Seconds", "MB");
                    ImPlot::PlotLine("Requested Bytes", s_time.data(), s_requested.data(), static_cast<int>(s_time.size()));
                    ImPlot::PlotLine("Reserved Bytes", s_time.data(), s_reserved.data(), static_cast<int>(s_time.size()));
                    ImPlot::EndPlot();
                }
            }

            void DrawPageGrid(const AllocatorPageSnapshot &page)
            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                const ImVec2 start = ImGui::GetCursorScreenPos();
                const f32 cell_size = 9.0f;
                const f32 gap = 2.0f;
                const f32 avail_width = ImGui::GetContentRegionAvail().x;
                const u32 columns = std::max<u32>(1u, static_cast<u32>(avail_width / (cell_size + gap)));
                const u32 rows = (page._block_count + columns - 1u) / columns;

                for (u32 i = 0u; i < page._block_count; ++i)
                {
                    const u32 row = i / columns;
                    const u32 column = i % columns;
                    const ImVec2 min_pos(start.x + column * (cell_size + gap), start.y + row * (cell_size + gap));
                    const ImVec2 max_pos(min_pos.x + cell_size, min_pos.y + cell_size);
                    ImU32 color = IM_COL32(55, 135, 220, 255);
                    if (i < page._block_states.size())
                    {
                        if (page._block_states[i] == EAllocatorBlockState::kUsed)
                            color = IM_COL32(65, 178, 92, 255);
                        else if (page._block_states[i] == EAllocatorBlockState::kPendingRemoteFree)
                            color = IM_COL32(153, 92, 214, 255);
                        else if (page._block_states[i] == EAllocatorBlockState::kCorrupted)
                            color = IM_COL32(220, 70, 70, 255);
                    }
                    draw_list->AddRectFilled(min_pos, max_pos, color, 1.5f);
                    if (ImGui::IsMouseHoveringRect(min_pos, max_pos))
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Block: %u", i);
                        ImGui::Text("Address: 0x%llX", static_cast<unsigned long long>(page._block_begin + static_cast<u64>(i) * page._block_size));
                        ImGui::Text("State: %s", i < page._block_states.size() ? BlockStateToString(page._block_states[i]) : "Unknown");
                        ImGui::EndTooltip();
                    }
                }
                ImGui::Dummy(ImVec2(avail_width, rows * (cell_size + gap)));
            }

            f32 ClampPanelSize(f32 value, f32 total_size, f32 min_primary, f32 min_secondary)
            {
                if (total_size <= min_primary + min_secondary)
                    return std::max(1.0f, total_size * 0.5f);
                return std::clamp(value, min_primary, total_size - min_secondary);
            }

            f32 ClampIndependentPanelSize(f32 value, f32 min_size, f32 max_size)
            {
                return std::clamp(value, min_size, max_size);
            }

            void DrawVerticalSplitter(const char *id, f32 &left_width, f32 total_width, f32 height, f32 min_left, f32 min_right)
            {
                constexpr f32 kSplitterSize = 6.0f;
                left_width = ClampPanelSize(left_width, total_width - kSplitterSize, min_left, min_right);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::InvisibleButton(id, ImVec2(kSplitterSize, height));
                if (ImGui::IsItemActive())
                    left_width = ClampPanelSize(left_width + ImGui::GetIO().MouseDelta.x, total_width - kSplitterSize, min_left, min_right);
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                const ImVec2 min_pos = ImGui::GetItemRectMin();
                const ImVec2 max_pos = ImGui::GetItemRectMax();
                const ImU32 color = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive :
                                                       ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
                ImGui::GetWindowDrawList()->AddRectFilled(min_pos, max_pos, color);
                ImGui::SameLine(0.0f, 0.0f);
            }

            void DrawHorizontalSplitter(const char *id, f32 &height, f32 min_height, f32 max_height)
            {
                constexpr f32 kSplitterSize = 6.0f;
                height = ClampIndependentPanelSize(height, min_height, max_height);
                ImGui::InvisibleButton(id, ImVec2(-1.0f, kSplitterSize));
                if (ImGui::IsItemActive())
                    height = ClampIndependentPanelSize(height + ImGui::GetIO().MouseDelta.y, min_height, max_height);
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                const ImVec2 min_pos = ImGui::GetItemRectMin();
                const ImVec2 max_pos = ImGui::GetItemRectMax();
                const ImU32 color = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive :
                                                       ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
                ImGui::GetWindowDrawList()->AddRectFilled(min_pos, max_pos, color);
            }
        }

        void ShowAllocatorDebugPanelWindow(bool *is_show)
        {
            if (is_show != nullptr && !*is_show)
                return;

            auto &service = MemoryDebugService::Get();
            static bool s_track_leaks = true;
            static bool s_show_empty_pages = true;
            static bool s_show_system_allocs = true;
            static char s_search[256] = {};
            static u64 s_thread_filter = 0u;
            static u64 s_min_size = 0u;
            static u64 s_max_size = 64u * 1024u;
            static u32 s_selected_bin_index = kInvalidAllocatorIndex;
            static u64 s_selected_page_address = 0u;

            ImGui::SetNextWindowSize(ImVec2(1280.0f, 780.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Allocator Debug", is_show))
            {
                ImGui::End();
                return;
            }

            const AllocatorSnapshot &snapshot = service.CurrentSnapshot();
            bool paused = service.IsPaused();
            if (ImGui::Button(paused ? "Resume" : "Pause"))
            {
                paused = !paused;
                service.SetPaused(paused);
            }
            ImGui::SameLine();
            if (ImGui::Button("Snapshot"))
                service.CreateSnapshot();
            ImGui::SameLine();
            if (ImGui::Button("Clear Events"))
                service.ClearEvents();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##AllocatorSearch", "Search file, function, tag", s_search, sizeof(s_search));
            ImGui::SameLine();
            if (ImGui::Checkbox("Track Leaks", &s_track_leaks))
                service.SetTrackLeaks(s_track_leaks);
            ImGui::SameLine();
            ImGui::Checkbox("Show Empty Pages", &s_show_empty_pages);
            ImGui::SameLine();
            ImGui::Checkbox("Show System Allocs", &s_show_system_allocs);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const char *refresh_labels[] = {"1 Hz", "5 Hz", "10 Hz", "Every Frame"};
            static i32 s_refresh_index = 1;
            if (ImGui::Combo("Refresh", &s_refresh_index, refresh_labels, IM_ARRAYSIZE(refresh_labels)))
            {
                const f32 intervals[] = {1.0f, 0.2f, 0.1f, 0.0f};
                service.SetRefreshInterval(intervals[s_refresh_index]);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170.0f);
            const String thread_preview = s_thread_filter == 0u ? String("All Threads") : std::format("Thread {}", s_thread_filter);
            if (ImGui::BeginCombo("Thread", thread_preview.c_str()))
            {
                if (ImGui::Selectable("All Threads", s_thread_filter == 0u))
                    s_thread_filter = 0u;
                for (const auto &arena: snapshot._arenas)
                {
                    const bool selected = s_thread_filter == arena._thread_id;
                    const String label = std::format("Arena {} / {}", arena._arena_id, arena._thread_id);
                    if (ImGui::Selectable(label.c_str(), selected))
                        s_thread_filter = arena._thread_id;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputScalar("Min Size", ImGuiDataType_U64, &s_min_size);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputScalar("Max Size", ImGuiDataType_U64, &s_max_size);

            const auto &global = snapshot._global;
            ImGui::Separator();
            const f64 fragmentation = global._reserved_bytes > 0u ? global._reserved_bytes / static_cast<f64>(std::max<u64>(global._requested_bytes, 1u)) : 0.0;
            static f32 s_summary_height = 68.0f;
            static f32 s_top_row_height = 270.0f;
            static f32 s_middle_row_height = 260.0f;
            static f32 s_trend_width = 480.0f;
            static f32 s_page_width = 560.0f;
            static f32 s_events_width = 680.0f;
            constexpr f32 kSplitterSize = 6.0f;
            const ImVec2 layout_avail = ImGui::GetContentRegionAvail();
            const f32 layout_width = std::max(1.0f, layout_avail.x);
            static f32 s_bottom_row_height = 220.0f;
            s_summary_height = ClampIndependentPanelSize(s_summary_height, 48.0f, 180.0f);
            s_top_row_height = ClampIndependentPanelSize(s_top_row_height, 140.0f, 720.0f);
            s_middle_row_height = ClampIndependentPanelSize(s_middle_row_height, 130.0f, 720.0f);
            s_bottom_row_height = ClampIndependentPanelSize(s_bottom_row_height, 110.0f, 720.0f);
            s_trend_width = ClampPanelSize(s_trend_width, layout_width - kSplitterSize, 260.0f, 360.0f);
            s_page_width = ClampPanelSize(s_page_width, layout_width - kSplitterSize, 300.0f, 320.0f);
            s_events_width = ClampPanelSize(s_events_width, layout_width - kSplitterSize, 360.0f, 260.0f);

            ImGui::BeginChild("AllocatorSummaryChild", ImVec2(-1.0f, s_summary_height), true);
            DrawSummaryCard("Live Requested", FormatBytes(global._requested_bytes));
            ImGui::SameLine();
            DrawSummaryCard("Live Reserved", FormatBytes(global._reserved_bytes));
            ImGui::SameLine();
            DrawSummaryCard("Peak Requested", FormatBytes(global._peak_requested_bytes));
            ImGui::SameLine();
            DrawSummaryCard("Active Allocs", std::to_string(global._active_allocation_count));
            ImGui::SameLine();
            DrawSummaryCard("Page Count", std::to_string(global._page_count));
            ImGui::SameLine();
            DrawSummaryCard("Cached Empty Pages", std::to_string(global._cached_empty_page_count));
            ImGui::SameLine();
            DrawSummaryCard("System Allocs", std::to_string(global._system_allocation_count));
            ImGui::SameLine();
            DrawSummaryCard("Leaks", std::to_string(snapshot._allocations.size()), snapshot._allocations.empty() ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            ImGui::SameLine();
            DrawSummaryCard("Fragmentation", std::format("{:.2f}x", fragmentation));
            ImGui::EndChild();

            DrawHorizontalSplitter("##AllocatorSummaryTopSplitter", s_summary_height, 48.0f, 180.0f);
            ImGui::BeginChild("AllocatorTrendChild", ImVec2(s_trend_width, s_top_row_height), true);
            DrawMemoryTrend(service.Timeline());
            ImGui::EndChild();
            DrawVerticalSplitter("##AllocatorTrendBinSplitter", s_trend_width, layout_width, s_top_row_height, 260.0f, 360.0f);
            ImGui::BeginChild("AllocatorBinChild", ImVec2(0.0f, s_top_row_height), true);
            if (ImGui::BeginTable("AllocatorBinTable", 10, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Bin Size");
                ImGui::TableSetupColumn("Partial");
                ImGui::TableSetupColumn("Full");
                ImGui::TableSetupColumn("Empty");
                ImGui::TableSetupColumn("Active");
                ImGui::TableSetupColumn("Free");
                ImGui::TableSetupColumn("Requested");
                ImGui::TableSetupColumn("Capacity");
                ImGui::TableSetupColumn("Reserved");
                ImGui::TableSetupColumn("Fragment");
                ImGui::TableHeadersRow();
                for (const auto &bin: snapshot._bins)
                {
                    if (!s_show_empty_pages && bin._active_block_count == 0u && bin._empty_page_count > 0u)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool selected = s_selected_bin_index == bin._bin_index;
                    if (ImGui::Selectable(std::to_string(bin._block_size).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        s_selected_bin_index = bin._bin_index;
                        s_selected_page_address = bin._page_addresses.empty() ? 0u : bin._page_addresses.front();
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", bin._partial_page_count);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", bin._full_page_count);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", bin._empty_page_count);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu", static_cast<unsigned long long>(bin._active_block_count));
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%llu", static_cast<unsigned long long>(bin._free_block_count));
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(FormatBytes(bin._requested_bytes).c_str());
                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextUnformatted(FormatBytes(bin._capacity_bytes).c_str());
                    ImGui::TableSetColumnIndex(8);
                    ImGui::TextUnformatted(FormatBytes(bin._reserved_bytes).c_str());
                    ImGui::TableSetColumnIndex(9);
                    const f64 bin_fragment = bin._requested_bytes > 0u ? bin._capacity_bytes / static_cast<f64>(bin._requested_bytes) : 0.0;
                    ImGui::Text("%.2fx", bin_fragment);
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            DrawHorizontalSplitter("##AllocatorTopMiddleSplitter", s_top_row_height, 140.0f, 720.0f);
            ImGui::BeginChild("AllocatorPageChild", ImVec2(s_page_width, s_middle_row_height), true);
            ImGui::TextUnformatted("Page Detail");
            if (s_selected_bin_index < snapshot._bins.size())
            {
                const auto &bin = snapshot._bins[s_selected_bin_index];
                if (!bin._page_addresses.empty())
                {
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##PageAddress", std::format("0x{:016X}", s_selected_page_address).c_str()))
                    {
                        for (u64 address: bin._page_addresses)
                        {
                            const bool selected = address == s_selected_page_address;
                            if (ImGui::Selectable(std::format("0x{:016X}", address).c_str(), selected))
                                s_selected_page_address = address;
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            AllocatorPageSnapshot page;
            if (s_selected_page_address != 0u && Allocator::Get().CapturePageSnapshot(s_selected_page_address, page))
            {
                ImGui::Text("State: %s  Bin: %u  Block Size: %u", PageStateToString(page._state), page._bin_index, page._block_size);
                ImGui::Text("Used: %u  Free: %u  Blocks: %u", page._used_count, page._free_count, page._block_count);
                ImGui::Text("Arena: %u  Thread: %llu", page._arena_id, static_cast<unsigned long long>(page._thread_id));
                ImGui::Text("Range: 0x%llX - 0x%llX", static_cast<unsigned long long>(page._block_begin), static_cast<unsigned long long>(page._block_end));
                DrawPageGrid(page);
            }
            else
            {
                ImGui::TextDisabled("Select a Bin row with pages to inspect block occupancy.");
            }
            ImGui::EndChild();
            DrawVerticalSplitter("##AllocatorPageArenaSplitter", s_page_width, layout_width, s_middle_row_height, 300.0f, 320.0f);
            ImGui::BeginChild("AllocatorArenaChild", ImVec2(0.0f, s_middle_row_height), true);
            if (ImGui::BeginTable("AllocatorArenaTable", 8, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Arena");
                ImGui::TableSetupColumn("Thread");
                ImGui::TableSetupColumn("Allocs");
                ImGui::TableSetupColumn("Frees");
                ImGui::TableSetupColumn("Pages");
                ImGui::TableSetupColumn("Requested");
                ImGui::TableSetupColumn("Reserved");
                ImGui::TableSetupColumn("Peak");
                ImGui::TableHeadersRow();
                for (const auto &arena: snapshot._arenas)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", arena._arena_id);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", static_cast<unsigned long long>(arena._thread_id));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(arena._allocation_count));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", static_cast<unsigned long long>(arena._free_count));
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu", static_cast<unsigned long long>(arena._page_count));
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(FormatBytes(arena._requested_bytes).c_str());
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(FormatBytes(arena._reserved_bytes).c_str());
                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextUnformatted(FormatBytes(arena._peak_requested_bytes).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            DrawHorizontalSplitter("##AllocatorMiddleBottomSplitter", s_middle_row_height, 130.0f, 720.0f);
            ImGui::BeginChild("AllocatorEventChild", ImVec2(s_events_width, s_bottom_row_height), true);
            if (ImGui::BeginTable("AllocatorEventTable", 10, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Seq");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Thread");
                ImGui::TableSetupColumn("Arena");
                ImGui::TableSetupColumn("Size");
                ImGui::TableSetupColumn("Align");
                ImGui::TableSetupColumn("Address");
                ImGui::TableSetupColumn("File");
                ImGui::TableSetupColumn("Line");
                ImGui::TableSetupColumn("Tag");
                ImGui::TableHeadersRow();
                for (auto it = service.RecentEvents().rbegin(); it != service.RecentEvents().rend(); ++it)
                {
                    const MemoryDebugEvent &event = *it;
                    if (!EventMatchesFilters(event, s_search, s_thread_filter, s_min_size, s_max_size))
                        continue;
                    if (!s_show_system_allocs && event._bin_index == kInvalidAllocatorIndex)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu", static_cast<unsigned long long>(event._sequence));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(EventTypeToString(event._type));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(event._thread_id));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", event._arena_id);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(FormatBytes(event._requested_size).c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%u", event._alignment);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("0x%llX", static_cast<unsigned long long>(event._address));
                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextUnformatted(event._file != nullptr ? event._file : "");
                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text("%u", event._line);
                    ImGui::TableSetColumnIndex(9);
                    ImGui::TextUnformatted(event._tag != nullptr ? event._tag : "");
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            DrawVerticalSplitter("##AllocatorEventErrorSplitter", s_events_width, layout_width, s_bottom_row_height, 360.0f, 260.0f);
            ImGui::BeginChild("AllocatorErrorChild", ImVec2(0.0f, s_bottom_row_height), true);
            ImGui::TextUnformatted("Leaks and Errors");
            ImGui::Separator();
            ImGui::Text("Leaks: %llu", static_cast<unsigned long long>(snapshot._allocations.size()));
            ImGui::Text("Double Free: %llu", static_cast<unsigned long long>(global._double_free_count));
            ImGui::Text("Invalid Free: %llu", static_cast<unsigned long long>(global._invalid_free_count));
            ImGui::Text("Guard Corruption: %llu", static_cast<unsigned long long>(global._guard_corruption_count));
            ImGui::Text("Dropped Events: %llu", static_cast<unsigned long long>(global._dropped_event_count));
            ImGui::Separator();
            const u32 leak_count = static_cast<u32>(std::min<size_t>(snapshot._allocations.size(), 64u));
            for (u32 i = 0u; i < leak_count; ++i)
            {
                const auto &allocation = snapshot._allocations[i];
                if (!MatchesSearch(allocation._file, s_search) && !MatchesSearch(allocation._function, s_search) &&
                    !MatchesSearch(allocation._tag, s_search))
                    continue;
                ImGui::Text("0x%llX  %s  %s:%u", static_cast<unsigned long long>(allocation._address),
                            FormatBytes(allocation._requested_size).c_str(), allocation._file ? allocation._file : "", allocation._line);
            }
            ImGui::EndChild();

            ImGui::End();
        }
    }
}
