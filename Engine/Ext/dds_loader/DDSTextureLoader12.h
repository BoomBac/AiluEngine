//--------------------------------------------------------------------------------------
// File: DDSTextureLoader12.h
//
// Functions for loading a DDS texture and creating a Direct3D runtime resource for it
//
// Note these functions are useful as a light-weight runtime loader for DDS files. For
// a full-featured DDS file reader, writer, and texture processing pipeline see
// the 'Texconv' sample and the 'DirectXTex' library.
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248926
// http://go.microsoft.com/fwlink/?LinkID=615561
//--------------------------------------------------------------------------------------

#pragma once

#ifdef __MINGW32__
#include <unknwn.h>
#endif

#ifndef _WIN32
#include <wsl/winadapter.h>
#include <wsl/wrladapter.h>
#endif

#if !defined(_WIN32) || defined(USING_DIRECTX_HEADERS)
#include <directx/d3d12.h>
#include <dxguids/dxguids.h>
#else
#include <d3d12.h>
#pragma comment(lib, "dxguid.lib")
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>


namespace DirectX
{
#ifndef DDS_ALPHA_MODE_DEFINED
#define DDS_ALPHA_MODE_DEFINED
    enum DDS_ALPHA_MODE : uint32_t
    {
        DDS_ALPHA_MODE_UNKNOWN = 0,
        DDS_ALPHA_MODE_STRAIGHT = 1,
        DDS_ALPHA_MODE_PREMULTIPLIED = 2,
        DDS_ALPHA_MODE_OPAQUE = 3,
        DDS_ALPHA_MODE_CUSTOM = 4,
    };

#endif

#ifndef DDS_LOADER_FLAGS_DEFINED
#define DDS_LOADER_FLAGS_DEFINED

    enum DDS_LOADER_FLAGS : uint32_t
    {
        DDS_LOADER_DEFAULT = 0,
        DDS_LOADER_FORCE_SRGB = 0x1,
        DDS_LOADER_IGNORE_SRGB = 0x2,
        DDS_LOADER_MIP_RESERVE = 0x8,
    };

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-dynamic-exception-spec"
#endif

    DEFINE_ENUM_FLAG_OPERATORS(DDS_LOADER_FLAGS);

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
    struct DDS_PIXELFORMAT
    {
        uint32_t size;
        uint32_t flags;
        uint32_t fourCC;
        uint32_t RGBBitCount;
        uint32_t RBitMask;
        uint32_t GBitMask;
        uint32_t BBitMask;
        uint32_t ABitMask;
    };

    struct DDS_HEADER
    {
        uint32_t size;
        uint32_t flags;
        uint32_t height;
        uint32_t width;
        uint32_t pitchOrLinearSize;
        uint32_t depth;// only if DDS_HEADER_FLAGS_VOLUME is set in flags
        uint32_t mipMapCount;
        uint32_t reserved1[11];
        DDS_PIXELFORMAT ddspf;
        uint32_t caps;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
        uint32_t reserved2;
    };
    //--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
// Macros
//--------------------------------------------------------------------------------------
#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                                \
((uint32_t) (uint8_t) (ch0) | ((uint32_t) (uint8_t) (ch1) << 8) | \
 ((uint32_t) (uint8_t) (ch2) << 16) | ((uint32_t) (uint8_t) (ch3) << 24))
#endif /* defined(MAKEFOURCC) */

// HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)
#define HRESULT_E_ARITHMETIC_OVERFLOW static_cast<HRESULT>(0x80070216L)

// HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
#define HRESULT_E_NOT_SUPPORTED static_cast<HRESULT>(0x80070032L)

// HRESULT_FROM_WIN32(ERROR_HANDLE_EOF)
#define HRESULT_E_HANDLE_EOF static_cast<HRESULT>(0x80070026L)

// HRESULT_FROM_WIN32(ERROR_INVALID_DATA)
#define HRESULT_E_INVALID_DATA static_cast<HRESULT>(0x8007000DL)
// DDS file structure definitions
//
// See DDS.h in the 'Texconv' sample and the 'DirectXTex' library
//--------------------------------------------------------------------------------------
#pragma pack(push, 1)

constexpr uint32_t DDS_MAGIC = 0x20534444;// "DDS "


#define DDS_FOURCC 0x00000004   // DDPF_FOURCC
#define DDS_RGB 0x00000040      // DDPF_RGB
#define DDS_LUMINANCE 0x00020000// DDPF_LUMINANCE
#define DDS_ALPHA 0x00000002    // DDPF_ALPHA
#define DDS_BUMPDUDV 0x00080000 // DDPF_BUMPDUDV

#define DDS_HEADER_FLAGS_VOLUME 0x00800000// DDSD_DEPTH

#define DDS_HEIGHT 0x00000002// DDSD_HEIGHT

#define DDS_CUBEMAP_POSITIVEX 0x00000600// DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX
#define DDS_CUBEMAP_NEGATIVEX 0x00000a00// DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEX
#define DDS_CUBEMAP_POSITIVEY 0x00001200// DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEY
#define DDS_CUBEMAP_NEGATIVEY 0x00002200// DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEY
#define DDS_CUBEMAP_POSITIVEZ 0x00004200// DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEZ
#define DDS_CUBEMAP_NEGATIVEZ 0x00008200// DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEZ

#define DDS_CUBEMAP_ALLFACES (DDS_CUBEMAP_POSITIVEX | DDS_CUBEMAP_NEGATIVEX | \
                          DDS_CUBEMAP_POSITIVEY | DDS_CUBEMAP_NEGATIVEY | \
                          DDS_CUBEMAP_POSITIVEZ | DDS_CUBEMAP_NEGATIVEZ)

