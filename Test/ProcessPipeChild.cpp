#include <Windows.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using u64 = unsigned long long;

namespace
{
    void WriteBytes(DWORD standard_handle, const std::string &bytes)
    {
        HANDLE handle = ::GetStdHandle(standard_handle);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0u;
        ::WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }

    bool HasFlag(int argc, wchar_t **argv, const wchar_t *flag)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (std::wstring_view(argv[index]) == flag)
                return true;
        }
        return false;
    }

    u64 ParseValue(int argc, wchar_t **argv, const wchar_t *name, u64 default_value)
    {
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (std::wstring_view(argv[index]) == name)
                return std::stoull(argv[index + 1]);
        }
        return default_value;
    }

    void SpawnDescendant()
    {
        wchar_t path_buffer[MAX_PATH]{};
        const DWORD path_length = ::GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
        if (path_length == 0u || path_length >= MAX_PATH)
            return;
        std::wstring command = L"\"" + std::wstring(path_buffer, path_length) + L"\" --child --sleep-ms 30000";
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        if (::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                              &startup_info, &process_info))
        {
            ::CloseHandle(process_info.hThread);
            ::CloseHandle(process_info.hProcess);
        }
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (HasFlag(argc, argv, L"--spawn-child"))
        SpawnDescendant();

    const u64 stdout_lines = ParseValue(argc, argv, L"--stdout-lines", 0u);
    const u64 stderr_lines = ParseValue(argc, argv, L"--stderr-lines", 0u);
    const u64 long_line_size = ParseValue(argc, argv, L"--long-line", 0u);
    const u64 sleep_ms = ParseValue(argc, argv, L"--sleep-ms", 0u);
    const u64 exit_code = ParseValue(argc, argv, L"--exit-code", 0u);
    const u64 probe_handle_value = ParseValue(argc, argv, L"--probe-inherited-handle", 0u);
    const bool unicode = HasFlag(argc, argv, L"--unicode");
    if (HasFlag(argc, argv, L"--check-standard-output"))
    {
        const HANDLE output_handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD bytes_written = 0u;
        const bool valid_handle = output_handle != nullptr && output_handle != INVALID_HANDLE_VALUE;
        const bool is_pipe = valid_handle && ::GetFileType(output_handle) == FILE_TYPE_PIPE;
        const bool write_succeeded =
            valid_handle && ::WriteFile(output_handle, "standard-output-check\n", 22u, &bytes_written, nullptr);
        if (!valid_handle)
            return 201;
        if (!is_pipe)
            return 202;
        if (!write_succeeded)
            return 203;
        return bytes_written == 22u ? 0 : 204;
    }
    const std::string unicode_bytes = "\xE4\xB8\xAD\xE6\x96\x87";

    if (probe_handle_value != 0u)
    {
        const HANDLE probe_handle = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(probe_handle_value));
        DWORD handle_flags = 0u;
        const bool inherited = ::GetHandleInformation(probe_handle, &handle_flags) != FALSE;
        WriteBytes(STD_OUTPUT_HANDLE, inherited ? "probe-handle-inherited\n" : "probe-handle-not-inherited\n");
    }

    if (long_line_size != 0u)
    {
        std::string line(static_cast<size_t>(long_line_size), 'L');
        if (unicode && line.size() >= unicode_bytes.size())
            std::copy(unicode_bytes.begin(), unicode_bytes.end(), line.begin());
        WriteBytes(STD_OUTPUT_HANDLE, line + '\n');
        WriteBytes(STD_ERROR_HANDLE, line + '\n');
    }
    for (u64 index = 0u; index < stdout_lines; ++index)
        WriteBytes(STD_OUTPUT_HANDLE, "stdout-line-" + std::to_string(index) + '\n');
    for (u64 index = 0u; index < stderr_lines; ++index)
        WriteBytes(STD_ERROR_HANDLE, "stderr-line-" + std::to_string(index) + '\n');
    if (unicode)
    {
        WriteBytes(STD_OUTPUT_HANDLE, unicode_bytes + '\n');
        WriteBytes(STD_ERROR_HANDLE, unicode_bytes + '\n');
    }
    if (sleep_ms != 0u)
        ::Sleep(static_cast<DWORD>(std::min<u64>(sleep_ms, 0xFFFFFFFFu)));
    return static_cast<int>(exit_code & 0xFFu);
}
