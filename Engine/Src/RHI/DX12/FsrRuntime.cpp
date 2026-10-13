#include "pch.h"

#include "RHI/DX12/FsrRuntime.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/SystemInfo.h"

#include <type_traits>
#include <unordered_map>
#include <vector>

#if AILU_WITH_FSR
#include <d3d12.h>
#include <dx12/ffx_api_dx12.h>
#include <softpub.h>
#include <wintrust.h>
#include <wrl/client.h>

#include <ffx_api.h>
#include <ffx_upscale.h>
#endif

namespace Ailu::RHI::DX12
{
#if AILU_WITH_FSR
    namespace
    {
        constexpr char kLoaderDllName[] = "amd_fidelityfx_loader_dx12.dll";
        constexpr char kUpscalerDllName[] = "amd_fidelityfx_upscaler_dx12.dll";
        constexpr char kTargetProviderVersion[] = "3.1.5";

        void FsrRuntimeModuleAnchor() {}

        bool VerifySignature(const std::filesystem::path& path)
        {
            WINTRUST_FILE_INFO file_info{};
            file_info.cbStruct = sizeof(file_info);
            file_info.pcwszFilePath = path.c_str();

            WINTRUST_DATA trust_data{};
            trust_data.cbStruct = sizeof(trust_data);
            trust_data.dwUIChoice = WTD_UI_NONE;
            trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
            trust_data.dwUnionChoice = WTD_CHOICE_FILE;
            trust_data.pFile = &file_info;
            trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
            trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;

            GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            const LONG result = WinVerifyTrust(nullptr, &policy, &trust_data);
            trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(nullptr, &policy, &trust_data);
            return result == ERROR_SUCCESS;
        }

        String FileVersion(const std::filesystem::path& path)
        {
            const DWORD data_size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
            if (data_size == 0u)
                return "unknown";

            Vector<u8> data(data_size);
            if (!GetFileVersionInfoW(path.c_str(), 0u, data_size, data.data()))
                return "unknown";

            VS_FIXEDFILEINFO* version = nullptr;
            UINT version_size = 0u;
            if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&version), &version_size) ||
                version == nullptr || version_size < sizeof(VS_FIXEDFILEINFO))
                return "unknown";

            return std::format("{}.{}.{}.{}", HIWORD(version->dwFileVersionMS), LOWORD(version->dwFileVersionMS),
                               HIWORD(version->dwFileVersionLS), LOWORD(version->dwFileVersionLS));
        }

        String ReturnCodeName(ffxReturnCode_t result)
        {
            switch (result)
            {
            case FFX_API_RETURN_OK: return "ok";
            case FFX_API_RETURN_ERROR: return "error";
            case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "unknown descriptor type";
            case FFX_API_RETURN_ERROR_RUNTIME_ERROR: return "runtime error";
            case FFX_API_RETURN_NO_PROVIDER: return "no provider";
            case FFX_API_RETURN_ERROR_MEMORY: return "out of memory";
            case FFX_API_RETURN_ERROR_PARAMETER: return "invalid parameter";
            case FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE: return "provider does not support descriptor";
            default: return std::format("unknown result {}", result);
            }
        }

        bool IsTargetProvider(const char* name)
        {
            if (name == nullptr)
                return false;
            const String version_name = name;
            return version_name.find(kTargetProviderVersion) != String::npos;
        }
    }
