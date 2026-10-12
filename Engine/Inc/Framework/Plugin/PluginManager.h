#pragma once
#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Plugin/PluginApi.h"
#include "Framework/Plugin/PluginManifest.h"
#include "Framework/Plugin/PluginRegistration.h"
#include <functional>

namespace Ailu
{
    struct PluginInfo
    {
        String _id;
        String _name;
        String _version;
        EPluginType _type = EPluginType::kRuntime;
        WString _library_path;
    };

    enum class EPluginState : u8
    {
        kUnloaded,
        kLoading,
        kActive,
        kUnloading,
        kFailed,
    };

    // Construct, initialize, use and destroy on the owner thread. No callbacks escape the DLL lifecycle.
    class AILU_API PluginManager final : public NonCopyable
    {
    public:
        PluginManager();
        ~PluginManager();
        bool Initialize();
        using ProgressCallback = std::function<void(const char*, const char*, u64, EPluginProgressState)>;
        void SetProgressCallback(ProgressCallback callback);
        void Shutdown();
        bool LoadPlugin(const WString& path);
        bool LoadPlugin(const String& plugin_id);
        u32 ScanPlugins(const Vector<WString>& roots);
        bool LoadAutoloadPlugins();
        [[nodiscard]] Vector<PluginManifest> DiscoveredPlugins() const;
        bool UnloadPlugin(const String& plugin_id);
        void Tick(f32 delta_time);
        [[nodiscard]] bool IsLoaded(const String& plugin_id) const;
        [[nodiscard]] u32 LoadedPluginCount() const;
        [[nodiscard]] PluginToken GetPluginToken(const String& plugin_id) const;
        PluginRegistrationHandle RegisterHostRecord(PluginToken owner, const String& name);
        bool ReleaseRegistration(PluginRegistrationHandle handle);
        u32 ReleaseAllRegistrations(PluginToken owner);
        [[nodiscard]] bool HasRegistration(PluginRegistrationHandle handle) const;
        [[nodiscard]] Vector<PluginRegistrationRecord> Registrations(PluginToken owner) const;

    private:
        bool LoadPluginInternal(const WString& path, const PluginManifest* manifest);
        class Impl;
        Scope<Impl> _impl;
    };
}
