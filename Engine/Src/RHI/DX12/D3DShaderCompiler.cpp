#include "pch.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <mutex>

#include "Framework/Common/Application.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "GlobalMarco.h"
#include "RHI/DX12/D3DShaderCompiler.h"
#include "RHI/DX12/dxhelper.h"


#define SHADER_DEBUG_SYMBOLS 1

namespace Ailu
{
    bool LoadBlobFromFile(const WString &path, ComPtr<ID3DBlob> &blob)
    {
        return SUCCEEDED(D3DReadFileToBlob(path.c_str(), blob.ReleaseAndGetAddressOf()));
    }

    bool CopyDxcBlob(IDxcBlob *source_blob, ComPtr<ID3DBlob> &dest_blob)
    {
        if (!source_blob)
            return false;

        ComPtr<ID3DBlob> copied_blob;
        if (FAILED(D3DCreateBlob(source_blob->GetBufferSize(), copied_blob.GetAddressOf())))
            return false;

        std::memcpy(copied_blob->GetBufferPointer(), source_blob->GetBufferPointer(), source_blob->GetBufferSize());
        dest_blob = copied_blob;
        return true;
    }

    class D3DShaderInclude : public ID3DInclude
    {
    public:
        HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) override;

        HRESULT Close(LPCVOID pData) override;

    public:
        WString _cur_source_file_path;
        std::set<WString> _include_files;