#endif

    struct FsrRuntime::Impl
    {
#if AILU_WITH_FSR
        using CreateContextFn = PfnFfxCreateContext;
        using DestroyContextFn = PfnFfxDestroyContext;
        using DispatchFn = PfnFfxDispatch;
        using QueryFn = PfnFfxQuery;
        using ConfigureFn = PfnFfxConfigure;

        DLL_DIRECTORY_COOKIE _directory_cookie = nullptr;
        HMODULE _upscaler_module = nullptr;
        HMODULE _loader_module = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Device> _device;
        CreateContextFn _create_context = nullptr;
        DestroyContextFn _destroy_context = nullptr;
        DispatchFn _dispatch = nullptr;
        QueryFn _query = nullptr;
        ConfigureFn _configure = nullptr;
        struct UpscaleContext
        {
            ffxContext _context{};
            std::uint32_t _render_width = 0u;
            std::uint32_t _render_height = 0u;
            std::uint32_t _output_width = 0u;
            std::uint32_t _output_height = 0u;
            std::uint64_t _reset_revision = 0u;
            bool _has_dispatched = false;
        };
        std::unordered_map<std::uint64_t, UpscaleContext> _contexts;
        std::vector<ffxContext> _retired_contexts;
#endif
        bool _supported = false;
        std::uint64_t _provider_id = 0u;
        std::string _provider_version;
        std::string _error;
    };

    FsrRuntime::FsrRuntime() : _impl(std::make_unique<Impl>()) {}

    FsrRuntime::~FsrRuntime() { Shutdown(); }

    bool FsrRuntime::Init(void* native_device)
    {
        if (_impl->_supported)
            return true;
        Shutdown();
        _impl->_error.clear();

#if !AILU_WITH_FSR
        (void) native_device;
        _impl->_error = "FSR support is disabled at build time.";
        LOG_INFO("FSR: disabled at build time.");
        return false;
#else
        if (native_device == nullptr)
        {
            _impl->_error = "DX12 device is null.";
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            return false;
        }
        _impl->_device = static_cast<ID3D12Device*>(native_device);

        HMODULE engine_module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&FsrRuntimeModuleAnchor), &engine_module))
        {
            _impl->_error = std::format("failed to locate the Engine module (error={}).", GetLastError());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        wchar_t module_path[MAX_PATH]{};
        const DWORD module_path_length = GetModuleFileNameW(engine_module, module_path, _countof(module_path));
        if (module_path_length == 0u || module_path_length >= _countof(module_path))
        {
            _impl->_error = std::format("failed to resolve the Engine module path (error={}).", GetLastError());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        const std::filesystem::path runtime_dir = std::filesystem::path(module_path).parent_path() / L"FSR";
        const std::filesystem::path loader_path = runtime_dir / kLoaderDllName;
        const std::filesystem::path upscaler_path = runtime_dir / kUpscalerDllName;
        if (!std::filesystem::exists(loader_path) || !std::filesystem::exists(upscaler_path))
        {
            _impl->_error = std::format("SDK 2.3.0 runtime is incomplete in {}.", runtime_dir.string());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }
        if (!VerifySignature(loader_path) || !VerifySignature(upscaler_path))
        {
            _impl->_error = std::format("Authenticode verification failed for the SDK DLLs in {}.", runtime_dir.string());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        _impl->_directory_cookie = AddDllDirectory(runtime_dir.c_str());
        if (_impl->_directory_cookie == nullptr)
        {
            _impl->_error = std::format("failed to register SDK DLL directory (error={}).", GetLastError());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        _impl->_upscaler_module = LoadLibraryExW(upscaler_path.c_str(), nullptr,
                                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (_impl->_upscaler_module == nullptr)
        {
            _impl->_error = std::format("failed to load {} (error={}).", upscaler_path.string(), GetLastError());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }
        _impl->_loader_module = LoadLibraryExW(loader_path.c_str(), nullptr,
                                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (_impl->_loader_module == nullptr)
        {
            _impl->_error = std::format("failed to load {} (error={}).", loader_path.string(), GetLastError());
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        auto load_function = [this](const char* name, auto& function)
        {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                GetProcAddress(_impl->_loader_module, name));
            if (function == nullptr)
                _impl->_error = std::format("required SDK export '{}' is missing (error={}).", name, GetLastError());
            return function != nullptr;
        };
        if (!load_function("ffxCreateContext", _impl->_create_context) ||
            !load_function("ffxDestroyContext", _impl->_destroy_context) ||
            !load_function("ffxDispatch", _impl->_dispatch) || !load_function("ffxQuery", _impl->_query) ||
            !load_function("ffxConfigure", _impl->_configure))
        {
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        ffxQueryDescGetVersions version_query{};
        version_query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        version_query.header.pNext = nullptr;
        version_query.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        version_query.device = _impl->_device.Get();
        std::uint64_t version_count = 0u;
        version_query.outputCount = &version_count;
        ffxReturnCode_t result = _impl->_query(nullptr, &version_query.header);
        if (result != FFX_API_RETURN_OK || version_count == 0u)
        {
            _impl->_error = std::format("upscaler provider query failed ({}).", ReturnCodeName(result));
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        Vector<std::uint64_t> version_ids(static_cast<size_t>(version_count));
        Vector<const char*> version_names(static_cast<size_t>(version_count), nullptr);
        version_query.outputCount = &version_count;
        version_query.versionIds = version_ids.data();
        version_query.versionNames = version_names.data();
        result = _impl->_query(nullptr, &version_query.header);
        if (result != FFX_API_RETURN_OK)
        {
            _impl->_error = std::format("upscaler version enumeration failed ({}).", ReturnCodeName(result));
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        for (size_t i = 0u; i < std::min(static_cast<size_t>(version_count), version_ids.size()); ++i)
        {
            if (!IsTargetProvider(version_names[i]))
                continue;
            _impl->_provider_id = version_ids[i];
            _impl->_provider_version = version_names[i];
            break;
        }
        if (_impl->_provider_id == 0u)
        {
            _impl->_error = "the DX12 device does not expose the FSR 3.1.5 upscaler provider.";
            LOG_WARNING("FSR: unavailable ({}).", _impl->_error);
            Shutdown();
            return false;
        }

        _impl->_supported = true;
        LOG_INFO("FSR: SDK 2.3.0 available at {}; loader file version {}, upscaler file version {}; provider '{}' (id={}) supported.",
                 runtime_dir.string(), FileVersion(loader_path), FileVersion(upscaler_path),
                 _impl->_provider_version, _impl->_provider_id);
        return true;
#endif
    }

    void FsrRuntime::Shutdown()
    {
#if AILU_WITH_FSR
        if (_impl->_destroy_context != nullptr)
        {
            for (auto& [camera_id, context] : _impl->_contexts)
            {
                (void) camera_id;
                _impl->_destroy_context(&context._context, nullptr);
            }
            for (auto& context : _impl->_retired_contexts)
                _impl->_destroy_context(&context, nullptr);
        }
        _impl->_contexts.clear();
        _impl->_retired_contexts.clear();
        if (_impl->_loader_module != nullptr)
        {
            FreeLibrary(_impl->_loader_module);
            _impl->_loader_module = nullptr;
        }
        if (_impl->_upscaler_module != nullptr)
        {
            FreeLibrary(_impl->_upscaler_module);
            _impl->_upscaler_module = nullptr;
        }
        if (_impl->_directory_cookie != nullptr)
        {
            RemoveDllDirectory(_impl->_directory_cookie);
            _impl->_directory_cookie = nullptr;
        }
        _impl->_device.Reset();
        _impl->_create_context = nullptr;
        _impl->_destroy_context = nullptr;
        _impl->_dispatch = nullptr;
        _impl->_query = nullptr;
        _impl->_configure = nullptr;
#endif
        _impl->_supported = false;
        _impl->_provider_id = 0u;
        _impl->_provider_version.clear();
    }

    bool FsrRuntime::IsSupported() const { return _impl->_supported; }

    const std::string& FsrRuntime::GetError() const { return _impl->_error; }

    const std::string& FsrRuntime::GetProviderVersion() const { return _impl->_provider_version; }

    std::uint64_t FsrRuntime::GetProviderId() const { return _impl->_provider_id; }

    bool FsrRuntime::GetRenderSize(std::uint32_t output_width, std::uint32_t output_height,
                                          std::uint32_t quality_mode,
                                          std::uint32_t& render_width, std::uint32_t& render_height) const
    {
#if !AILU_WITH_FSR
        (void) output_width;
        (void) output_height;
        (void) quality_mode;
        (void) render_width;
        (void) render_height;
        return false;
#else
        if (!_impl->_supported || output_width == 0u || output_height == 0u)
            return false;
        ffxQueryDescUpscaleGetRenderResolutionFromQualityMode query{};
        query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
        query.displayWidth = output_width;
        query.displayHeight = output_height;
        if (quality_mode < FFX_UPSCALE_QUALITY_MODE_QUALITY ||
            quality_mode > FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE)
            return false;
        query.qualityMode = quality_mode;
        query.pOutRenderWidth = &render_width;
        query.pOutRenderHeight = &render_height;
        return _impl->_query(nullptr, &query.header) == FFX_API_RETURN_OK && render_width > 0u && render_height > 0u;
#endif
    }

    bool FsrRuntime::Evaluate(void* command_list, void* color, void* depth, void* motion_vectors, void* output,
                              std::uint64_t camera_id, std::uint64_t reset_revision,
                              std::uint32_t render_width, std::uint32_t render_height,
                              std::uint32_t output_width, std::uint32_t output_height,
                              float jitter_x, float jitter_y, float frame_time_ms,
                              float camera_near, float camera_far, float camera_fov_vertical)
    {
#if !AILU_WITH_FSR
        (void) command_list; (void) color; (void) depth; (void) motion_vectors; (void) output;
        (void) camera_id; (void) reset_revision; (void) render_width; (void) render_height;
        (void) output_width; (void) output_height; (void) jitter_x; (void) jitter_y;
        (void) frame_time_ms; (void) camera_near; (void) camera_far; (void) camera_fov_vertical;
        return false;
#else
        if (!_impl->_supported || command_list == nullptr || color == nullptr || depth == nullptr ||
            motion_vectors == nullptr || output == nullptr || render_width == 0u || render_height == 0u ||
            output_width == 0u || output_height == 0u)
            return false;

        auto& context = _impl->_contexts[camera_id];
        const bool resize = context._render_width != render_width || context._render_height != render_height ||
                            context._output_width != output_width || context._output_height != output_height;
        if (resize && context._context != nullptr)
        {
            _impl->_retired_contexts.push_back(context._context);
            context = {};
        }
        if (context._context == nullptr)
        {
            ffxCreateContextDescUpscale upscale{};
            upscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            upscale.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
            if (SystemInfo::kReverseZ)
                upscale.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
            upscale.maxRenderSize = {render_width, render_height};
            upscale.maxUpscaleSize = {output_width, output_height};

            ffxCreateBackendDX12Desc backend{};
            backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            backend.device = _impl->_device.Get();

            ffxOverrideVersion version_override{};
            version_override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
            version_override.versionId = _impl->_provider_id;

            ffxCreateContextDescUpscaleVersion version{};
            version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
            version.version = FFX_UPSCALER_VERSION;

            // The effect descriptor must be the chain head; backend/version descriptors only extend it.
            upscale.header.pNext = &backend.header;
            backend.header.pNext = &version_override.header;
            version_override.header.pNext = &version.header;
            const auto create_result = _impl->_create_context(&context._context, &upscale.header, nullptr);
            if (create_result != FFX_API_RETURN_OK)
            {
                _impl->_contexts.erase(camera_id);
                LOG_WARNING("FSR: context creation failed for camera {} ({}).", camera_id,
                            ReturnCodeName(create_result));
                return false;
            }
            context._render_width = render_width;
            context._render_height = render_height;
            context._output_width = output_width;
            context._output_height = output_height;
        }

        ffxDispatchDescUpscale dispatch{};
        dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList = command_list;
        dispatch.color = ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(color), FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatch.depth = ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(depth), FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatch.motionVectors = ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(motion_vectors),
                                                       FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatch.output = ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(output),
                                                FFX_API_RESOURCE_STATE_UNORDERED_ACCESS,
                                                FFX_API_RESOURCE_USAGE_UAV);
        dispatch.jitterOffset = {jitter_x, jitter_y};
        dispatch.motionVectorScale = {-static_cast<float>(render_width), -static_cast<float>(render_height)};
        dispatch.renderSize = {render_width, render_height};
        dispatch.upscaleSize = {output_width, output_height};
        dispatch.frameTimeDelta = std::clamp(frame_time_ms, 1.0f, 100.0f);
        dispatch.preExposure = 1.0f;
        dispatch.reset = !context._has_dispatched || context._reset_revision != reset_revision || resize;
        dispatch.cameraNear = camera_near;
        dispatch.cameraFar = camera_far;
        dispatch.cameraFovAngleVertical = camera_fov_vertical;
        dispatch.viewSpaceToMetersFactor = 1.0f;
        const auto dispatch_result = _impl->_dispatch(&context._context, &dispatch.header);
        if (dispatch_result != FFX_API_RETURN_OK)
        {
            LOG_WARNING("FSR: dispatch failed for camera {} ({}).", camera_id, ReturnCodeName(dispatch_result));
            return false;
        }
        context._has_dispatched = true;
        context._reset_revision = reset_revision;
        return true;
#endif
    }
}
