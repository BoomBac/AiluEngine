#include "Framework/Plugin/PluginManager.h"
#include "Framework/Platform/DynamicLibrary.h"
#include "Framework/Common/Log.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Ailu
{
    namespace
    {
        // Identity counter only, not a registry or lifetime owner. Managers may exist on different threads.
        static std::atomic<u64> s_next_plugin_token{1};

        PluginToken AllocatePluginToken()
        {
            auto next = s_next_plugin_token.load();
            while (next != 0)
            {
                const auto successor = next == std::numeric_limits<u64>::max() ? 0 : next + 1;
                if (s_next_plugin_token.compare_exchange_weak(next, successor))
                    return {next};
            }
            throw std::overflow_error("Plugin token identity exhausted");
        }

        class PluginRegistrationScope final : public NonCopyable
        {
        public:
            explicit PluginRegistrationScope(PluginToken owner) : _owner(owner) {}
            PluginRegistrationHandle Register(const String& name)
            {
                if (!_open || name.empty() || _next_handle == 0)
                    return {};
                const PluginRegistrationHandle handle{_owner, _next_handle++};
                _records.push_back({_owner, handle, EPluginRegistrationType::kHostRecord, name});
                return handle;
            }
            bool Release(PluginRegistrationHandle handle)
            {
                const auto found = Find(handle);
                if (found == _records.end())
                    return false;
                _records.erase(found);
                return true;
            }
            u32 ReleaseAll()
            {
                const auto count = static_cast<u32>(_records.size());
                _records.clear();
                return count;
            }
            void Close()
            {
                _open = false;
                const auto count = ReleaseAll();
                if (count != 0)
                    LOG_INFO("[Plugin] Host registrations released: token={} count={}", _owner._value, count);
            }
            bool Contains(PluginRegistrationHandle handle) const
            {
                return std::find_if(_records.begin(), _records.end(), [handle](const auto& record)
                    { return record._handle == handle; }) != _records.end();
            }
            const Vector<PluginRegistrationRecord>& Records() const { return _records; }
        private:
            Vector<PluginRegistrationRecord>::iterator Find(PluginRegistrationHandle handle)
            {
                return std::find_if(_records.begin(), _records.end(), [handle](const auto& record)
                    { return record._handle == handle; });
            }
            PluginToken _owner;
            Vector<PluginRegistrationRecord> _records;
            u64 _next_handle = 1;
            bool _open = true;
        };

        class PluginModule final : public NonCopyable
        {
        public:
            PluginModule() : _token(AllocatePluginToken()), _registrations(_token) {}
            ~PluginModule() { Unload(); }

            bool Prepare(const WString& path)
            {
                _state = EPluginState::kLoading;
                if (!_library.Load(path))
                    return Fail();
                _get_version = reinterpret_cast<PluginGetApiVersionFn>(_library.GetSymbol("AiluPluginGetApiVersion"));
                if (!_get_version)
                    return MissingExport("AiluPluginGetApiVersion");
                const auto version = _get_version();
                if (version != kPluginApiVersion)
                {
                    LOG_ERROR("[Plugin] API version mismatch: host={} plugin={}", kPluginApiVersion, version);
                    return Fail();
                }
                _get_descriptor = reinterpret_cast<PluginGetDescriptorFn>(
                    _library.GetSymbol("AiluPluginGetDescriptor"));
                _create = reinterpret_cast<PluginCreateFn>(_library.GetSymbol("AiluPluginCreate"));
                _destroy = reinterpret_cast<PluginDestroyFn>(_library.GetSymbol("AiluPluginDestroy"));
                if (!_get_descriptor)
                    return MissingExport("AiluPluginGetDescriptor");
                if (!_create)
                    return MissingExport("AiluPluginCreate");
                if (!_destroy)
                    return MissingExport("AiluPluginDestroy");
                const auto* descriptor = _get_descriptor();
                if (!descriptor || !descriptor->_id || !*descriptor->_id || !descriptor->_name
                    || !*descriptor->_name || !descriptor->_version || !*descriptor->_version
                    || (descriptor->_type != EPluginType::kRuntime && descriptor->_type != EPluginType::kEditor))
                {
                    LOG_ERROR("[Plugin] Invalid descriptor");
                    return Fail();
                }
                _info = {descriptor->_id, descriptor->_name, descriptor->_version,
                    descriptor->_type, _library.Path()};
                return true;
            }

            bool Activate(const PluginContext& context)
            {
                _instance = _create();
                if (!_instance)
                {
                    LOG_ERROR("[Plugin] Create returned null: {}", _info._id);
                    return Fail();
                }
                _on_load_started = true;
                if (!_instance->OnLoad(context))
                {
                    LOG_ERROR("[Plugin] OnLoad failed: {}", _info._id);
                    return Fail();
                }
                _state = EPluginState::kActive;
                return true;
            }

            void Unload()
            {
                _state = EPluginState::kUnloading;
                // Stop new registrations and detach host records before any plugin teardown code executes.
                _registrations.Close();
                if (_instance)
                {
                    // Even a failed OnLoad may have started work: pair it with cleanup before Destroy.
                    if (_on_load_started)
                        _instance->OnUnload();
                    _destroy(_instance);
                    _instance = nullptr;
                }
                _on_load_started = false;
                _get_version = nullptr;
                _get_descriptor = nullptr;
                _create = nullptr;
                _destroy = nullptr;
                _library.Unload();
                _state = _library.IsLoaded() ? EPluginState::kFailed : EPluginState::kUnloaded;
            }

            void Tick(f32 delta_time)
            {
                if (_state == EPluginState::kActive)
                    _instance->Tick(delta_time);
            }
            const PluginInfo& Info() const { return _info; }
            PluginToken Token() const { return _token; }
            bool IsActive() const { return _state == EPluginState::kActive; }
            PluginRegistrationScope& Registrations() { return _registrations; }

        private:
            bool Fail()
            {
                Unload();
                _state = EPluginState::kFailed;
                return false;
            }
            bool MissingExport(const char* name)
            {
                LOG_ERROR(L"[Plugin] Missing export {}: {}", ToWChar(name), _library.Path());
                return Fail();
            }

            DynamicLibrary _library;
            PluginGetApiVersionFn _get_version = nullptr;
            PluginGetDescriptorFn _get_descriptor = nullptr;
            PluginCreateFn _create = nullptr;
            PluginDestroyFn _destroy = nullptr;
            IPlugin* _instance = nullptr;
            PluginInfo _info;
            PluginToken _token;
            PluginRegistrationScope _registrations;
            EPluginState _state = EPluginState::kUnloaded;
            bool _on_load_started = false;
        };

        class OperationGuard final : public NonCopyable
        {
        public:
            explicit OperationGuard(bool& busy) : _busy(busy) { _busy = true; }
            ~OperationGuard() { _busy = false; }
        private:
            bool& _busy;
        };
    }

    class PluginManager::Impl final : public IPluginHost, public IPluginProgressHost
    {
    public:
        void LogInfo(const char* message) noexcept override
        {
            try { LOG_INFO("[Plugin] {}", message ? message : ""); } catch (...) {}
        }
        void LogWarning(const char* message) noexcept override
        {
            try { LOG_WARNING("[Plugin] {}", message ? message : ""); } catch (...) {}
        }
        void LogError(const char* message) noexcept override
        {
            try { LOG_ERROR("[Plugin] {}", message ? message : ""); } catch (...) {}
        }
        bool IsMainThread() const noexcept override { return std::this_thread::get_id() == _owner_thread; }
        void ReportProgress(const char* title, const char* message, u64 completed, EPluginProgressState state) noexcept override
        {
            if (!IsMainThread() || !_progress_callback) return;
            try { _progress_callback(title, message, completed, state); } catch (...) {}
        }
        ProgressCallback _progress_callback;
        bool CheckOwner() const
        {
            if (IsMainThread())
                return true;
            LOG_WARNING("[Plugin] Operation rejected: not the owner thread");
            return false;
        }
        bool CanMutate() const
        {
            if (!CheckOwner())
                return false;
            if (_busy)
            {
                LOG_WARNING("[Plugin] Lifecycle operation rejected during Tick/load/unload");
                return false;
            }
            return _initialized;
        }
        auto Find(const String& id) const
        {
            return std::find_if(_plugins.begin(), _plugins.end(), [&id](const auto& module)
                { return module->Info()._id == id; });
        }
        auto Find(PluginToken owner) const
        {
            return std::find_if(_plugins.begin(), _plugins.end(), [owner](const auto& module)
                { return module->Token() == owner; });
        }

        // The vector is also the explicit load order; plugin counts are small in v1.
        std::vector<Scope<PluginModule>> _plugins;
        Vector<PluginManifest> _manifests;
        std::thread::id _owner_thread = std::this_thread::get_id();
        bool _initialized = false;
        bool _busy = false;
    };

    PluginManager::PluginManager() : _impl(MakeScope<Impl>()) {}
    PluginManager::~PluginManager()
    {
        // Destruction cannot silently bypass affinity or happen inside a plugin call.
        if (!_impl->IsMainThread() || _impl->_busy)
            std::terminate();
        Shutdown();
    }

    void PluginManager::SetProgressCallback(ProgressCallback callback)
    {
        if (!_impl->CheckOwner() || _impl->_busy) return;
        _impl->_progress_callback = std::move(callback);
    }

    bool PluginManager::Initialize()
    {
        if (!_impl->CheckOwner() || _impl->_busy)
            return false;
        _impl->_owner_thread = std::this_thread::get_id();
        _impl->_initialized = true;
        return true;
    }

    bool PluginManager::LoadPlugin(const WString& path)
    {
        return LoadPluginInternal(path, nullptr);
    }

    bool PluginManager::LoadPluginInternal(const WString& path, const PluginManifest* manifest)
    {
        if (!_impl->CanMutate())
            return false;
        OperationGuard guard(_impl->_busy);
        try
        {
            auto module = MakeScope<PluginModule>();
            if (!module->Prepare(path))
                return false;
            if (manifest && (module->Info()._id != manifest->_id || module->Info()._name != manifest->_name
                || module->Info()._version != manifest->_version || module->Info()._type != manifest->_type))
            {
                LOG_ERROR("[Plugin] Manifest/DLL descriptor mismatch: {}", manifest->_id);
                return false;
            }
            const auto existing = _impl->Find(module->Info()._id);
            if (existing != _impl->_plugins.end())
            {
                LOG_ERROR(L"[Plugin] Duplicate plugin id: {}; existing='{}', new='{}",
                    ToWChar(module->Info()._id), (*existing)->Info()._library_path, module->Info()._library_path);
                return false;
            }
            // Allocate the manager entry before OnLoad starts plugin work.
            _impl->_plugins.reserve(_impl->_plugins.size() + 1);
            if (!module->Activate({kPluginApiVersion, _impl.get()}))
                return false;
            LOG_INFO("[Plugin] Loaded {} ({})", module->Info()._id, module->Info()._version);
            _impl->_plugins.push_back(std::move(module));
            return true;
        }
        catch (const std::exception& error)
        {
            LOG_ERROR("[Plugin] Load failed: {}", error.what());
            return false;
        }
    }

    bool PluginManager::UnloadPlugin(const String& plugin_id)
    {
        if (!_impl->CanMutate())
            return false;
        const auto found = _impl->Find(plugin_id);
        if (found == _impl->_plugins.end())
            return false;
        OperationGuard guard(_impl->_busy);
        (*found)->Unload();
        LOG_INFO("[Plugin] Unloaded {}", plugin_id);
        _impl->_plugins.erase(found);
        return true;
    }

    u32 PluginManager::ScanPlugins(const Vector<WString>& roots)
    {
        if (!_impl->CanMutate())
            return 0;
        OperationGuard guard(_impl->_busy);
        _impl->_manifests = ScanPluginManifests(roots);
        LOG_INFO("[Plugin] Discovered {} plugin manifests", _impl->_manifests.size());
        return static_cast<u32>(_impl->_manifests.size());
    }

    Vector<PluginManifest> PluginManager::DiscoveredPlugins() const
    {
        return _impl->CheckOwner() ? _impl->_manifests : Vector<PluginManifest>{};
    }

    bool PluginManager::LoadPlugin(const String& plugin_id)
    {
        if (!_impl->CanMutate())
            return false;
        const auto manifest = std::find_if(_impl->_manifests.begin(), _impl->_manifests.end(),
            [&plugin_id](const auto& item) { return item._id == plugin_id; });
        if (manifest == _impl->_manifests.end())
        {
            LOG_WARNING("[Plugin] Plugin id not discovered: {}", plugin_id);
            return false;
        }
        return LoadPluginInternal(manifest->_library_path, &*manifest);
    }

    bool PluginManager::LoadAutoloadPlugins()
    {
        if (!_impl->CanMutate())
            return false;
        // Snapshot survives any host log observer; LoadPlugin itself rejects lifecycle reentrancy.
        const auto manifests = _impl->_manifests;
        bool success = true;
        for (const auto& manifest : manifests)
            if (manifest._autoload && !IsLoaded(manifest._id))
                success = LoadPluginInternal(manifest._library_path, &manifest) && success;
        return success;
    }

    void PluginManager::Tick(f32 delta_time)
    {
        if (!_impl->CanMutate())
            return;
        OperationGuard guard(_impl->_busy);
        for (const auto& module : _impl->_plugins)
            module->Tick(delta_time);
    }

    void PluginManager::Shutdown()
    {
        if (!_impl->CanMutate())
            return;
        OperationGuard guard(_impl->_busy);
        while (!_impl->_plugins.empty())
        {
            auto& module = _impl->_plugins.back();
            module->Unload();
            LOG_INFO("[Plugin] Unloaded {}", module->Info()._id);
            _impl->_plugins.pop_back();
        }
        _impl->_initialized = false;
        _impl->_manifests.clear();
    }

    bool PluginManager::IsLoaded(const String& plugin_id) const
    {
        return _impl->CheckOwner() && _impl->Find(plugin_id) != _impl->_plugins.end();
    }
    u32 PluginManager::LoadedPluginCount() const
    {
        return _impl->CheckOwner() ? static_cast<u32>(_impl->_plugins.size()) : 0;
    }

    PluginToken PluginManager::GetPluginToken(const String& plugin_id) const
    {
        if (!_impl->CheckOwner())
            return {};
        const auto found = _impl->Find(plugin_id);
        return found != _impl->_plugins.end() && (*found)->IsActive() ? (*found)->Token() : PluginToken{};
    }

    PluginRegistrationHandle PluginManager::RegisterHostRecord(PluginToken owner, const String& name)
    {
        if (!_impl->CanMutate() || owner._value == 0)
            return {};
        const auto found = _impl->Find(owner);
        if (found == _impl->_plugins.end() || !(*found)->IsActive())
            return {};
        return (*found)->Registrations().Register(name);
    }

    bool PluginManager::ReleaseRegistration(PluginRegistrationHandle handle)
    {
        if (!_impl->CanMutate())
            return false;
        const auto found = _impl->Find(handle._owner);
        return found != _impl->_plugins.end() && (*found)->Registrations().Release(handle);
    }

    u32 PluginManager::ReleaseAllRegistrations(PluginToken owner)
    {
        if (!_impl->CanMutate())
            return 0;
        const auto found = _impl->Find(owner);
        return found != _impl->_plugins.end() ? (*found)->Registrations().ReleaseAll() : 0;
    }

    bool PluginManager::HasRegistration(PluginRegistrationHandle handle) const
    {
        if (!_impl->CheckOwner())
            return false;
        const auto found = _impl->Find(handle._owner);
        return found != _impl->_plugins.end() && (*found)->Registrations().Contains(handle);
    }

    Vector<PluginRegistrationRecord> PluginManager::Registrations(PluginToken owner) const
    {
        if (!_impl->CheckOwner())
            return {};
        const auto found = _impl->Find(owner);
        return found != _impl->_plugins.end() ? (*found)->Registrations().Records()
            : Vector<PluginRegistrationRecord>{};
    }
}