    private:
        std::set<WString> _addi_include_pathes = {
                L"Shaders/",
                L"Shaders/hlsl/",
                L"Shaders/hlsl/Compute/",
                L"Shaders/hlsl/PostProcess/"};
        u8 *_data = nullptr;
        inline static std::mutex s_compile_lock;
    };

    namespace
    {
        std::set<WString> BuildIncludeSearchPaths(const WString &source_file_path, const Vector<WString> *search_paths = nullptr)
        {
            std::set<WString> resolved_paths = {
                    L"Shaders/",
                    L"Shaders/hlsl/",
                    L"Shaders/hlsl/Compute/",
                    L"Shaders/hlsl/PostProcess/"};

            auto pwd = PathUtils::ExtractAssetPath(PathUtils::Parent(source_file_path));
            if (!pwd.empty())
                resolved_paths.insert(pwd);

            if (search_paths)
            {
                for (const auto &path: *search_paths)
                {
                    if (!path.empty())
                        resolved_paths.insert(path);
                }
            }
            return resolved_paths;
        }

        bool TryResolveIncludeFile(const WString &source_file_path,
                                   const WString &include_path,
                                   const std::set<WString> &search_paths,
                                   WString &resolved_path)
        {
            auto try_use_path = [&](const WString &candidate_path) -> bool
            {
                if (!candidate_path.empty() && FileManager::Exist(candidate_path))
                {
                    resolved_path = PathUtils::FormatFilePath(candidate_path);
                    return true;
                }
                return false;
            };

            if (include_path.empty())
                return false;

            if (PathUtils::IsSystemPath(include_path) && try_use_path(include_path))
                return true;

            const auto source_dir = PathUtils::Parent(source_file_path);
            if (include_path[0] == L'.')
            {
                if (try_use_path(PathUtils::ResolveRelPath(include_path, source_dir).wstring()))
                    return true;
            }

            if (try_use_path(PathUtils::ResolveRelPath(include_path, source_dir).wstring()))
                return true;

            for (const auto &search_path: search_paths)
            {
                WString candidate_path;
                if (PathUtils::IsSystemPath(search_path))
                {
                    candidate_path = PathUtils::FormatFilePath(search_path);
                    if (!candidate_path.empty() && candidate_path.back() != L'/')
                        candidate_path += L'/';
                    candidate_path += include_path;
                }
                else
                {
                    candidate_path = ResourceMgr::GetResSysPath(search_path) + include_path;
                }

                if (try_use_path(candidate_path))
                    return true;
            }

            return false;
        }

        Vector<WString> ParseIncludeStatements(const String &source)
        {
            Vector<WString> include_paths;
            std::istringstream iss(source);
            String line;

            auto trim_left = [](StringView text) -> StringView
            {
                const auto pos = text.find_first_not_of(" \t\r\n");
                return pos == StringView::npos ? StringView{} : text.substr(pos);
            };

            while (std::getline(iss, line))
            {
                auto trimmed = trim_left(line);
                if (trimmed.empty() || trimmed.starts_with("//") || trimmed[0] != '#')
                    continue;

                trimmed.remove_prefix(1);
                trimmed = trim_left(trimmed);
                if (!trimmed.starts_with("include"))
                    continue;

                trimmed.remove_prefix(std::char_traits<char>::length("include"));
                trimmed = trim_left(trimmed);
                if (trimmed.empty())
                    continue;

                const char closing = trimmed[0] == '"' ? '"' : trimmed[0] == '<' ? '>' : '\0';
                if (closing == '\0')
                    continue;

                trimmed.remove_prefix(1);
                const auto end_pos = trimmed.find(closing);
                if (end_pos == StringView::npos || end_pos == 0)
                    continue;

                include_paths.emplace_back(ToWChar(String(trimmed.substr(0, end_pos))));
            }

            return include_paths;
        }

        bool ParseIncludeDependenciesRecursive(const WString &source_file_path,
                                               const Vector<WString> &search_paths,
                                               std::set<WString> &visited_files,
                                               std::set<WString> &include_files)
        {
            const WString normalized_source_path = PathUtils::FormatFilePath(source_file_path);
            if (!FileManager::Exist(normalized_source_path))
                return false;

            if (!visited_files.insert(normalized_source_path).second)
                return true;

            String file_text;
            if (!FileManager::ReadFile(normalized_source_path, file_text))
                return false;

            bool parse_succeed = true;
            const auto effective_search_paths = BuildIncludeSearchPaths(normalized_source_path, &search_paths);
            const auto direct_includes = ParseIncludeStatements(file_text);
            for (const auto &include_path: direct_includes)
            {
                WString resolved_include_path;
                if (!TryResolveIncludeFile(normalized_source_path, include_path, effective_search_paths, resolved_include_path))
                {
                    LOG_WARNING(L"Failed to resolve include {} from {}", include_path, normalized_source_path);
                    parse_succeed = false;
                    continue;
                }

                include_files.insert(resolved_include_path);
                if (!ParseIncludeDependenciesRecursive(resolved_include_path, search_paths, visited_files, include_files))
                    parse_succeed = false;
            }

            return parse_succeed;
        }
    }

    HRESULT D3DShaderInclude ::Close(LPCVOID pData)
    {
        delete[] pData;
        return S_OK;
    }

    HRESULT D3DShaderInclude::Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
    {
        const auto search_paths = BuildIncludeSearchPaths(_cur_source_file_path);
        WString resolved_path;
        if (!TryResolveIncludeFile(_cur_source_file_path, ToWChar(pFileName), search_paths, resolved_path))
        {
            AL_ASSERT(true);
            return E_FAIL;
        }

        {
            std::lock_guard<std::mutex> l(s_compile_lock);
            auto [file_data, byte_size] = FileManager::ReadFile(resolved_path);
            _data = file_data;
            *ppData = _data;
            *pBytes = (u32) byte_size;
            _include_files.insert(resolved_path);
            return S_OK;
        }
    }

    class DxcIncludeHandlerEx final : public IDxcIncludeHandler
    {
    public:
        WString _cur_source_file_path;
        std::set<WString> _include_files;

    private:
        std::atomic<ULONG> _ref_count{1};
        IDxcUtils *_utils = nullptr;
        std::set<WString> _addi_include_pathes = {
                L"Shaders/",
                L"Shaders/hlsl/",
                L"Shaders/hlsl/Compute/",
                L"Shaders/hlsl/PostProcess/"};
        inline static std::mutex s_compile_lock;

    public:
        explicit DxcIncludeHandlerEx(IDxcUtils *utils)
            : _utils(utils)
        {
            _utils->AddRef();
        }

        ~DxcIncludeHandlerEx()
        {
            if (_utils)
                _utils->Release();
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
        {
            if (riid == __uuidof(IDxcIncludeHandler) ||
                riid == __uuidof(IUnknown))
            {
                *ppvObject = static_cast<IDxcIncludeHandler *>(this);
                AddRef();
                return S_OK;
            }
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++_ref_count;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG ref = --_ref_count;
            if (ref == 0)
                delete this;
            return ref;
        }

        HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFilename, IDxcBlob **ppIncludeSource) override
        {
            std::lock_guard<std::mutex> l(s_compile_lock);

            *ppIncludeSource = nullptr;

            auto try_load_file = [&](const WString &full_path) -> HRESULT
            {
                if (!FileManager::Exist(full_path))
                    return E_FAIL;

                auto [file_data, byte_size] = FileManager::ReadFile(full_path);
                ComPtr<IDxcBlobEncoding> blob;
                HRESULT hr = _utils->CreateBlob(file_data, (UINT32) byte_size, DXC_CP_UTF8, blob.GetAddressOf());

                delete[] file_data;

                if (FAILED(hr))
                    return hr;

                *ppIncludeSource = blob.Detach();
                _include_files.insert(PathUtils::FormatFilePath(full_path));
                return S_OK;
            };

            const auto search_paths = BuildIncludeSearchPaths(_cur_source_file_path);
            WString resolved_path;
            if (!TryResolveIncludeFile(_cur_source_file_path, pFilename, search_paths, resolved_path))
                return E_FAIL;

            return try_load_file(resolved_path);
        }
    };

    bool IsDxrLibraryTarget(const std::string &target)
    {
        return Ailu::StringUtils::BeginWith(target, "lib_");
    }

    bool NeedsEntryPoint(const Ailu::RHI::DX12::D3DShaderCompileDesc &desc)
    {
        return !desc._skip_entry_point && !desc._entry_point.empty() && !IsDxrLibraryTarget(desc._target);
    }

    void AddDxcDefine(std::vector<std::wstring> &define_strings, std::vector<LPCWSTR> &args, const std::wstring &name, const std::wstring &value = L"1")
    {
        define_strings.emplace_back(name + L"=" + value);
        args.push_back(L"-D");
        args.push_back(define_strings.back().c_str());
    }

    void BuildKeywordStrings(const Vector<D3D_SHADER_MACRO> &keywords, Vector<String> &keyword_str)
    {
        for (auto &kw: keywords)
        {
            if (kw.Name)
                keyword_str.emplace_back(kw.Name);
        }
    }

    bool TryCreateShaderReflection(IDxcUtils *utils, ID3DBlob *reflection_blob, ComPtr<ID3D12ShaderReflection> &shader_reflection)
    {
        if (!reflection_blob)
            return false;

        DxcBuffer rb{};
        rb.Ptr = reflection_blob->GetBufferPointer();
        rb.Size = reflection_blob->GetBufferSize();
        rb.Encoding = 0;
        return SUCCEEDED(utils->CreateReflection(&rb, IID_PPV_ARGS(shader_reflection.GetAddressOf())));
    }

    bool TryCreateShaderReflection(IDxcUtils *utils, ID3DBlob *reflection_blob, ComPtr<ID3D12LibraryReflection> &library_reflection)
    {
        if (!reflection_blob)
            return false;

        DxcBuffer rb{};
        rb.Ptr = reflection_blob->GetBufferPointer();
        rb.Size = reflection_blob->GetBufferSize();
        rb.Encoding = 0;
        return SUCCEEDED(utils->CreateReflection(&rb, IID_PPV_ARGS(library_reflection.GetAddressOf())));
    }

    bool RHI::DX12::ParseIncludeDependencies(const WString &source_file_path,
                                             std::set<WString> &include_files,
                                             const Vector<WString> &search_paths)
    {
        include_files.clear();
        std::set<WString> visited_files;
        return ParseIncludeDependenciesRecursive(source_file_path, search_paths, visited_files, include_files);
    }

    namespace RHI::DX12
    {
        bool CreateFromFileDXC(const D3DShaderCompileDesc &desc, D3DShaderCompileOutput &output)
        {
            Vector<String> keyword_str;
            BuildKeywordStrings(desc._keywords, keyword_str);

            const bool needs_entry_point = NeedsEntryPoint(desc);
            String unique = std::format("{}_{}_{}_{}_{}",
                                        ToChar(desc._filename),
                                        desc._entry_point,
                                        desc._target,
                                        needs_entry_point ? "entry" : "library",
                                        su::Join(keyword_str, "_"));

            u64 hash = std::hash<String>{}(unique);

            auto working = Application::GetWorkingPath();
            WString cached_shader_blob_path = working + std::format(L"cache/shader_cache/dxc/{}.dxil", hash);
            WString cached_reflection_blob_path = working + std::format(L"cache/shader_cache/dxc/{}.rft", hash);

            if (desc._is_load_cache &&
                FileManager::Exist(cached_shader_blob_path) &&
                FileManager::IsFileNewer(cached_shader_blob_path, desc._filename))
            {
                const bool load_shader_succeed = LoadBlobFromFile(cached_shader_blob_path, output._byte_code);
                AL_ASSERT(load_shader_succeed);
                ParseIncludeDependencies(desc._filename, output._include_files);

                if (FileManager::Exist(cached_reflection_blob_path))
                {
                    LoadBlobFromFile(cached_reflection_blob_path, output._reflection_blob);
                    if (output._reflection_blob)
                    {
                        ComPtr<IDxcUtils> utils;
                        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
                        if (!IsDxrLibraryTarget(desc._target))
                            TryCreateShaderReflection(utils.Get(), output._reflection_blob.Get(), output._shader_reflection);
                        else
                            TryCreateShaderReflection(utils.Get(), output._reflection_blob.Get(), output._library_reflection);
                    }
                    LOG_INFO(L"Loaded cached shader and reflection for {} from {}", desc._filename, cached_shader_blob_path);
                }
                return true;
            }

            ComPtr<IDxcUtils> utils;
            ComPtr<IDxcCompiler3> compiler;
            DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
            DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

            auto base_path = std::filesystem::path(desc._filename).parent_path().wstring();
            auto include = std::make_unique<DxcIncludeHandlerEx>(utils.Get());
            include->_cur_source_file_path = desc._filename;

            ComPtr<IDxcBlobEncoding> source;
            utils->LoadFile(desc._filename.c_str(), nullptr, &source);

            DxcBuffer src = {};
            src.Ptr = source->GetBufferPointer();
            src.Size = source->GetBufferSize();
            src.Encoding = DXC_CP_UTF8;

            std::vector<LPCWSTR> args;
            auto source_name_w = desc._filename;
            auto entry_point_w = ToWChar(desc._entry_point);
            auto target_w = ToWChar(desc._target);
            args.push_back(source_name_w.c_str());
            args.push_back(L"-I");
            args.push_back(base_path.c_str());
            if (needs_entry_point)
            {
                args.push_back(L"-E");
                args.push_back(entry_point_w.c_str());
            }
            args.push_back(L"-T");
            args.push_back(target_w.c_str());
            args.push_back(L"-Zi");
            args.push_back(L"-Zss");
#if SHADER_DEBUG_SYMBOLS
            args.push_back(L"-Od");
            args.push_back(L"-Qembed_debug");
#else
            args.push_back(L"-O3");
#endif

            std::vector<std::wstring> define_strings;
            for (auto &kw: desc._keywords)
            {
                if (!kw.Name)
                    continue;
                AddDxcDefine(define_strings, args, ToWChar(kw.Name), kw.Definition ? ToWChar(kw.Definition) : L"1");
            }
            AddDxcDefine(define_strings, args, L"SHADER_DXC");
            if (IsDxrLibraryTarget(desc._target))
            {
                AddDxcDefine(define_strings, args, L"AL_SHADER_INTEROP_CBUFFER_AS_STRUCT");
            }

            ComPtr<IDxcResult> result;
            compiler->Compile(&src, args.data(), (UINT) args.size(), include.get(), IID_PPV_ARGS(&result));

            HRESULT hr;
            result->GetStatus(&hr);
            if (FAILED(hr))
            {
                ComPtr<IDxcBlobUtf8> err;
                result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&err), nullptr);
                LOG_ERROR("DXC error: {}", err ? err->GetStringPointer() : "unknown dxc compile failure");
                return false;
            }

            ComPtr<IDxcBlob> shader_blob;
            hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shader_blob.GetAddressOf()), nullptr);
            if (FAILED(hr) || !CopyDxcBlob(shader_blob.Get(), output._byte_code))
            {
                LOG_ERROR(L"DXC object extraction failed for shader {}", desc._filename);
                return false;
            }
            FileManager::WriteFile(cached_shader_blob_path, false, reinterpret_cast<u8 *>(output._byte_code->GetBufferPointer()), output._byte_code->GetBufferSize());

            ComPtr<IDxcBlob> reflection_blob;
            hr = result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflection_blob.GetAddressOf()), nullptr);
            if (SUCCEEDED(hr) && reflection_blob && CopyDxcBlob(reflection_blob.Get(), output._reflection_blob))
            {
                if (!IsDxrLibraryTarget(desc._target))
                {
                    TryCreateShaderReflection(utils.Get(), output._reflection_blob.Get(), output._shader_reflection);
                }
                else
                {
                    TryCreateShaderReflection(utils.Get(), output._reflection_blob.Get(), output._library_reflection);
                }
                FileManager::WriteFile(cached_reflection_blob_path, false, reinterpret_cast<u8 *>(output._reflection_blob->GetBufferPointer()), output._reflection_blob->GetBufferSize());
            }
            else
            {
                LOG_WARNING(L"DXC generate reflection failed for shader {}", desc._filename);
            }

            ComPtr<IDxcBlob> pdb;
            hr = result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdb), nullptr);
            if (SUCCEEDED(hr) && pdb)
            {
                ComPtr<IDxcBlob> hash_blob;
                result->GetOutput(DXC_OUT_SHADER_HASH, IID_PPV_ARGS(&hash_blob), nullptr);
                const DxcShaderHash *shader_hash = reinterpret_cast<const DxcShaderHash *>(hash_blob->GetBufferPointer());
                std::wstring hash_str;
                for (int i = 0; i < 16; ++i)
                {
                    wchar_t buf[3];
                    swprintf(buf, 3, L"%02x", shader_hash->HashDigest[i]);
                    hash_str += buf;
                }
                WString pdb_cache_path = working + std::format(L"cache/shader_cache/dxc/{}.pdb", hash_str);
                FileManager::WriteFile(pdb_cache_path, false, (u8 *) pdb->GetBufferPointer(), pdb->GetBufferSize());
            }
            else
            {
                LOG_WARNING(L"DXC generate pdb failed for shader {}", desc._filename);
            }

            output._include_files = include->_include_files;
            return true;
        }

        bool CreateFromFileFXC(const D3DShaderCompileDesc &desc, D3DShaderCompileOutput &output)
        {
            if (IsDxrLibraryTarget(desc._target) || desc._skip_entry_point || desc._entry_point.empty())
            {
                LOG_ERROR(L"FXC does not support DXR library compilation for shader {}", desc._filename);
                return false;
            }

            D3DShaderInclude include;
            include._cur_source_file_path = desc._filename;
            Vector<String> keyword_str;
            BuildKeywordStrings(desc._keywords, keyword_str);

            String unique_str = std::format("{}_{}_{}_{}", ToChar(desc._filename), desc._entry_point, desc._target, su::Join(keyword_str, "_"));
            u64 shader_hash = std::hash<String>{}(unique_str);
            auto working_path = Application::GetWorkingPath();
            WString cached_blob_path = working_path + std::format(L"cache/shader_cache/fxc/{}.cso", shader_hash);
            if (desc._is_load_cache && FileManager::Exist(cached_blob_path) && FileManager::IsFileNewer(cached_blob_path, desc._filename))
            {
                LOG_INFO(L"[D3DShader compiler]: load cache: {},entry : {}", desc._filename, ToWChar(desc._entry_point));
                AL_ASSERT(LoadBlobFromFile(cached_blob_path, output._byte_code));
                ParseIncludeDependencies(desc._filename, output._include_files);
            }
            else
            {
                LOG_INFO(L"[D3DShader compiler]: compile : {},entry : {},defines: {}", desc._filename, ToWChar(desc._entry_point), ToWChar(su::Join(keyword_str, ",")));
                ID3DBlob *pErrorBlob = nullptr;
                UINT compileFlags = 0;
#if SHADER_DEBUG_SYMBOLS
                if (desc._target == Render::RenderConstants::kCSModel_5_0)
                {
                    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE | D3DCOMPILE_SKIP_OPTIMIZATION;
                }
                else
                {
                    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE;
                }
#endif
                D3DCompileFromFile(desc._filename.c_str(), desc._keywords.data(), &include, desc._entry_point.c_str(), desc._target.c_str(), compileFlags, 0, &output._byte_code, &pErrorBlob);
                if (pErrorBlob)
                {
                    String text(reinterpret_cast<const char *>(pErrorBlob->GetBufferPointer()));
                    std::istringstream iss(text);
                    std::string line;
                    while (std::getline(iss, line))
                    {
                        if (line.find("ERROR") != line.npos || line.find("error") != line.npos)
                        {
                            LOG_ERROR("{} when compile shader {}", line, ToChar(desc._filename));
                        }
                    }
                    pErrorBlob->Release();
                }
                if (output._byte_code != nullptr)
                {
                    FileManager::WriteFile(cached_blob_path, false, (u8 *) output._byte_code->GetBufferPointer(), output._byte_code->GetBufferSize());
                }
            }

            if (output._byte_code != nullptr)
            {
                D3DReflect(output._byte_code->GetBufferPointer(), output._byte_code->GetBufferSize(), IID_ID3D12ShaderReflection, (void **) &output._shader_reflection);
                output._include_files.insert(include._include_files.begin(), include._include_files.end());
                return true;
            }
            return false;
        }

        bool CreateFromFileDXC(const std::wstring &filename, const std::string &entryPoint, const std::string &target, const Vector<D3D_SHADER_MACRO> &keywords, ComPtr<ID3DBlob> &p_blob,
                               ComPtr<ID3D12ShaderReflection> &shader_reflection,
                               std::set<WString> &include_files,
                               bool is_load_cache)
        {
            D3DShaderCompileOutput output;
            if (!CreateFromFileDXC(D3DShaderCompileDesc{filename, entryPoint, target, keywords, is_load_cache, false}, output))
                return false;

            p_blob = output._byte_code;
            shader_reflection = output._shader_reflection;
            include_files = std::move(output._include_files);
            return true;
        }

        bool CreateFromFileFXC(const std::wstring &filename, const std::string &entryPoint, const std::string &target, const Vector<D3D_SHADER_MACRO> &keywords, ComPtr<ID3DBlob> &p_blob,
                               ComPtr<ID3D12ShaderReflection> &shader_reflection,
                               std::set<WString> &include_files,
                               bool is_load_cache)
        {
            D3DShaderCompileOutput output;
            if (!CreateFromFileFXC(D3DShaderCompileDesc{filename, entryPoint, target, keywords, is_load_cache, false}, output))
                return false;

            p_blob = output._byte_code;
            shader_reflection = output._shader_reflection;
            include_files = std::move(output._include_files);
            return true;
        }
    }// namespace RHI::DX12
}// namespace Ailu
