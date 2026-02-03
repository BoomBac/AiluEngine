#include "pch.h"
//#include <atlcomcli.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <mutex>

#include "Framework/Common/Application.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "GlobalMarco.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DShader.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/GraphicsPipelineStateObject.h"

#define SHADER_DXC 1

namespace Ailu::RHI::DX12
{
//-------------------------------------------------------------D3DShaderInclude------------------------------------------------------------------
#pragma region D3DShaderInclude
    class D3DShaderInclude : public ID3DInclude
    {
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
        u8 *_data;
        inline static std::mutex s_compile_lock;
    };
    HRESULT D3DShaderInclude::Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
    {
        auto pwd = PathUtils::ExtractAssetPath(PathUtils::Parent(_cur_source_file_path));
        _addi_include_pathes.insert(pwd);
        for (auto &include_path: _addi_include_pathes)
        {
            std::lock_guard<std::mutex> l(s_compile_lock);
            WString p;
            if ((const char *) pFileName[0] == ".")
            {
                p = PathUtils::ResolveRelPath(pFileName, _cur_source_file_path);
            }
            else
            {
                p = ResourceMgr::GetResSysPath(include_path) + ToWChar(pFileName);
            }
            if (FileManager::Exist(p))
            {
                auto [file_data, byte_size] = FileManager::ReadFile(p);
                _data = file_data;
                *ppData = _data;
                *pBytes = (u32) byte_size;
                _include_files.insert(p);
                return S_OK;
            }
            //else
            //{
            //    LOG_ERROR(L"D3DShaderInclude::Open: include file:{} not exist!", p);
            //}
            //AL_ASSERT(true);
        }
        AL_ASSERT(true);
        return E_FAIL;
    }

    HRESULT D3DShaderInclude::Close(LPCVOID pData)
    {
        delete[] pData;
        return S_OK;
    }

    class DxcIncludeHandlerEx final : public IDxcIncludeHandler
    {
    public:
        DxcIncludeHandlerEx(IDxcUtils *utils)
            : _utils(utils)
        {
            _utils->AddRef();
        }

        ~DxcIncludeHandlerEx()
        {
            if (_utils)
                _utils->Release();
        }

        // ================= IUnknown =================
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