#define DDS_CUBEMAP 0x00000200// DDSCAPS2_CUBEMAP

enum DDS_MISC_FLAGS2
{
    DDS_MISC_FLAGS2_ALPHA_MODE_MASK = 0x7L,
};


struct DDS_HEADER_DXT10
{
    DXGI_FORMAT dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;// see D3D11_RESOURCE_MISC_FLAG
    uint32_t arraySize;
    uint32_t miscFlags2;
};

#pragma pack(pop)

    HRESULT LoadTextureDataFromFile(const wchar_t *fileName,
                                    std::unique_ptr<uint8_t[]> &ddsData,
                                    const DDS_HEADER **header,
                                    const uint8_t **bitData,
                                    size_t *bitSize) noexcept;

    size_t BitsPerPixel(_In_ DXGI_FORMAT fmt) noexcept;
    DXGI_FORMAT GetDXGIFormat(const DDS_PIXELFORMAT &ddpf) noexcept;
    // Standard version
    HRESULT __cdecl LoadDDSTextureFromMemory(
            _In_ ID3D12Device *d3dDevice,
            _In_reads_bytes_(ddsDataSize) const uint8_t *ddsData,
            size_t ddsDataSize,
            _Outptr_ ID3D12Resource **texture,
            std::vector<D3D12_SUBRESOURCE_DATA> &subresources,
            size_t maxsize = 0,
            _Out_opt_ DDS_ALPHA_MODE *alphaMode = nullptr,
            _Out_opt_ bool *isCubeMap = nullptr);

    HRESULT __cdecl LoadDDSTextureFromFile(
            _In_ ID3D12Device *d3dDevice,
            _In_z_ const wchar_t *szFileName,
            _Outptr_ ID3D12Resource **texture,
            std::unique_ptr<uint8_t[]> &ddsData,
            std::vector<D3D12_SUBRESOURCE_DATA> &subresources,
            size_t maxsize = 0,
            _Out_opt_ DDS_ALPHA_MODE *alphaMode = nullptr,
            _Out_opt_ bool *isCubeMap = nullptr);

    // Extended version
    HRESULT __cdecl LoadDDSTextureFromMemoryEx(
            _In_ ID3D12Device *d3dDevice,
            _In_reads_bytes_(ddsDataSize) const uint8_t *ddsData,
            size_t ddsDataSize,
            size_t maxsize,
            D3D12_RESOURCE_FLAGS resFlags,
            DDS_LOADER_FLAGS loadFlags,
            _Outptr_ ID3D12Resource **texture,
            std::vector<D3D12_SUBRESOURCE_DATA> &subresources,
            _Out_opt_ DDS_ALPHA_MODE *alphaMode = nullptr,
            _Out_opt_ bool *isCubeMap = nullptr);

    HRESULT __cdecl LoadDDSTextureFromFileEx(
            _In_ ID3D12Device *d3dDevice,
            _In_z_ const wchar_t *szFileName,
            size_t maxsize,
            D3D12_RESOURCE_FLAGS resFlags,
            DDS_LOADER_FLAGS loadFlags,
            _Outptr_ ID3D12Resource **texture,
            std::unique_ptr<uint8_t[]> &ddsData,
            std::vector<D3D12_SUBRESOURCE_DATA> &subresources,
            _Out_opt_ DDS_ALPHA_MODE *alphaMode = nullptr,
            _Out_opt_ bool *isCubeMap = nullptr);
}// namespace DirectX
