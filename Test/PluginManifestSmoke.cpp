#include "Framework/Plugin/PluginManager.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <stdexcept>

using namespace Ailu;
namespace fs = std::filesystem;

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct ManifestTestDirectory
{
    fs::path _path;
    ~ManifestTestDirectory()
    {
        std::error_code error;
        fs::remove_all(_path, error);
    }
};

static String ManifestJson(const char* id, const char* name, const char* library, bool autoload,
    const char* type = "editor", u32 api_version = kPluginApiVersion)
{
    return std::format(R"({{"id":"{}","name":"{}","version":"0.1.0","api_version":{},)"
        R"("type":"{}","library":"{}","autoload":{}}})", id, name, api_version, type, library,
        autoload ? "true" : "false");
}

static void WriteManifest(const fs::path& folder, const String& text)
{
    fs::create_directories(folder);
    std::ofstream file(folder / "plugin.json", std::ios::binary);
    file << text;
    Require(file.good(), "Write manifest fixture");
}

class ManifestAppender final : public IAppender
{
public:
    void Print(const LogMessage& message) override
    {
        if (message._message.find(L"Manifest ignored:") != WString::npos)
            ++_invalid_count;
        if (message._message.find(L"Duplicate manifest id:") != WString::npos)
            ++_duplicate_count;
        if (message._message.find(L"API version mismatch:") != WString::npos)
            ++_runtime_mismatch_count;
    }
    u32 _invalid_count = 0;
    u32 _duplicate_count = 0;
    u32 _runtime_mismatch_count = 0;
};