        // ================= IDxcIncludeHandler =================
        HRESULT STDMETHODCALLTYPE LoadSource(
                LPCWSTR pFilename,
                IDxcBlob **ppIncludeSource) override
        {
            std::lock_guard<std::mutex> l(s_compile_lock);

            *ppIncludeSource = nullptr;

            // 当前源文件所在目录（和 FXC 逻辑一致）
            auto pwd = PathUtils::ExtractAssetPath(
                    PathUtils::Parent(_cur_source_file_path));

            _addi_include_pathes.insert(pwd);

            for (auto &include_path: _addi_include_pathes)
            {
                WString full_path;

                // 相对 include
                if (pFilename[0] == L'.')
                {
                    full_path = PathUtils::ResolveRelPath(pFilename, PathUtils::Parent(_cur_source_file_path));
                }
                else
                {
                    full_path = ResourceMgr::GetResSysPath(include_path) + pFilename;
                }

                if (!FileManager::Exist(full_path))
                    continue;

                // 读取文件
                auto [file_data, byte_size] = FileManager::ReadFile(full_path);
                //LOG_INFO(L"DxcIncludeHandlerEx::LoadSource: include file: {},src: {}", full_path,_cur_source_file_path);
                // 创建 DXC blob（DXC 会管理生命周期）
                ComPtr<IDxcBlobEncoding> blob;
                HRESULT hr = _utils->CreateBlob(
                        file_data,
                        (UINT32) byte_size,
                        DXC_CP_UTF8,
                        blob.GetAddressOf());

                delete[] file_data;

                if (FAILED(hr))
                    return hr;

                *ppIncludeSource = blob.Detach();
                _include_files.insert(PathUtils::FormatFilePath(full_path));
                return S_OK;
            }

            return E_FAIL;
        }

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
    };

#pragma endregion
    //-------------------------------------------------------------D3DShaderInclude------------------------------------------------------------------
#pragma region CompileUtils
    //shader model 6.0 and higher,can't see cbuffer info in PIX!!!!
    static bool CreateFromFileDXC(const std::wstring &filename, const std::string &entryPoint, const std::string &target, const Vector<D3D_SHADER_MACRO> &keywords, ComPtr<ID3DBlob> &p_blob,
                                  ComPtr<ID3D12ShaderReflection> &shader_reflection,
                                  std::set<WString> &include_files,
                                  bool is_load_cache = true)
    {
        // ===== hash & cache（和 FXC 一致） =====
        Vector<String> keyword_str;
        for (auto &kw: keywords)
            if (kw.Name) keyword_str.emplace_back(kw.Name);

        String unique = std::format("{}_{}_{}_{}",
                                    ToChar(filename),
                                    entryPoint,
                                    target,
                                    su::Join(keyword_str, "_"));

        u64 hash = std::hash<String>{}(unique);

        auto working = Application::GetWorkingPath();
        WString cached_shader_blob_path = working + std::format(L"cache/shader_cache/dxc/{}.dxil", hash);
        WString cached_reflection_blob_path = working + std::format(L"cache/shader_cache/dxc/{}.rft", hash);

        if (is_load_cache &&
            FileManager::Exist(cached_shader_blob_path) &&
            FileManager::IsFileNewer(cached_shader_blob_path, filename))
        {
            auto hr = D3DReadFileToBlob(cached_shader_blob_path.c_str(), p_blob.GetAddressOf());
            AL_ASSERT(SUCCEEDED(hr));
            ComPtr<ID3DBlob> refl_blob;
            D3DReadFileToBlob(cached_reflection_blob_path.c_str(), refl_blob.GetAddressOf());
            DxcBuffer rb{};
            rb.Ptr = refl_blob->GetBufferPointer();
            rb.Size = refl_blob->GetBufferSize();
            ComPtr<IDxcUtils> utils;
            DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
            utils->CreateReflection(&rb, IID_PPV_ARGS(shader_reflection.GetAddressOf()));

            AL_ASSERT(SUCCEEDED(hr));
            //ThrowIfFailed(hr);
            return true;
        }

        // ===== DXC init =====
        ComPtr<IDxcUtils> utils;
        ComPtr<IDxcCompiler3> compiler;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

        auto base_path = std::filesystem::path(filename).parent_path().wstring();
        auto include = std::make_unique<DxcIncludeHandlerEx>(utils.Get());
        include->_cur_source_file_path = filename;

        // ===== load source =====
        ComPtr<IDxcBlobEncoding> source;
        utils->LoadFile(filename.c_str(), nullptr, &source);

        DxcBuffer src = {};
        src.Ptr = source->GetBufferPointer();
        src.Size = source->GetBufferSize();
        src.Encoding = DXC_CP_UTF8;

        // ===== arguments =====
        std::vector<LPCWSTR> args;
        auto entry_point_w = ToWChar(entryPoint);
        auto target_w = ToWChar(target);
        args.push_back(L"-E");
        args.push_back(entry_point_w.c_str());
        args.push_back(L"-T");
        args.push_back(target_w.c_str());
        args.push_back(L"-Zi");
        args.push_back(L"-Zss");
#if defined(_DEBUG)
        args.push_back(L"-Od");
        args.push_back(L"-Qembed_debug");
#else
        args.push_back(L"-O3");
#endif

        // defines
        std::vector<std::wstring> define_strings;
        for (auto &kw: keywords)
        {
            if (!kw.Name) continue;
            define_strings.emplace_back(
                    ToWChar(kw.Name) + L"=" +
                    (kw.Definition ? ToWChar(kw.Definition) : L"1"));
            args.push_back(L"-D");
            args.push_back(define_strings.back().c_str());
        }
        args.push_back(L"-D");
        args.push_back(L"SHADER_DXC=1");

        // ===== compile =====
        ComPtr<IDxcResult> result;
        compiler->Compile(
                &src,
                args.data(),
                (UINT) args.size(),
                include.get(),
                IID_PPV_ARGS(&result));

        HRESULT hr;
        result->GetStatus(&hr);
        if (FAILED(hr))
        {
            ComPtr<IDxcBlobUtf8> err;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&err), nullptr);
            LOG_ERROR("DXC error: {}", err->GetStringPointer());
            return false;
        }

        // ===== output =====
        //shader blob
        result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&p_blob),nullptr);
        FileManager::WriteFile(cached_shader_blob_path, false,(u8 *) p_blob->GetBufferPointer(),p_blob->GetBufferSize());

        // reflection
        ComPtr<IDxcBlob> refl;
        hr = result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&refl), nullptr);

        if (SUCCEEDED(hr) && refl)
        {
            DxcBuffer rb{};
            rb.Ptr = refl->GetBufferPointer();
            rb.Size = refl->GetBufferSize();
            utils->CreateReflection(&rb, IID_PPV_ARGS(shader_reflection.GetAddressOf()));
            FileManager::WriteFile(cached_reflection_blob_path, false, (u8 *) refl->GetBufferPointer(), refl->GetBufferSize());
        }
        else
        {
            LOG_WARNING(L"DXC generate reflection failed for shader {}", filename);
        }
        //pdb
        ComPtr<IDxcBlob> pdb;
        hr = result->GetOutput(DXC_OUT_PDB,IID_PPV_ARGS(&pdb), nullptr);
        if (SUCCEEDED(hr) && pdb)
        {
            ComPtr<IDxcBlob> hash_blob;
            result->GetOutput(DXC_OUT_SHADER_HASH,IID_PPV_ARGS(&hash_blob), nullptr);
            const DxcShaderHash* shader_hash =reinterpret_cast<const DxcShaderHash*>(hash_blob->GetBufferPointer());
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
            LOG_WARNING(L"DXC generate pdb failed for shader {}", filename);
        }
        include_files = include->_include_files;
        return true;
    }


    static bool CreateFromFileFXC(const std::wstring &filename, const std::string &entryPoint, const std::string &pTarget, const Vector<D3D_SHADER_MACRO> &keywords, ComPtr<ID3DBlob> &p_blob,
                                  ComPtr<ID3D12ShaderReflection> &shader_reflection, std::set<WString> &include_files, bool is_load_cache = true)
    {
        D3DShaderInclude include;
        include._cur_source_file_path = filename;
        Vector<String> keyword_str;
        for (auto &kw: keywords)
        {
            if (kw.Name != nullptr)
                keyword_str.emplace_back(kw.Name);
        }
        String unique_str = std::format("{}_{}_{}", ToChar(filename), entryPoint, su::Join(keyword_str, "_"));
        std::hash<String> hash_fn;
        u64 shader_hash = hash_fn(unique_str);
        auto working_path = Application::GetWorkingPath();
        WString cached_blob_path = working_path + std::format(L"cache/shader_cache/fxc/{}.cso", shader_hash);
        if (is_load_cache && FileManager::Exist(cached_blob_path) && FileManager::IsFileNewer(cached_blob_path, filename))
        {
            LOG_INFO(L"[D3DShader compiler]: load cache: {},entry : {}", filename, ToWChar(entryPoint));
            ThrowIfFailed(D3DReadFileToBlob(cached_blob_path.c_str(), p_blob.GetAddressOf()));
        }
        else
        {
            LOG_INFO(L"[D3DShader compiler]: compile : {},entry : {},defines: {}", filename, ToWChar(entryPoint), ToWChar(su::Join(keyword_str, ",")));
            ID3DBlob *pErrorBlob = nullptr;
            //D3D_SHADER_MACRO macros[] = { {"D3D_COMPILE","1"},{NULL,NULL} };
            UINT compileFlags = 0;
#if defined(_DEBUG)
            if (pTarget == RenderConstants::kCSModel_5_0)
            {
                compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE | D3DCOMPILE_SKIP_OPTIMIZATION;//跳过优化的话，compute shader算brdf lut时值有点异常
            }
            else
            {
                // Enable better shader debugging with the graphics debugging tools.
                compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE;
            }
#else
            compileFlags = 0;
#endif
            D3DCompileFromFile(filename.c_str(), keywords.data(), &include, entryPoint.c_str(), pTarget.c_str(), compileFlags, 0, &p_blob, &pErrorBlob);
            if (pErrorBlob)
            {
                //OutputDebugStringA(reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()));
                String text(reinterpret_cast<const char *>(pErrorBlob->GetBufferPointer()));
                // 使用 std::stringstream 分割文本并提取每一行
                std::istringstream iss(text);
                //std::vector<std::string> lines;
                std::string line;
                while (std::getline(iss, line))
                {
                    if (line.find("ERROR") != line.npos || line.find("error") != line.npos)
                    {
                        LOG_ERROR("{} when compile shader {}", line, ToChar(filename))
                    }
                }
                pErrorBlob->Release();
            }
            if (p_blob != nullptr)
                FileManager::WriteFile(cached_blob_path, false, (u8 *) p_blob->GetBufferPointer(), p_blob->GetBufferSize());
        }

        if (p_blob != nullptr)
        {
            ID3D12ShaderReflection *pReflection = NULL;
            D3DReflect(p_blob->GetBufferPointer(), p_blob->GetBufferSize(), IID_ID3D12ShaderReflection, (void **) &shader_reflection);
            for (auto &p: include._include_files)
                include_files.insert(p);
            return true;
        }
        return false;
    }

    static D3D12_PRIMITIVE_TOPOLOGY ConvertTopologyToType(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
    {
        switch (type)
        {
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
                return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
                return D3D12_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_LINELIST;
            default:
                return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
        return D3D12_PRIMITIVE_TOPOLOGY::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

    static EShaderDateType GetShaderDataType(DXGI_FORMAT dx_format)
    {
        switch (dx_format)
        {
            case DXGI_FORMAT_R32G32B32_FLOAT:
                return EShaderDateType::kFloat3;
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
                return EShaderDateType::kFloat4;
            case DXGI_FORMAT_R32G32_FLOAT:
                return EShaderDateType::kFloat2;
        }
        AL_ASSERT_MSG(false, "Unsupported DXGI_FORMAT to ShaderDataType!");
        //LOG_ERROR("Unsupported DXGI_FORMAT to ShaderDataType!");
        return EShaderDateType::kBool;
    }

    static const Vector<CD3DX12_STATIC_SAMPLER_DESC> &CreateStaticSampler()
    {
        static Vector<CD3DX12_STATIC_SAMPLER_DESC> samplers{
                CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
                CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
                CD3DX12_STATIC_SAMPLER_DESC(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER),
                CD3DX12_STATIC_SAMPLER_DESC(3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
                CD3DX12_STATIC_SAMPLER_DESC(4, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
                CD3DX12_STATIC_SAMPLER_DESC(5, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER),
                CD3DX12_STATIC_SAMPLER_DESC(6, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 16, D3D12_COMPARISON_FUNC_LESS, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK),
                CD3DX12_STATIC_SAMPLER_DESC(7, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP)};
        samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        return samplers;
    }

    static std::pair<String, ShaderBindResourceInfo> ParserBindResource(D3D12_SHADER_INPUT_BIND_DESC bind_desc, EShaderType shader_type)
    {
        std::pair<String, ShaderBindResourceInfo> ret;
        auto res_type = bind_desc.Type;
        if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER)
        {
            ret = std::make_pair(bind_desc.Name, ShaderBindResourceInfo{EBindResDescType::kConstBuffer, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE)
        {
            ret = std::make_pair(bind_desc.Name, ShaderBindResourceInfo{EBindResDescType::kTexture2D, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER)
        {
            ret = std::make_pair(bind_desc.Name, ShaderBindResourceInfo{EBindResDescType::kSampler, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_STRUCTURED)
        {
            ret = std::make_pair(bind_desc.Name, ShaderBindResourceInfo{EBindResDescType::kBuffer, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWSTRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_APPEND_STRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_CONSUME_STRUCTURED || res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER)
        {
            ret = std::make_pair(bind_desc.Name, ShaderBindResourceInfo{EBindResDescType::kRWBuffer, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else if (res_type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWTYPED)
        {
            ret = std::make_pair(bind_desc.Name, ShaderBindResourceInfo{EBindResDescType::kUAVTexture2D, static_cast<uint16_t>(bind_desc.BindPoint), 255u, bind_desc.Name});
        }
        else
        {
            AL_ASSERT(false);
        }
        ret.second._register_space = bind_desc.Space;
        //ret.second._register_space = static_cast<u16>(shader_type);
        return ret;
    }

    static void ParserBindResourceAddiInfo(HashMap<String, ShaderBindResourceInfo> &bind_res_infos, String line, bool is_in_cbuf_scope)
    {
        line = line.find(";") != line.npos ? line.substr(0, line.find_first_of(";") + 1) : line;
        if (su::BeginWith(line, "Texture2D"))
        {
            size_t name_begin = line.find_first_of("D") + 1;
            size_t name_end = line.find_first_of(":") - 1;
            String tex_name = line.substr(name_begin, name_end - name_begin);
        }
        else if (su::BeginWith(line, "TextureCube"))
        {
            size_t name_begin = line.find_last_of("e") + 1;
            size_t name_end = line.find_first_of(":") - 1;
            String tex_name = line.substr(name_begin, name_end - name_begin);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kCubeMap;
            }
        }
        else if (su::BeginWith(line, "TEXTURECUBE"))
        {
            size_t name_begin = line.find_first_of("(") + 1;
            size_t name_end = line.find_last_of(",");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kCubeMap;
            }
        }
        else if (su::BeginWith(line, "Texture3D"))
        {
            auto eol = line.find_first_of("<") == line.length() ? "D" : ">";
            size_t name_begin = line.find_first_of(eol) + 1;
            size_t name_end = line.find_first_of(";");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kTexture3D;
            }
        }
        else if (su::BeginWith(line, "TEXTURE3D"))
        {
            size_t name_begin = line.find_first_of("(") + 1;
            size_t name_end = line.find_first_of(")");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kTexture3D;
            }
        }
        else if (su::BeginWith(line, "RWTexture3D"))
        {
            auto eol = line.find_first_of("<") == line.length() ? "D" : ">";
            size_t name_begin = line.find_first_of(eol) + 1;
            size_t name_end = line.find_first_of(";");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kRWTexture3D;
            }
        }
        else if (su::BeginWith(line, "RWTEXTURE3D"))//RWTEXTURE3D(_OutNoise3D,float4)
        {
            size_t name_begin = line.find_first_of('(') + 1;
            size_t name_end = line.find_first_of(",");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kRWTexture3D;
            }
        }
        else if (su::BeginWith(line, "RWBuffer"))
        {
            auto eol = line.find_first_of("<") == line.length() ? "r" : ">";
            size_t name_begin = line.find_first_of(eol) + 1;
            size_t name_end = line.find_first_of(";");
            String tex_name = line.substr(name_begin, name_end - name_begin);
            su::RemoveSpaces(tex_name);
            auto it = bind_res_infos.find(tex_name);
            if (it != bind_res_infos.end())
            {
                it->second._res_type = EBindResDescType::kRWBuffer;
            }
        }
        else if (su::BeginWith(line, "uint"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._array_size = array_size;
                    if (type_name == "uint4" || type_name == "uint3" || type_name == "uint2")
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferUInts);
                    else
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferUInt);
                }
            }
        }
        else if (su::BeginWith(line, "int"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._array_size = array_size;
                    if (type_name == "int4" || type_name == "int3" || type_name == "int2")
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferInts);
                    else
                        c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferInt);
                }
            }
        }
        else if (su::BeginWith(line, "float"))//默认所有4字节大小的均为float，所以这里不需要调整其类型
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._array_size = array_size;
                }
            }
        }
        else if (su::BeginWith(line, "bool"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferBool);
                }
            }
        }
        else if (su::BeginWith(line, "float4x4"))
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferMatrix);
                    c._array_size = array_size;
                }
            }
        }
        else if (line.find("[") != line.npos && is_in_cbuf_scope)//cbuf内结构体解析支持
        {
            std::regex pattern(R"(^(\w+)\s+(\w+)\s*(\[\d*\])?\s*;)");
            std::smatch matches;
            if (std::regex_match(line, matches, pattern))
            {
                const auto &type_name = matches[1].str();
                const auto &value_name = matches[2].str();
                u8 array_size = matches[3].str().empty() ? 0u : (u8) std::stoi(matches[3].str().substr(1, matches[3].str().size() - 2));
                auto it = bind_res_infos.find(value_name);
                if (it != bind_res_infos.end())
                {
                    auto &c = it->second;
                    c._res_type = (EBindResDescType) (EBindResDescType::kCBufferAttribute | EBindResDescType::kCBufferFloats);
                    c._array_size = array_size;
                }
            }
        }
        else
        {
        };
    }

    static ShaderBindResourceInfo ParserBindVariable(const D3D12_SHADER_VARIABLE_DESC &desc)
    {
        u16 offset = (u16) desc.StartOffset;
        u16 size = (u16) desc.Size;
        u32 variable_info = 0u;
        variable_info |= offset;
        variable_info <<= 16;
        variable_info |= size;
        auto value_type = EBindResDescType::kCBufferAttribute;
        if (size == 4) value_type = (EBindResDescType) (EBindResDescType::kCBufferFloat | value_type);
        else if (size == 16 || size == 12 || size == 8)
            value_type = (EBindResDescType) (EBindResDescType::kCBufferFloats | value_type);
        else if (size == 64)
            value_type = (EBindResDescType) (EBindResDescType::kCBufferMatrix | value_type);
        else
        {
        }
        auto info = ShaderBindResourceInfo{value_type, variable_info, 255u, desc.Name};
        return info;
    }

    static bool IsValidMacroName(const String &s)
    {
        if (s.empty()) return false;
        if (!(isalpha(s[0]) || s[0] == '_')) return false;
        for (char c: s)
            if (!(isalnum(c) || c == '_'))
                return false;
        return true;
    }


    static Vector<D3D_SHADER_MACRO> ConstructVariantMarcos(const std::set<String> &kw_seqs)
    {
        Vector<D3D_SHADER_MACRO> v;
        for (const auto &kw: Shader::GetPreDefinedMacros())
            v.emplace_back(D3D_SHADER_MACRO{kw.c_str(), "1"});
        for (auto &kw: kw_seqs)
        {
            if (kw != "_")
            {
                v.emplace_back(D3D_SHADER_MACRO{kw.c_str(), "1"});
            }
        }
        v.emplace_back(D3D_SHADER_MACRO{NULL, NULL});
        for (auto &m: v)
        {
            if (m.Name)
            {
                if (!IsValidMacroName(m.Name))
                {
                    LOG_ERROR("Invalid macro name in shader variant: {}", m.Name);
                }
            }
        }

        return v;
    }

