#include "pch.h"

#include "RHI/DX12/StreamlineRuntime.h"
#include "Framework/Common/Log.h"

#if AILU_WITH_STREAMLINE
#include <sl.h>
#include <sl_core_api.h>
#include <sl_security.h>
#include <sl_helpers.h>
#include <sl_dlss.h>
#endif

namespace Ailu::RHI::DX12
{
    std::mutex& GetStreamlineApiMutex()
    {
        static std::mutex s_streamline_api_mutex;
        return s_streamline_api_mutex;
    }

#if AILU_WITH_STREAMLINE
    namespace
    {
        struct StreamlineFunctions
        {
            PFun_slInit* _init = nullptr;
            PFun_slShutdown* _shutdown = nullptr;
            PFun_slSetD3DDevice* _set_d3d_device = nullptr;
            PFun_slIsFeatureSupported* _is_feature_supported = nullptr;
            PFun_slIsFeatureLoaded* _is_feature_loaded = nullptr;
            PFun_slGetFeatureRequirements* _get_feature_requirements = nullptr;
            PFun_slUpgradeInterface* _upgrade_interface = nullptr;
            PFun_slGetNativeInterface* _get_native_interface = nullptr;
            PFun_slGetFeatureFunction* _get_feature_function = nullptr;
        };

        constexpr char kAiluEngineVersion[] = "0.1.0";
        constexpr char kAiluStreamlineProjectId[] = "d34eaade-9ae8-4783-a726-7b19adce638d";

        void StreamlineLog(sl::LogType type, const char* message)
        {
            if (message == nullptr)
                return;
            if (type == sl::LogType::eError)
            {
                LOG_ERROR("Streamline: {}", message);
            }
            else if (type == sl::LogType::eWarn)
            {
                LOG_WARNING("Streamline: {}", message);
            }
            else
            {
                LOG_INFO("Streamline: {}", message);
            }
        }

        void StreamlineModuleAnchor() {}

        bool LoadFunction(HMODULE module, const char* name, FARPROC& output)
        {
            output = GetProcAddress(module, name);
            if (output != nullptr)
                return true;
            LOG_ERROR("Streamline: required export '{}' is missing (error={}).", name, GetLastError());
            return false;
        }

        String ResultName(sl::Result result)
        {
            return std::format("{} ({})", sl::getResultAsStr(result), static_cast<u32>(result));
        }

        String FileVersion(const std::filesystem::path& path)
        {
            const DWORD data_size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
            if (data_size == 0)
                return "unknown";
            Vector<u8> data(data_size);
            if (!GetFileVersionInfoW(path.c_str(), 0, data_size, data.data()))
                return "unknown";
            VS_FIXEDFILEINFO* version = nullptr;
            UINT version_size = 0;
            if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&version), &version_size) ||
                version == nullptr || version_size < sizeof(VS_FIXEDFILEINFO))
                return "unknown";
            return std::format("{}.{}.{}.{}", HIWORD(version->dwFileVersionMS), LOWORD(version->dwFileVersionMS),
                               HIWORD(version->dwFileVersionLS), LOWORD(version->dwFileVersionLS));
        }
    }
