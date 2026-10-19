#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "Ext/rapidjson/inc/document.h"
#define STB_IMAGE_IMPLEMENTATION
#include "Ext/stb/stb_image.h"
#include <array>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
static constexpr UINT kSize = 256u;

static void Check(HRESULT result)
{
    if (FAILED(result)) throw std::runtime_error("DX12 call failed: " + std::to_string(result));
}

static std::vector<char> ReadBytes(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return std::vector<char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device *device, UINT64 size, D3D12_HEAP_TYPE type,
                                          D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                          IID_PPV_ARGS(&resource)));
    return resource;
}

static void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4) return 2;
    const auto directory = std::filesystem::absolute(argv[1]);
    const auto reference = std::filesystem::absolute(argv[2]);
    const auto shaders = std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(directory);
    try
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<ID3D12Device> device;
        Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&list)));
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        UINT64 fence_value = 0;
        const auto submit = [&]()
        {
            Check(list->Close());
            ID3D12CommandList *lists[]{list.Get()};
            queue->ExecuteCommandLists(1, lists);
            Check(queue->Signal(fence.Get(), ++fence_value));
            const auto deadline = GetTickCount64() + 15000;
            while (fence->GetCompletedValue() < fence_value)
            {
                if (GetTickCount64() > deadline) throw std::runtime_error("GPU fence timeout");
                Sleep(1);
            }
        };
        const auto reset = [&]() { Check(allocator->Reset()); Check(list->Reset(allocator.Get(), nullptr)); };
        const auto texture = [&](UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                 D3D12_RESOURCE_STATES state)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width; desc.Height = height;
            desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
            desc.Format = format; desc.Flags = flags;
            ComPtr<ID3D12Resource> resource;
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                  IID_PPV_ARGS(&resource)));
            return resource;
        };
        int width, height;
        auto *pixels = stbi_load((reference / "height.png").string().c_str(), &width, &height, nullptr, 4);
        if (!pixels) throw std::runtime_error("Height PNG decode failed");
        auto input = texture(width, height, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_NONE,
                             D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT input_layout{};
        UINT64 input_size = 0;
        const auto input_desc = input->GetDesc();
        device->GetCopyableFootprints(&input_desc, 0, 1, 0, &input_layout, nullptr, nullptr, &input_size);
        auto upload = CreateBuffer(device.Get(), input_size, D3D12_HEAP_TYPE_UPLOAD,
                                   D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr;
        Check(upload->Map(0, nullptr, &mapped));
        for (int y = 0; y < height; ++y)
            std::memcpy(static_cast<char *>(mapped) + y * input_layout.Footprint.RowPitch,
                        pixels + y * width * 4, width * 4);
        upload->Unmap(0, nullptr);
        stbi_image_free(pixels);
        D3D12_TEXTURE_COPY_LOCATION input_destination{};
        input_destination.pResource = input.Get();
        input_destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION input_source{};
        input_source.pResource = upload.Get();
        input_source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        input_source.PlacedFootprint = input_layout;
        list->CopyTextureRegion(&input_destination, 0, 0, 0, &input_source, nullptr);
        Transition(list.Get(), input.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        submit();
        ComPtr<ID3D12DescriptorHeap> srv_heap, rtv_heap;
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = 1;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap)));
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Check(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(input.Get(), &srv, srv_heap->GetCPUDescriptorHandleForHeapStart());
        auto target = texture(kSize, kSize, DXGI_FORMAT_R32G32B32A32_FLOAT,
                              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
        device->CreateRenderTargetView(target.Get(), nullptr, rtv_heap->GetCPUDescriptorHandleForHeapStart());
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT output_layout{};
        UINT64 output_size = 0;
        const auto target_desc = target->GetDesc();
        device->GetCopyableFootprints(&target_desc, 0, 1, 0, &output_layout, nullptr, nullptr, &output_size);
        auto readback = CreateBuffer(device.Get(), output_size, D3D12_HEAP_TYPE_READBACK,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
        auto constants = CreateBuffer(device.Get(), 256, D3D12_HEAP_TYPE_UPLOAD,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {1, &range};
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[1].Descriptor = {1, 0};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = FLT_MAX;
        sampler.MaxAnisotropy = 1;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root_desc{2, parameters, 1, &sampler,
                                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> root_blob, error;
        Check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &root_blob, &error));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
                                          IID_PPV_ARGS(&root)));
        auto vertex = ReadBytes(shaders / "box_vs.dxil"), pixel = ReadBytes(shaders / "box_ps.dxil");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = root.Get();
        pso_desc.VS = {vertex.data(), vertex.size()};
        pso_desc.PS = {pixel.data(), pixel.size()};
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        auto &blend = pso_desc.BlendState.RenderTarget[0];
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.DepthStencilState.DepthEnable = FALSE;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso_desc.DepthStencilState.StencilEnable = FALSE;
        pso_desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                              D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        pso_desc.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pso;
        Check(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)));
        auto json = ReadBytes(reference / "cases.json");
        rapidjson::Document cases;
        cases.Parse(json.data(), json.size());
        if (cases.HasParseError() || !cases.IsArray()) throw std::runtime_error("Invalid oracle cases");
        for (const auto &entry : cases.GetArray())
        {
            const bool srgb = entry.HasMember("srgb") && entry["srgb"].GetBool();
            srv.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            device->CreateShaderResourceView(input.Get(), &srv, srv_heap->GetCPUDescriptorHandleForHeapStart());
            std::array<float, 20> values{};
            const char *names[]{"origin", "u", "v", "normal", "parameters"};
            for (unsigned row = 0; row < 5; ++row)
                for (unsigned column = 0; column < entry[names[row]].Size(); ++column)
                    values[row * 4 + column] = entry[names[row]][column].GetFloat();
            Check(constants->Map(0, nullptr, &mapped));
            std::memcpy(mapped, values.data(), sizeof(values));
            constants->Unmap(0, nullptr);
            reset();
            list->SetPipelineState(pso.Get());
            list->SetGraphicsRootSignature(root.Get());
            ID3D12DescriptorHeap *heaps[]{srv_heap.Get()};
            list->SetDescriptorHeaps(1, heaps);
            list->SetGraphicsRootDescriptorTable(0, srv_heap->GetGPUDescriptorHandleForHeapStart());
            list->SetGraphicsRootConstantBufferView(1, constants->GetGPUVirtualAddress());
            D3D12_VIEWPORT viewport{0, 0, float(kSize), float(kSize), 0, 1};
            D3D12_RECT rect{0, 0, LONG(kSize), LONG(kSize)};
            list->RSSetViewports(1, &viewport);
            list->RSSetScissorRects(1, &rect);
            const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list->DrawInstanced(3, 1, 0, 0);
            Transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = output_layout;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = target.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            Transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
            submit();
            Check(readback->Map(0, nullptr, &mapped));
            std::ofstream output(directory / (std::string(entry["name"].GetString()) + ".bin"), std::ios::binary);
            for (UINT row = 0; row < kSize; ++row)
                output.write(static_cast<const char *>(mapped) + row * output_layout.Footprint.RowPitch,
                             kSize * 4 * sizeof(float));
            readback->Unmap(0, nullptr);
        }
        ComPtr<ID3D12InfoQueue> info;
        if (SUCCEEDED(device.As(&info)))
            for (UINT64 index = 0; index < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
            {
                SIZE_T size = 0;
                info->GetMessage(index, nullptr, &size);
                std::vector<char> bytes(size);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
                Check(info->GetMessage(index, message, &size));
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    throw std::runtime_error(message->pDescription);
            }
        std::ofstream(directory / "transport.txt") << "GPU raster output captured; D3D12 debug layer clean\n";
        std::cout << "OBJECT_BOX_GPU_TRANSPORT PASS\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::ofstream(directory / "failure.txt") << error.what() << '\n';
        std::cerr << error.what() << '\n';
        return 1;
    }
}