#pragma endregion

#pragma region D3DShader
    D3DShader::D3DShader(const WString &sys_path) : Shader(sys_path)
    {
        Compile();
    }
    D3DShader::~D3DShader()
    {
    }
    /*
    void D3DShader::GenerateInternalPSO(u16 pass_index, ShaderVariantHash variant_hash)
    {
        struct SrvUavRangeBuildInfo
        {
            D3D12_DESCRIPTOR_RANGE_TYPE type; // SRV / UAV
            u32 space;

            u32 base_register = UINT32_MAX;
            u32 max_register  = 0;

            Vector<ShaderBindResourceInfo*> resources;
        };
        AL_ASSERT(pass_index < _passes.size());
        auto& pass_variant = _passes[pass_index]._variants[variant_hash];
        auto& bind_infos   = pass_variant._bind_res_infos;
        auto& root_params = _pass_elements[pass_index]._variants[variant_hash]._root_parameters;
        root_params.resize(32);

        // ---------------------------------------------------------------------
        // Root signature version
        // ---------------------------------------------------------------------
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData{};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        auto device = static_cast<D3DContext&>(GraphicsContext::Get()).GetDevice();
        if (FAILED(device->CheckFeatureSupport(
                D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        //CD3DX12_ROOT_PARAMETER1 root_params[32]{};
        CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
        u32 root_param_index = 0;

        // ---------------------------------------------------------------------
        // 1. CBV
        // ---------------------------------------------------------------------
        auto bind_cbv = [&](const char* name, u32 b_reg)->u32
        {
            auto it = bind_infos.find(name);
            if (it == bind_infos.end()) 
                return UINT32_MAX;
            it->second._bind_slot = root_param_index;
            root_params[root_param_index++]
                .InitAsConstantBufferView(b_reg);
                return root_param_index-1;
        };

        bind_infos[RenderConstants::kCBufNamePerObject]._bind_slot = bind_cbv(RenderConstants::kCBufNamePerObject.c_str(),0);
        bind_infos[RenderConstants::kCBufNamePerMaterial]._bind_slot = bind_cbv(RenderConstants::kCBufNamePerMaterial.c_str(),1);
        bind_infos[RenderConstants::kCBufNamePerScene]._bind_slot = bind_cbv(RenderConstants::kCBufNamePerScene.c_str(),2);
        bind_infos[RenderConstants::kCBufNamePerCamera]._bind_slot = bind_cbv(RenderConstants::kCBufNamePerCamera.c_str(),3);

        // ---------------------------------------------------------------------
        // 2. 收集 SRV / UAV（按 type + space 分组）
        // ---------------------------------------------------------------------
        Vector<SrvUavRangeBuildInfo> range_builders;

        auto find_or_create_range = [&](D3D12_DESCRIPTOR_RANGE_TYPE type, u32 space)
            -> SrvUavRangeBuildInfo&
        {
            for (auto& r : range_builders)
            {
                if (r.type == type && r.space == space)
                    return r;
            }
            range_builders.push_back({ type, space });
            return range_builders.back();
        };

        for (auto& [name, desc] : bind_infos)
        {
            switch (desc._res_type)
            {
                case EBindResDescType::kTexture2D:
                case EBindResDescType::kTexture3D:
                case EBindResDescType::kCubeMap:
                case EBindResDescType::kBuffer:
                {
                    auto& r = find_or_create_range(
                        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                        desc._register_space);

                    r.base_register = std::min(r.base_register, desc._res_slot);
                    r.max_register  = std::max(r.max_register,  desc._res_slot);
                    r.resources.push_back(&desc);
                }
                break;

                case EBindResDescType::kUAVTexture2D:
                case EBindResDescType::kRWBuffer:
                {
                    auto& r = find_or_create_range(
                        D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                        desc._register_space);

                    r.base_register = std::min(r.base_register, desc._res_slot);
                    r.max_register  = std::max(r.max_register,  desc._res_slot);
                    r.resources.push_back(&desc);
                }
                break;

                default:
                    break;
            }
        }

        // ---------------------------------------------------------------------
        // 3. 生成 descriptor tables（真正关键的地方）
        // ---------------------------------------------------------------------
        for (auto& r : range_builders)
        {
            const u32 num_desc = r.max_register - r.base_register + 1;

            ranges[root_param_index].Init(
                r.type,
                num_desc,
                r.base_register,
                r.space,
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

            root_params[root_param_index]
                .InitAsDescriptorTable(1, &ranges[root_param_index],
                                    D3D12_SHADER_VISIBILITY_ALL);

            // 所有落在这个 range 里的资源，共享同一个 root slot
            for (auto* res : r.resources)
            {
                res->_bind_slot = root_param_index;
            }

            ++root_param_index;
        }

        // ---------------------------------------------------------------------
        // 4. Root Signature flags
        // ---------------------------------------------------------------------
        D3D12_ROOT_SIGNATURE_FLAGS flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;

        if (_pass_elements[pass_index]._variants[variant_hash]._p_gblob == nullptr)
            flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        auto samplers = CreateStaticSampler();

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rs_desc;
        rs_desc.Init_1_1(
            root_param_index,
            root_params.data(),
            (UINT)samplers.size(),
            samplers.data(),
            flags);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
            &rs_desc, featureData.HighestVersion, &sig, &err));

        if (err)
            LOG_ERROR("RootSignature error: {}", (char*)err->GetBufferPointer());

        ThrowIfFailed(device->CreateRootSignature(
            0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&_pass_elements[pass_index]._variants[variant_hash]._p_sig)));
    }
*/

    void D3DShader::GenerateInternalPSO(u16 pass_index, ShaderVariantHash variant_hash)
    {
        AL_ASSERT(pass_index < _passes.size());
        AL_ASSERT(_passes[pass_index]._variants.contains(variant_hash));
        AL_ASSERT(pass_index < _pass_elements.size());
        AL_ASSERT(_pass_elements[pass_index]._variants.contains(variant_hash));
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        auto device = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice();
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) { featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0; }
        CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
        auto& rootParameters = _pass_elements[pass_index]._variants[variant_hash]._root_parameters;
        rootParameters.resize(32);
        int cbuf_mask = 0, texture_count = 0;
        auto &pass_variant = _passes[pass_index]._variants[variant_hash];
        auto &variant_bind_res_info = pass_variant._bind_res_infos;
        for (auto it = variant_bind_res_info.begin(); it != variant_bind_res_info.end(); it++)
        {
            auto &desc = it->second;//if (desc._res_type == EBindResDescType::kCBufferAttribute) continue;
            if (desc._res_type == EBindResDescType::kConstBuffer)
            {
                if (desc._name == RenderConstants::kCBufNamePerObject) cbuf_mask |= 0x01;
                else if (desc._name == RenderConstants::kCBufNamePerMaterial)
                    cbuf_mask |= 0x02;
                else if (desc._name == RenderConstants::kCBufNamePerCamera)
                    cbuf_mask |= 0x04;
                else if (desc._name == RenderConstants::kCBufNamePerScene)
                    cbuf_mask |= 0x08;
            }
        }
        u8 root_param_index = 0;
        if (cbuf_mask & 0x01)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerObject]._bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(0u);
        }
        if (cbuf_mask & 0x02)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerMaterial]._bind_slot = root_param_index;
            pass_variant._per_mat_buf_bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(1u);
        }
        if (cbuf_mask & 0x04)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerCamera]._bind_slot = root_param_index;
            pass_variant._per_pass_buf_bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(3u);
        }
        if (cbuf_mask & 0x08)
        {
            variant_bind_res_info[RenderConstants::kCBufNamePerScene]._bind_slot = root_param_index;
            pass_variant._per_frame_buf_bind_slot = root_param_index;
            rootParameters[root_param_index++].InitAsConstantBufferView(2u);
        }
        for (auto it = variant_bind_res_info.begin(); it != variant_bind_res_info.end(); it++)
        {
            auto &desc = it->second;
            switch (desc._res_type)
            {
                case EBindResDescType::kTexture2D:
                case EBindResDescType::kUAVTexture2D:
                case EBindResDescType::kTexture3D:
                case EBindResDescType::kCubeMap:
                {
                    ++texture_count;
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index;
                    ++root_param_index;
                }
                break;
                case EBindResDescType::kRWBuffer:
                {
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index;
                    ++root_param_index;
                }
                break;
                case EBindResDescType::kBuffer:
                {
                    ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space);
                    rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                    desc._bind_slot = root_param_index;
                    ++root_param_index;
                }
                break;
                default:
                    break;
            }
        }
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
        if (_pass_elements[pass_index]._variants[variant_hash]._p_gblob == nullptr) rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        auto samplers = CreateStaticSampler();
        rootSignatureDesc.Init_1_1(root_param_index, rootParameters.data(), static_cast<UINT>(samplers.size()), samplers.data(), rootSignatureFlags);
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        auto hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if (error) { LOG_ERROR("Root signature error: {}", (const char *) error->GetBufferPointer()); }
        ThrowIfFailed(hr);//如果参数一致，实际上会从池中返回已有的根签名，这就意味着在使用一个重复的根签名之前，需要清空其绑定的资源 
        ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&_pass_elements[pass_index]._variants[variant_hash]._p_sig)));
    }

    bool D3DShader::RHICompileImpl(u16 pass_index, ShaderVariantHash variant_hash)
    {
        bool succeed = true;
        ComPtr<ID3DBlob> tmp_p_vblob;
        ComPtr<ID3DBlob> tmp_p_pblob;
        ComPtr<ID3DBlob> tmp_p_gblob;
        ComPtr<ID3D12ShaderReflection> tmp_p_vreflect, tmp_p_preflect, tmp_p_greflect;
        Vector<D3D_SHADER_MACRO> keyword_defines;
        try
        {
            //应该可以把全部元素都清空，pso在运行时似乎没有再引用这里的东西
            if (!_is_pass_elements_init.load())
            {
                Reset();
                _is_pass_elements_init.store(true);
            }
            auto &pass = _passes[pass_index];
            keyword_defines = ConstructVariantMarcos(pass._variants[variant_hash]._active_keywords);
#ifdef SHADER_DXC
            succeed &= CreateFromFileDXC(pass._vert_src_file, pass._vert_entry, RenderConstants::kVSModel_6_1, keyword_defines, tmp_p_vblob, tmp_p_vreflect, pass._source_files, _is_first_compile);
            succeed &= CreateFromFileDXC(pass._pixel_src_file, pass._pixel_entry, RenderConstants::kPSModel_6_1, keyword_defines, tmp_p_pblob, tmp_p_preflect, pass._source_files, _is_first_compile);
            if (!pass._geometry_entry.empty())
                succeed &= CreateFromFileDXC(pass._geom_src_file, pass._geometry_entry, RenderConstants::kGSModel_6_1, keyword_defines, tmp_p_gblob, tmp_p_greflect, pass._source_files, _is_first_compile);
            _is_first_compile = false;
#else
            succeed &= CreateFromFileFXC(pass._vert_src_file, pass._vert_entry, RenderConstants::kVSModel_5_0, keyword_defines, tmp_p_vblob, tmp_p_vreflect, pass._source_files, _is_first_compile);
            succeed &= CreateFromFileFXC(pass._pixel_src_file, pass._pixel_entry, RenderConstants::kPSModel_5_0, keyword_defines, tmp_p_pblob, tmp_p_preflect, pass._source_files, _is_first_compile);
            if (!pass._geometry_entry.empty())
                succeed &= CreateFromFileFXC(pass._geom_src_file, pass._geometry_entry, RenderConstants::kGSModel_5_0, keyword_defines, tmp_p_gblob, tmp_p_greflect, pass._source_files, _is_first_compile);
            _is_first_compile = false;
#endif// SHADER_DXC
        }
        catch (const std::exception &)
        {
            succeed = false;
            LOG_ERROR(L"Compile shader with src {0} failed!", _src_file_path);
        }
        if (succeed)
        {
            _pass_elements[pass_index]._variants.insert(std::make_pair(variant_hash, D3DShaderElement::D3DVariantElement()));
            LoadShaderReflection(pass_index, variant_hash, tmp_p_vreflect.Get(), tmp_p_preflect.Get());
            _pass_elements[pass_index]._variants[variant_hash]._p_vblob = tmp_p_vblob;
            _pass_elements[pass_index]._variants[variant_hash]._p_pblob = tmp_p_pblob;
            _pass_elements[pass_index]._variants[variant_hash]._p_gblob = tmp_p_gblob;
            _pass_elements[pass_index]._variants[variant_hash]._keyword_defines = keyword_defines;
            GenerateInternalPSO(pass_index, variant_hash);
        }
        return succeed;
    }

    void D3DShader::LoadAdditionalShaderReflection(const WString &sys_path, u16 pass_index, ShaderVariantHash variant_hash)
    {
        using namespace std;
        namespace su = StringUtils;
        namespace fs = std::filesystem;
        ifstream src(sys_path, ios::in);
        string line;
        vector<string> lines;
        List<WString> cur_file_head_files{};
        WString parent_path = su::SubStrRange(_src_file_path, 0, _src_file_path.find_last_of(L"/"));
        bool is_in_cbuf_scope = false;//为了支持cbuffer中对于结构体数组的解析
        while (getline(src, line))
        {
            line = su::Trim(line);
            if (su::BeginWith(line, "//"))
            {
                lines.emplace_back(line);
                continue;
            }
            if (su::BeginWith(line, "CBUFFER_START"))
                is_in_cbuf_scope = true;
            if (su::BeginWith(line, "CBUFFER_END") && is_in_cbuf_scope)
                is_in_cbuf_scope = false;
            ParserBindResourceAddiInfo(_passes[pass_index]._variants[variant_hash]._bind_res_infos, line, is_in_cbuf_scope);
            lines.emplace_back(line);
        }
        src.close();
        fs::path src_path(sys_path);
        fs::path pwd = src_path.parent_path();
        for (auto &head_file: cur_file_head_files)
        {
            fs::path temp = pwd;
            temp.append(head_file);
            LoadAdditionalShaderReflection(temp.wstring(), pass_index, variant_hash);
        }
    }

    std::pair<D3D12_INPUT_ELEMENT_DESC *, u8> D3DShader::GetVertexInputLayout(u16 pass_index, ShaderVariantHash variant_hash)
    {
        return std::make_pair(_pass_elements[pass_index]._variants[variant_hash]._vertex_input_layout, _passes[pass_index]._variants[variant_hash]._vertex_input_num);
    }

    void D3DShader::Reset()
    {
        //_pass_elements.clear();
        _pass_elements.resize(_passes.size());
        //for (int i = 0; i < _passes.size(); i++)
        //{
        //	_pass_elements[i] = D3DShaderElement();
        //	//if (_pass_elements[i]._p_vblob != nullptr) _pass_elements[i]._p_vblob.Reset();
        //	//if (_pass_elements[i]._p_pblob != nullptr) _pass_elements[i]._p_pblob.Reset();
        //	//if (_pass_elements[i]._p_v_reflection != nullptr) _pass_elements[i]._p_v_reflection.Reset();
        //	//if (_pass_elements[i]._p_p_reflection != nullptr) _pass_elements[i]._p_p_reflection.Reset();
        //	//memset(_pass_elements[i]._vertex_input_layout, 0, sizeof(D3D12_INPUT_ELEMENT_DESC) * RenderConstants::kMaxVertexAttrNum);
        //	//_passes[i]._vertex_input_num = 0u;
        //	_passes[i]._pipeline_topology = ETopology::kTriangle;
        //}
    }
    void D3DShader::LoadShaderReflection(u16 pass_index, ShaderVariantHash variant_hash, ID3D12ShaderReflection *ref_vs, ID3D12ShaderReflection *ref_ps)
    {
        AL_ASSERT(pass_index < _passes.size());
        auto &pass = _passes[pass_index];
        AL_ASSERT(pass._variants.contains(variant_hash));
        auto &pass_variant = pass._variants[variant_hash];
        D3D12_SHADER_DESC desc{};
        //parser vs reflecton
        {
            ref_vs->GetDesc(&desc);
            if (desc.InputParameters > 10)
            {
                AL_ASSERT_MSG(false, "LayoutDesc count must less than 10");
                return;
            }
            Vector<VertexBufferLayoutDesc> vb_input_desc{};
            for (u32 i = 0u; i < desc.InputParameters; i++)
            {
                D3D12_SIGNATURE_PARAMETER_DESC input_desc{};
                ref_vs->GetInputParameterDesc(i, &input_desc);
                EShaderDateType data_type = D3DConvertUtils::GetShaderDataType(input_desc.SemanticName, input_desc.Mask);
                _pass_elements[pass_index]._variants[variant_hash]._vertex_input_layout[i] = D3D12_INPUT_ELEMENT_DESC{input_desc.SemanticName, 0, D3DConvertUtils::GetGXGIFormatByShaderDataType(data_type), i, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
                if (data_type != EShaderDateType::kNone)
                {
                    vb_input_desc.emplace_back(input_desc.SemanticName, data_type, input_desc.Register, input_desc.SemanticIndex);
                }
                else
                {
                    LOG_WARNING("LoadShaderReflection {} skip input element: {}", _name, input_desc.SemanticName);
                }
            }
            pass_variant._vertex_input_num = (u8) vb_input_desc.size();
            pass_variant._pipeline_input_layout = VertexBufferLayout(vb_input_desc);
            pass_variant._bind_res_infos.clear();
            for (u32 i = 0u; i < desc.BoundResources; i++)
            {
                D3D12_SHADER_INPUT_BIND_DESC bind_desc{};
                ref_vs->GetResourceBindingDesc(i, &bind_desc);
                pass_variant._bind_res_infos.insert(ParserBindResource(bind_desc, EShaderType::kVertex));
            }
            for (u32 i = 0u; i < desc.ConstantBuffers; i++)
            {
                auto cbuf = ref_vs->GetConstantBufferByIndex(i);
                D3D12_SHADER_BUFFER_DESC desc{};
                cbuf->GetDesc(&desc);
                u8 flag = ShaderBindResourceInfo::GetBindResourceFlag(desc.Name);
                for (u32 j = 0u; j < desc.Variables; j++)
                {
                    auto variable = cbuf->GetVariableByIndex(j);
                    D3D12_SHADER_VARIABLE_DESC vdesc{};
                    variable->GetDesc(&vdesc);
                    auto info = ParserBindVariable(vdesc);
                    info._bind_flag = flag;
                    info._p_root_cbuf = &pass_variant._bind_res_infos.find(desc.Name)->second;
                    info._p_root_cbuf->_cbuf_size += vdesc.Size;
                    pass_variant._bind_res_infos.insert(std::make_pair(vdesc.Name, info));
                }
            }
        }
        //parser ps reflecton
        {
            ref_ps->GetDesc(&desc);
            for (u32 i = 0u; i < desc.BoundResources; i++)
            {
                D3D12_SHADER_INPUT_BIND_DESC bind_desc{};
                ref_ps->GetResourceBindingDesc(i, &bind_desc);
                //LOG_INFO("Name:{},Slot{},Space{}", bind_desc.Name, bind_desc.BindPoint, bind_desc.Space);
                pass_variant._bind_res_infos.insert(ParserBindResource(bind_desc, EShaderType::kPixel));
            }
            for (u32 i = 0u; i < desc.ConstantBuffers; i++)
            {
                auto cbuf = ref_ps->GetConstantBufferByIndex(i);
                D3D12_SHADER_BUFFER_DESC desc{};
                cbuf->GetDesc(&desc);
                u8 flag = ShaderBindResourceInfo::GetBindResourceFlag(desc.Name);
                for (u32 j = 0u; j < desc.Variables; j++)
                {
                    auto variable = cbuf->GetVariableByIndex(j);
                    D3D12_SHADER_VARIABLE_DESC vdesc{};
                    variable->GetDesc(&vdesc);
                    auto info = ParserBindVariable(vdesc);
                    info._bind_flag = flag;
                    info._p_root_cbuf = &pass_variant._bind_res_infos.find(desc.Name)->second;
                    info._p_root_cbuf->_cbuf_size += vdesc.Size;
                    pass_variant._bind_res_infos.insert(std::make_pair(vdesc.Name, info));
                }
            }
        }
        //parser additon info
        LoadAdditionalShaderReflection(_src_file_path, pass_index, variant_hash);
        //for (auto &p: pass._source_files)
        //    LoadAdditionalShaderReflection(p, pass_index, variant_hash);
    }

    void D3DShader::Bind(u16 pass_index, ShaderVariantHash variant_hash)
    {
        Shader::Bind(pass_index, variant_hash);
    }

    void *D3DShader::GetByteCode(EShaderType type, u16 pass_index, ShaderVariantHash variant_hash)
    {
        switch (type)
        {
            case EShaderType::kVertex:
                return reinterpret_cast<void *>(_pass_elements[pass_index]._variants[variant_hash]._p_vblob.Get());
            case EShaderType::kPixel:
                return reinterpret_cast<void *>(_pass_elements[pass_index]._variants[variant_hash]._p_pblob.Get());
            case EShaderType::kGeometry:
                return reinterpret_cast<void *>(_pass_elements[pass_index]._variants[variant_hash]._p_gblob.Get());
        }
        return nullptr;
    }

    ID3D12RootSignature *D3DShader::GetSignature(u16 pass_index, ShaderVariantHash variant_hash)
    {
        return _pass_elements[pass_index]._variants[variant_hash]._p_sig.Get();
    }

    Vector<CD3DX12_ROOT_PARAMETER1> &D3DShader::GetRootParameters(u16 pass_index, ShaderVariantHash variant_hash)
    {
        return _pass_elements[pass_index]._variants[variant_hash]._root_parameters;
    }

#pragma endregion

#pragma region D3DComputeShader
    //-------------------------------------------------------------------------------D3DComputeShader---------------------------------------------------------------------------
    D3DComputeShader::D3DComputeShader(const WString &sys_path) : ComputeShader(sys_path)
    {
        Compile();
    }

    void D3DComputeShader::Bind(RHICommandBuffer *cmd, u16 kernel)
    {
        if (!_is_valid && kernel >= _kernels.size())
        {
            LOG_WARNING("ComputeShader or kernel id is not valid!");
            return;
        }
        ComputeShader::Bind(cmd, kernel);
        AL_ASSERT(!_bind_state.empty());
        std::unique_lock lock(_state_mutex);
        auto &cur_state = _bind_state.front();
        if (_variant_state[kernel][cur_state._variant_hash] != EShaderVariantState::kReady)
            return;
        auto d3dcmd = static_cast<D3DCommandBuffer *>(cmd)->NativeCmdList();
        auto &d3d_ele = _elements[kernel]._variants[cur_state._variant_hash];
        auto &cs_ele = _kernels[kernel]._variants[cur_state._variant_hash];
        d3dcmd->SetPipelineState(d3d_ele._pso.Get());
        d3dcmd->SetComputeRootSignature(d3d_ele._p_sig.Get());
        for (u16 i = 0; i <= cur_state._max_bind_slot; i++)
        {
            auto it = std::find_if(cs_ele._bind_res_infos.begin(), cs_ele._bind_res_infos.end(), [&](const auto &it) -> bool
                                   { return it.second._bind_slot == i; });
            if (it != cs_ele._bind_res_infos.end())
            {
                auto &bind_info = it->second;
                auto &view_info = cur_state._bind_params[bind_info._bind_slot];
                GpuResource *bind_res = cur_state._bind_res[bind_info._bind_slot];
                if (bind_res == nullptr)
                    continue;
                static_cast<D3DCommandBuffer *>(cmd)->MarkUsedResource(bind_res);
                if (bind_info._res_type == EBindResDescType::kTexture2D)
                {
                    auto tex = static_cast<Texture2D *>(bind_res);
                    u16 view_index = view_info._view_index == (u16) -1 ? tex->CalculateViewIndex(Texture::ETextureViewType::kSRV, view_info._face, view_info._mipmap, 0) : view_info._view_index;
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = view_index;
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kUAVTexture2D)
                {
                    auto tex = static_cast<Texture *>(bind_res);
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = tex->CalculateViewIndex(Texture::ETextureViewType::kUAV, view_info._face, view_info._mipmap, 0);
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kTexture3D)
                {
                    auto tex = static_cast<Texture3D *>(bind_res);
                    u16 view_index = view_info._view_index == (u16) -1 ? tex->CalculateViewIndex(Texture::ETextureViewType::kSRV, view_info._face, view_info._mipmap, view_info._slice) : view_info._view_index;
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = view_index;
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kRWTexture3D)
                {
                    auto tex = static_cast<Texture3D *>(bind_res);
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._params._texture_binder._sub_res = view_info._sub_res;
                    params._params._texture_binder._view_idx = tex->CalculateViewIndex(Texture::ETextureViewType::kUAV, view_info._face, view_info._mipmap, view_info._slice);
                    params._slot = bind_info._bind_slot;
                    tex->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kRWBuffer)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    params._is_random_access = true;
                    bind_res->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kBuffer)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    bind_res->Bind(cmd, params);
                }
                else if (bind_info._res_type == EBindResDescType::kConstBuffer)
                {
                    BindParams params;
                    params._is_compute_pipeline = true;
                    params._slot = bind_info._bind_slot;
                    bind_res->Bind(cmd, params);
                }
                else
                {
                    AL_ASSERT(false);
                }
            }
        }
        if (d3d_ele._has_bindless_texture2d)
        {
            d3dcmd->SetComputeRootDescriptorTable(0, D3DDescriptorMgr::Get().GetBindlessSRVBaseGpuHandle());
        }
        _bind_state.pop();
        //d3dcmd->Dispatch(thread_group_x, thread_group_y, thread_group_z);
    }


    void D3DComputeShader::LoadReflectionInfo(ID3D12ShaderReflection *p_reflect, u16 kernel_index, ShaderVariantHash variant_hash)
    {
        auto &cs_ele = _kernels[kernel_index]._variants[variant_hash];
        D3D12_SHADER_DESC desc{};
        cs_ele._temp_bind_res_infos.clear();
        //parser vs reflecton
        p_reflect->GetDesc(&desc);
        p_reflect->GetThreadGroupSize(&_kernels[kernel_index]._thread_num.x, &_kernels[kernel_index]._thread_num.y, &_kernels[kernel_index]._thread_num.z);
        for (u32 i = 0u; i < desc.BoundResources; i++)
        {
            D3D12_SHADER_INPUT_BIND_DESC bind_desc{};
            p_reflect->GetResourceBindingDesc(i, &bind_desc);
            cs_ele._temp_bind_res_infos.insert(ParserBindResource(bind_desc, EShaderType::kCompute));
        }
        for (u32 i = 0u; i < desc.ConstantBuffers; i++)
        {
            auto cbuf = p_reflect->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC desc{};
            cbuf->GetDesc(&desc);
            cs_ele._temp_bind_res_infos.find(desc.Name)->second._bind_flag = ShaderBindResourceInfo::GetBindResourceFlag(desc.Name);
            for (u32 j = 0u; j < desc.Variables; j++)
            {
                auto variable = cbuf->GetVariableByIndex(j);
                D3D12_SHADER_VARIABLE_DESC vdesc{};
                variable->GetDesc(&vdesc);
                auto info = ParserBindVariable(vdesc);
                info._p_root_cbuf = &cs_ele._temp_bind_res_infos.find(desc.Name)->second;
                info._p_root_cbuf->_cbuf_size += vdesc.Size;
                cs_ele._temp_bind_res_infos.insert(std::make_pair(vdesc.Name, info));
            }
        }
    }
    //https://rtarun9.github.io/blogs/shader_reflection/#reflecting-input-parameters
    void D3DComputeShader::LoadAdditionalShaderReflection(const WString &sys_path, u16 kernel_index, ShaderVariantHash variant_hash)
    {
        using namespace std;
        namespace su = StringUtils;
        namespace fs = std::filesystem;
        ifstream src(sys_path, ios::in);
        string line;
        vector<string> lines;
        List<WString> cur_file_head_files{};
        WString parent_path = su::SubStrRange(_src_file_path, 0, _src_file_path.find_last_of(L"/"));
        auto &cur_kernel = _kernels[kernel_index]._variants[variant_hash];
        bool is_in_cbuf_scope = false;
        while (getline(src, line))
        {
            line = su::Trim(line);
            if (su::BeginWith(line, "//"))
            {
                lines.emplace_back(line);
                continue;
            }
            if (su::BeginWith(line, "CBUFFER_START"))
                is_in_cbuf_scope = true;
            if (su::BeginWith(line, "CBUFFER_END") && is_in_cbuf_scope)
                is_in_cbuf_scope = false;
            ParserBindResourceAddiInfo(_kernels[kernel_index]._variants[variant_hash]._temp_bind_res_infos, line, is_in_cbuf_scope);
            lines.emplace_back(line);
        }
        src.close();
        fs::path src_path(sys_path);
        fs::path pwd = src_path.parent_path();
        for (auto &head_file: cur_file_head_files)
        {
            fs::path temp = pwd;
            temp.append(head_file);
            LoadAdditionalShaderReflection(temp.wstring(), kernel_index, variant_hash);
        }
    }

    bool D3DComputeShader::RHICompileImpl(u16 kernel_index, ShaderVariantHash variant_hash)
    {
        if (kernel_index >= _kernels.size())
            return false;
        {
            std::lock_guard<std::mutex> lock(_ele_lock);
            if (kernel_index >= _elements.size())
                _elements.resize(kernel_index + 1);
        }
        bool succeed = true;
        ComPtr<ID3DBlob> tmp_blob = nullptr;
        ComPtr<ID3D12ShaderReflection> tmp_reflection{nullptr};
        std::set<WString> tmp_all_dep_file_pathes;
        //Vector<D3D_SHADER_MACRO> v = {{"COMPUTE", "1"}, {NULL, NULL}};
        auto &cur_variant = _kernels[kernel_index]._variants[variant_hash];
        Vector<D3D_SHADER_MACRO> marcos = ConstructVariantMarcos(cur_variant._active_keywords);
        try
        {
#ifdef SHADER_DXC
            succeed &= CreateFromFileDXC(_src_file_path, _kernels[kernel_index]._name, RenderConstants::kCSModel_6_1, marcos, tmp_blob, tmp_reflection, tmp_all_dep_file_pathes);
#else
            succeed &= CreateFromFileFXC(_src_file_path, _kernels[kernel_index]._name, RenderConstants::kCSModel_5_0, marcos, tmp_blob, tmp_reflection, tmp_all_dep_file_pathes);
#endif// SHADER_DXC
        }
        catch (const std::exception &)
        {
            succeed = false;
            LOG_ERROR(L"Compile shader with src {0} failed!", _src_file_path);
            _is_valid = false;
        }
        if (succeed)
        {
            for (auto &p: tmp_all_dep_file_pathes)
                cur_variant._all_dep_file_pathes.insert(p);
            _elements[kernel_index]._variants[variant_hash]._p_blob = tmp_blob;
            LoadReflectionInfo(tmp_reflection.Get(), kernel_index, variant_hash);
            LoadAdditionalShaderReflection(_src_file_path, kernel_index, variant_hash);
            GenerateInternalPSO(kernel_index, variant_hash);
            succeed = _is_valid;
            LOG_INFO(L"Compile shader with src {0} succeed!", _src_file_path);
        }
        return succeed;
    }

    void D3DComputeShader::GenerateInternalPSO(u16 kernel_index, ShaderVariantHash variant_hash)
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        auto device = static_cast<D3DContext &>(GraphicsContext::Get()).GetDevice();
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        CD3DX12_DESCRIPTOR_RANGE1 ranges[32]{};
        CD3DX12_ROOT_PARAMETER1 rootParameters[32]{};
        int cbuf_mask = 0, texture_count = 0;
        u8 root_param_index = 0;
        auto &cs_ele = _kernels[kernel_index]._variants[variant_hash];
        auto bindless_tex2d_it = std::find_if(cs_ele._temp_bind_res_infos.begin(), cs_ele._temp_bind_res_infos.end(), [](auto it) -> bool
                                                             { return it.second._name == "g_bindless_texture2d"; });
        if (bindless_tex2d_it != cs_ele._temp_bind_res_infos.end())
        {
            rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
            
            ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GPUVisibleDescriptorAllocator::kBindlessSRVCapacity, bindless_tex2d_it->second._res_slot
                ,bindless_tex2d_it->second._register_space,D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
            bindless_tex2d_it->second._bind_slot = root_param_index;
            _elements[kernel_index]._variants[variant_hash]._has_bindless_texture2d = true;
            ++root_param_index;
        }
        for (auto it = cs_ele._temp_bind_res_infos.begin(); it != cs_ele._temp_bind_res_infos.end(); it++)
        {
            auto &desc = it->second;
            if (desc._name == "g_bindless_texture2d")
                continue;
            if (desc._res_type == EBindResDescType::kTexture2D || desc._res_type == EBindResDescType::kTexture3D)
            {
                ++texture_count;
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kBuffer)
            {
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, desc._res_slot, desc._register_space);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kUAVTexture2D || desc._res_type == EBindResDescType::kRWTexture3D)
            {
                ++texture_count;
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kRWBuffer)
            {
                ranges[root_param_index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, desc._res_slot, desc._register_space);
                rootParameters[root_param_index].InitAsDescriptorTable(1, &ranges[root_param_index]);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
            else if (desc._res_type == EBindResDescType::kConstBuffer)
            {
                rootParameters[root_param_index].InitAsConstantBufferView(desc._res_slot, desc._register_space);
                desc._bind_slot = root_param_index;
                ++root_param_index;
            }
        }
        auto &d3d_ele = _elements[kernel_index]._variants[variant_hash];
        auto &sig = d3d_ele._p_sig;
        auto &pso = d3d_ele._pso;

        auto &samplers = CreateStaticSampler();
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(root_param_index, rootParameters, static_cast<u32>(samplers.size()), samplers.data(), rootSignatureFlags);
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        if (SUCCEEDED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error)) &&
            SUCCEEDED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&sig))))
        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
            pso_desc.pRootSignature = sig.Get();
            pso_desc.CS = {d3d_ele._p_blob->GetBufferPointer(), d3d_ele._p_blob->GetBufferSize()};
            pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
            if (SUCCEEDED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso))))
            {
                std::unordered_map<String, ShaderBindResourceInfo> cbuffer_bind_info;
                u32 cbuffer_size = 0;
                u16 cbuffer_count = 0;
                auto cb_iter = std::find_if(cs_ele._temp_bind_res_infos.begin(), cs_ele._temp_bind_res_infos.end(), [this](auto it) -> bool
                                            {
					auto& desc = it.second;
					return desc._res_type == EBindResDescType::kConstBuffer && desc._name != RenderConstants::kCBufNamePerScene & desc._name != RenderConstants::kCBufNamePerCamera; });
                if (cb_iter != cs_ele._temp_bind_res_infos.end())
                {
                    _internal_cbuf_name = cb_iter->second._name;
                }
                for (auto it = cs_ele._temp_bind_res_infos.begin(); it != cs_ele._temp_bind_res_infos.end(); it++)
                {
                    //临时方案，kCBufNameSceneState外部创建
                    if (it->second._res_type & EBindResDescType::kCBufferAttribute && it->second._p_root_cbuf->_name != RenderConstants::kCBufNamePerScene && it->second._p_root_cbuf->_name != RenderConstants::kCBufNamePerCamera)
                    {
                        cbuffer_bind_info.insert(std::make_pair(it->first, it->second));
                        if (it->second._res_type & EBindResDescType::kCBufferAttribute)
                            cbuffer_size += ShaderBindResourceInfo::GetVariableSize(it->second);
                    }
                }
                AL_ASSERT_MSG(cbuffer_size <= ComputeShader::kCBufferSize, "ComputeBuffer size must be less than 1024");
                cbuffer_size = ALIGN_TO_256(cbuffer_size);
                //这里暂时只支持一个cbuffer，以后按需修改
                auto cbuf_it = std::find_if(cs_ele._temp_bind_res_infos.begin(), cs_ele._temp_bind_res_infos.end(), [this](auto it)
                                            {
					auto& desc = it.second;
					return desc._res_type == EBindResDescType::kConstBuffer; });
                if (cbuf_it != cs_ele._temp_bind_res_infos.end())
                {
                    for (auto it = cbuffer_bind_info.begin(); it != cbuffer_bind_info.end(); it++)
                    {
                        auto old_variable_it = cs_ele._bind_res_infos.find(it->first);
                        if (old_variable_it != cs_ele._bind_res_infos.end())
                        {
                            memcpy(_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(it->second), _cache_cbuf_data + ShaderBindResourceInfo::GetVariableOffset(old_variable_it->second),
                                   ShaderBindResourceInfo::GetVariableSize(it->second));
                        }
                    }
                }
                cs_ele._bind_res_infos = std::move(cs_ele._temp_bind_res_infos);
                _is_valid = true;
            }
            else
                _is_valid = false;
        }
        else
        {
            _is_valid = false;
            LOG_ERROR(L"Create compute shader {} failed when generate internal pso!", _src_file_path);
        }
    }
#pragma endregion
    //-------------------------------------------------------------------------------D3DComputeShader---------------------------------------------------------------------------
}// namespace Ailu::RHI::DX12