#endif

    struct StreamlineRuntime::Impl
    {
#if AILU_WITH_STREAMLINE
        HMODULE _module = nullptr;
        DLL_DIRECTORY_COOKIE _directory_cookie = nullptr;
        StreamlineFunctions _functions;
        bool _initialized = false;
        bool _device_attached = false;
        bool _dlss_supported = false;
#endif
    };

    StreamlineRuntime::StreamlineRuntime() : _impl(std::make_unique<Impl>()) {}

    StreamlineRuntime::~StreamlineRuntime() { Shutdown(); }

    bool StreamlineRuntime::Init()
    {
#if !AILU_WITH_STREAMLINE
        LOG_INFO("Streamline: disabled at build time.");
        return false;
#else
        if (_impl->_initialized)
            return true;

        HMODULE engine_module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&StreamlineModuleAnchor), &engine_module))
        {
            LOG_ERROR("Streamline: failed to locate the Engine module (error={}).", GetLastError());
            return false;
        }

        wchar_t module_path[MAX_PATH]{};
        const DWORD module_path_length = GetModuleFileNameW(engine_module, module_path, _countof(module_path));
        if (module_path_length == 0 || module_path_length >= _countof(module_path))
        {
            LOG_ERROR("Streamline: failed to resolve the Engine module path (error={}).", GetLastError());
            return false;
        }

        std::filesystem::path runtime_dir = std::filesystem::path(module_path).parent_path() / L"Streamline";
        const std::filesystem::path interposer_path = runtime_dir / L"sl.interposer.dll";
        if (!std::filesystem::exists(interposer_path))
        {
            LOG_WARNING("Streamline: unavailable ({} is missing); using native DX12.", interposer_path.string());
            return false;
        }
        if (!sl::security::verifyEmbeddedSignature(interposer_path.c_str()))
        {
            LOG_ERROR("Streamline: signature verification failed for {}; using native DX12.", interposer_path.string());
            return false;
        }
        LOG_INFO("Streamline: verified NVIDIA signature; sl.interposer.dll file version={}",
                 FileVersion(interposer_path));

        _impl->_directory_cookie = AddDllDirectory(runtime_dir.c_str());
        if (_impl->_directory_cookie == nullptr)
        {
            LOG_ERROR("Streamline: failed to add the trusted DLL directory (error={}).", GetLastError());
            return false;
        }

        _impl->_module = LoadLibraryExW(interposer_path.c_str(), nullptr,
                                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (_impl->_module == nullptr)
        {
            LOG_WARNING("Streamline: failed to load sl.interposer.dll (error={}); using native DX12.", GetLastError());
            Shutdown();
            return false;
        }

        FARPROC function = nullptr;
#define AILU_LOAD_STREAMLINE_FUNCTION(field, name) \
        do \
        { \
            if (!LoadFunction(_impl->_module, name, function)) \
            { \
                Shutdown(); \
                return false; \
            } \
            _impl->_functions.field = reinterpret_cast<decltype(_impl->_functions.field)>(function); \
        } while (false)
        AILU_LOAD_STREAMLINE_FUNCTION(_init, "slInit");
        AILU_LOAD_STREAMLINE_FUNCTION(_shutdown, "slShutdown");
        AILU_LOAD_STREAMLINE_FUNCTION(_set_d3d_device, "slSetD3DDevice");
        AILU_LOAD_STREAMLINE_FUNCTION(_is_feature_supported, "slIsFeatureSupported");
        AILU_LOAD_STREAMLINE_FUNCTION(_is_feature_loaded, "slIsFeatureLoaded");
        AILU_LOAD_STREAMLINE_FUNCTION(_get_feature_requirements, "slGetFeatureRequirements");
        AILU_LOAD_STREAMLINE_FUNCTION(_upgrade_interface, "slUpgradeInterface");
        AILU_LOAD_STREAMLINE_FUNCTION(_get_native_interface, "slGetNativeInterface");
        AILU_LOAD_STREAMLINE_FUNCTION(_get_feature_function, "slGetFeatureFunction");
#undef AILU_LOAD_STREAMLINE_FUNCTION

        constexpr sl::Feature kFeaturesToLoad[] = {sl::kFeatureDLSS};
        const wchar_t* plugin_paths[] = {runtime_dir.c_str()};
        sl::Preferences preferences{};
        preferences.logLevel = sl::LogLevel::eDefault;
        preferences.pathsToPlugins = plugin_paths;
        preferences.numPathsToPlugins = _countof(plugin_paths);
        preferences.pathToLogsAndData = runtime_dir.c_str();
        preferences.logMessageCallback = &StreamlineLog;
        preferences.engine = sl::EngineType::eCustom;
        preferences.engineVersion = kAiluEngineVersion;
        preferences.projectId = kAiluStreamlineProjectId;
        preferences.flags = sl::PreferenceFlags::eUseManualHooking |
                            sl::PreferenceFlags::eUseFrameBasedResourceTagging |
                            sl::PreferenceFlags::eDisableCLStateTracking;
        preferences.featuresToLoad = kFeaturesToLoad;
        preferences.numFeaturesToLoad = _countof(kFeaturesToLoad);
        const sl::Result result = _impl->_functions._init(preferences, sl::kSDKVersion);
        if (result != sl::Result::eOk)
        {
            LOG_WARNING("Streamline: slInit failed: {}; using native DX12.", ResultName(result));
            Shutdown();
            return false;
        }

        _impl->_initialized = true;
        LOG_INFO("Streamline: SDK {}.{}.{} initialized with Manual Hooking.", SL_VERSION_MAJOR, SL_VERSION_MINOR,
                 SL_VERSION_PATCH);
        return true;
#endif
    }

    bool StreamlineRuntime::AttachD3D12Device(void* native_device, const void* adapter_luid, std::uint32_t luid_size)
    {
#if !AILU_WITH_STREAMLINE
        (void) native_device;
        (void) adapter_luid;
        (void) luid_size;
        return false;
#else
        if (!_impl->_initialized || native_device == nullptr || adapter_luid == nullptr || luid_size == 0)
            return false;

        sl::Result result = _impl->_functions._set_d3d_device(native_device);
        if (result != sl::Result::eOk)
        {
            LOG_WARNING("Streamline: slSetD3DDevice failed: {}; feature support unavailable.", ResultName(result));
            return false;
        }
        _impl->_device_attached = true;

        sl::AdapterInfo adapter_info{};
        adapter_info.deviceLUID = reinterpret_cast<std::uint8_t*>(const_cast<void*>(adapter_luid));
        adapter_info.deviceLUIDSizeInBytes = luid_size;
        bool feature_loaded = false;
        const sl::Result loaded_result = _impl->_functions._is_feature_loaded(sl::kFeatureDLSS, feature_loaded);
        const sl::Result support_result = _impl->_functions._is_feature_supported(sl::kFeatureDLSS, adapter_info);
        sl::FeatureRequirements requirements{};
        const sl::Result requirements_result =
                _impl->_functions._get_feature_requirements(sl::kFeatureDLSS, requirements);
        _impl->_dlss_supported = loaded_result == sl::Result::eOk && feature_loaded &&
                                 support_result == sl::Result::eOk && requirements_result == sl::Result::eOk;
        if (_impl->_dlss_supported)
        {
            LOG_INFO("Streamline: SDK=loaded, DLSS_SR=supported, plugin=loaded.");
        }
        else
        {
            LOG_INFO("Streamline: SDK=loaded, DLSS_SR=unsupported "
                     "(plugin_loaded={}, load={}, support={}, requirements={}).",
                     loaded_result == sl::Result::eOk && feature_loaded, ResultName(loaded_result),
                     ResultName(support_result),
                     ResultName(requirements_result));
        }
        return true;
#endif
    }

    void StreamlineRuntime::Shutdown()
    {
#if AILU_WITH_STREAMLINE
        if (_impl->_initialized)
        {
            const sl::Result result = _impl->_functions._shutdown();
            if (result != sl::Result::eOk)
                LOG_WARNING("Streamline: slShutdown returned {}.", ResultName(result));
            _impl->_initialized = false;
        }
        _impl->_device_attached = false;
        _impl->_dlss_supported = false;
        if (_impl->_module != nullptr)
        {
            FreeLibrary(_impl->_module);
            _impl->_module = nullptr;
        }
        if (_impl->_directory_cookie != nullptr)
        {
            RemoveDllDirectory(_impl->_directory_cookie);
            _impl->_directory_cookie = nullptr;
        }
#endif
    }

    bool StreamlineRuntime::IsLoaded() const
    {
#if AILU_WITH_STREAMLINE
        return _impl->_initialized;
#else
        return false;
#endif
    }

    bool StreamlineRuntime::IsDlssSupported() const
    {
#if AILU_WITH_STREAMLINE
        return _impl->_dlss_supported;
#else
        return false;
#endif
    }

    bool StreamlineRuntime::GetDlssOptimalRenderSize(std::uint32_t mode, std::uint32_t output_width,
                                                      std::uint32_t output_height, std::uint32_t& render_width,
                                                      std::uint32_t& render_height) const
    {
#if AILU_WITH_STREAMLINE
        if (!_impl->_initialized || !_impl->_dlss_supported || output_width == 0u || output_height == 0u)
            return false;

        std::lock_guard lock(GetStreamlineApiMutex());
        void* function = nullptr;
        const sl::Result lookup = _impl->_functions._get_feature_function(sl::kFeatureDLSS,
                                                                           "slDLSSGetOptimalSettings", function);
        if (lookup != sl::Result::eOk || function == nullptr)
            return false;

        sl::DLSSOptions options{};
        options.mode = mode == 1u ? sl::DLSSMode::eMaxQuality
            : mode == 2u ? sl::DLSSMode::eBalanced : sl::DLSSMode::eMaxPerformance;
        options.outputWidth = output_width;
        options.outputHeight = output_height;
        sl::DLSSOptimalSettings settings{};
        auto* get_optimal_settings = reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(function);
        const sl::Result result = (*get_optimal_settings)(options, settings);
        if (result != sl::Result::eOk || settings.optimalRenderWidth == 0u || settings.optimalRenderHeight == 0u)
        {
            LOG_WARNING("Streamline: DLSS optimal settings failed for {}x{} ({}).", output_width, output_height,
                        ResultName(result));
            return false;
        }

        render_width = settings.optimalRenderWidth;
        render_height = settings.optimalRenderHeight;
        return true;
#else
        (void) mode;
        (void) output_width;
        (void) output_height;
        (void) render_width;
        (void) render_height;
        return false;
#endif
    }

    void* StreamlineRuntime::GetCoreFunction(const char* name) const
    {
#if AILU_WITH_STREAMLINE
        if (!_impl->_initialized || name == nullptr)
            return nullptr;
        return reinterpret_cast<void*>(GetProcAddress(_impl->_module, name));
#else
        (void) name;
        return nullptr;
#endif
    }

    void* StreamlineRuntime::GetFeatureFunction(const char* name) const
    {
#if AILU_WITH_STREAMLINE
        if (!_impl->_initialized || !_impl->_dlss_supported || name == nullptr)
            return nullptr;
        void* function = nullptr;
        const sl::Result result = _impl->_functions._get_feature_function(sl::kFeatureDLSS, name, function);
        if (result != sl::Result::eOk)
            return nullptr;
        return function;
#else
        (void) name;
        return nullptr;
#endif
    }

    bool StreamlineRuntime::UpgradeInterface(void** native_interface) const
    {
#if AILU_WITH_STREAMLINE
        if (!_impl->_initialized || native_interface == nullptr || *native_interface == nullptr)
            return false;
        const sl::Result result = _impl->_functions._upgrade_interface(native_interface);
        if (result == sl::Result::eOk)
            return true;
        LOG_WARNING("Streamline: slUpgradeInterface failed: {}.", ResultName(result));
#else
        (void) native_interface;
#endif
        return false;
    }

    bool StreamlineRuntime::GetNativeInterface(void* proxy_interface, void** native_interface) const
    {
#if AILU_WITH_STREAMLINE
        if (!_impl->_initialized || proxy_interface == nullptr || native_interface == nullptr)
            return false;
        const sl::Result result = _impl->_functions._get_native_interface(proxy_interface, native_interface);
        if (result == sl::Result::eOk)
            return true;
        LOG_WARNING("Streamline: slGetNativeInterface failed: {}.", ResultName(result));
#else
        (void) proxy_interface;
        (void) native_interface;
#endif
        return false;
    }
}