int main()
{
    LogMgr::Init();
    auto* appender = new ManifestAppender;
    LogMgr::Get().AddAppender(appender);
    int result = 0;
    try
    {
        wchar_t executable_path[32768]{};
        Require(GetModuleFileNameW(nullptr, executable_path, 32768) != 0, "Executable path");
        const auto root = fs::path(executable_path).parent_path();
        const auto fixture_path = root / (L"ManifestSmoke_" + std::to_wstring(GetCurrentProcessId()));
        Require(fs::create_directory(fixture_path), "Dedicated fixture directory");
        ManifestTestDirectory fixture{fixture_path};
        const auto engine_root = fixture_path / L"引擎Plugins";
        const auto project_root = fixture_path / L"projectPlugins";
        const auto manual_path = engine_root / "00_manual";
        const auto auto_path = engine_root / "01_auto";
        const auto bad_path = engine_root / "02_runtime_bad";
        const auto mismatch_path = engine_root / "03_identity_mismatch";
        WriteManifest(manual_path, ManifestJson("com.ailu.test.smoke", "Ailu Smoke Plugin", "Smoke.dll", false));
        fs::copy_file(root / "Plugins/SmokePlugin/AiluPluginSmoke.dll", manual_path / "Smoke.dll");
        WriteManifest(auto_path,
            ManifestJson("com.ailu.test.second", "Lifecycle probe", "Second.dll", true, "runtime"));
        fs::copy_file(root / "Plugins/LifecycleProbe/AiluPluginSecond.dll", auto_path / "Second.dll");
        WriteManifest(bad_path, ManifestJson("com.ailu.test.runtime_bad", "Bad", "Bad.dll", false));
        fs::copy_file(root / "Plugins/BadVersionPlugin/AiluPluginBadVersion.dll", bad_path / "Bad.dll");
        WriteManifest(mismatch_path, ManifestJson("com.ailu.test.wrong_id", "Ailu Smoke Plugin", "Wrong.dll", false));
        fs::copy_file(root / "Plugins/SmokePlugin/AiluPluginSmoke.dll", mismatch_path / "Wrong.dll");
        WriteManifest(engine_root / "90_duplicate",
            ManifestJson("com.ailu.test.smoke", "duplicate", "Missing.dll", true));
        WriteManifest(project_root / "00_duplicate",
            ManifestJson("com.ailu.test.smoke", "duplicate", "Missing.dll", true));
        WriteManifest(engine_root / "10_malformed", "{broken");
        WriteManifest(engine_root / "11_api", ManifestJson("unsupported", "Unsupported", "Bad.dll", true, "editor", 2));
        WriteManifest(engine_root / "12_escape", ManifestJson("escape", "Escape", "../00_manual/Smoke.dll", true));
        auto wrong_bool = ManifestJson("wrong_bool", "Wrong", "Missing.dll", true);
        wrong_bool.replace(wrong_bool.find("true"), 4, "1");
        WriteManifest(engine_root / "13_bool", wrong_bool);
        WriteManifest(engine_root / "14_nul", ManifestJson("nul", "Nul", "Smoke\\u0000.dll", true));
        WriteManifest(engine_root / "15_type", ManifestJson("type", "Type", "Missing.dll", true, "invalid"));
        auto duplicate_field = ManifestJson("duplicate_field", "Duplicate", "Missing.dll", true);
        duplicate_field.insert(1, "\"id\":\"second_id\",");
        WriteManifest(engine_root / "16_duplicate_field", duplicate_field);
        auto negative_api = ManifestJson("negative", "Negative", "Missing.dll", true);
        negative_api.replace(negative_api.find("\"api_version\":1"), 15, "\"api_version\":-1");
        WriteManifest(engine_root / "17_negative_api", negative_api);
        PluginManager manager;
        Require(manager.Initialize(), "Initialize");
        Require(manager.ScanPlugins({engine_root.wstring(), project_root.wstring(),
            (fixture_path / "missing_root").wstring()}) == 4, "Discover exactly four valid manifests");
        Require(manager.LoadedPluginCount() == 0 && !GetModuleHandleW((manual_path / "Smoke.dll").c_str())
            && !GetModuleHandleW((auto_path / "Second.dll").c_str()), "Scan never loads DLLs");
        const auto discovered = manager.DiscoveredPlugins();
        Require(discovered[0]._id == "com.ailu.test.smoke" && discovered[0]._name == "Ailu Smoke Plugin"
            && discovered[0]._manifest_path == fs::weakly_canonical(manual_path / "plugin.json").wstring(),
            "First entry wins deterministically across roots");
        Require(appender->_invalid_count == 8 && appender->_duplicate_count == 2,
            "Useful invalid/duplicate diagnostics");
        Require(manager.LoadAutoloadPlugins() && manager.LoadedPluginCount() == 1
            && manager.IsLoaded("com.ailu.test.second") && !manager.IsLoaded("com.ailu.test.smoke"),
            "Only autoload true activates");
        Require(manager.LoadPlugin(String("com.ailu.test.smoke")), "Explicit discovered ID load");
        Require(!manager.LoadPlugin(String("unknown_id")), "Unknown ID rejected");
        Require(!manager.LoadPlugin(String("com.ailu.test.runtime_bad")) && appender->_runtime_mismatch_count == 1
            && !GetModuleHandleW((bad_path / "Bad.dll").c_str()), "Runtime API gate still authoritative");
        Require(!manager.LoadPlugin(String("com.ailu.test.wrong_id"))
            && !GetModuleHandleW((mismatch_path / "Wrong.dll").c_str()), "Manifest/DLL identity mismatch rollback");
        manager.Shutdown();
        Require(!GetModuleHandleW((manual_path / "Smoke.dll").c_str())
            && !GetModuleHandleW((auto_path / "Second.dll").c_str()), "Shutdown unloads discovered plugins");
        Require(discovered.size() == 4, "Discovery snapshot owns data");
        std::cout << "M5 PluginManifest: PASS (scan, validation, duplicates, autoload, ID load, runtime gate)\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "M5 PluginManifest: FAIL: " << error.what() << '\n';
        result = 1;
    }
    LogMgr::Shutdown();
    return result;
}
