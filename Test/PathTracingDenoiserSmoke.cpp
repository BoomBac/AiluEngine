#include "Render/CommandBuffer.h"
#include "Render/Denoiser/PathTracingDenoiser.h"
#include "Render/RenderGraph/RenderGraph.h"
#include <iostream>
#include <stdexcept>

using namespace Ailu;
using namespace Ailu::Render;
namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    // Test-only backend: a real RG producer with a deterministic HDR clear, never installed in production.
    class SyntheticDenoiser final : public IPathTracingDenoiser
    {
    public:
        void Reset(u64 generation) override { _generation = generation; }
        void Shutdown() override {}
        EDenoiserStatus Status() const override { return EDenoiserStatus::kReady; }
        const char *Reason() const override { return "Synthetic test"; }
        PathTracingDenoiserOutput Record(RDG::RenderGraph &graph, const PathTracingDenoiserInput &input) override
        {
            TextureDesc desc(16, 16, ERenderTargetFormat::kRGBAFloat);
            auto handle = graph.CreateResource(desc, "Synthetic Denoiser");
            graph.AddPass(
                    "Synthetic HDR", RDG::PassDesc(),
                    [&](RDG::RenderGraphBuilder &builder) { handle = builder.Write(handle); },
                    [](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
                    { cmd->ClearRenderTarget(Color(2.0f, 0.25f, 0.5f, 1.0f)); });
            PathTracingDenoiserOutput output;
            output._display_texture = handle;
            output._generation = _generation;
            output._backend_instance = input._backend_instance;
            output._request_id = ++_request;
            output._width = input._width;
            output._height = input._height;
            output._source_sample_count = input._sample_count;
            output._valid = true;
            return output;
        }

    private:
        u64 _generation = 0u, _request = 0u;
    };
    std::unique_ptr<IPathTracingDenoiser> CreateSynthetic(EPathTracingDenoiser)
    { return std::make_unique<SyntheticDenoiser>(); }
}// namespace
int main()
{
    try
    {
        PathTracingDenoiserHost none;
        none.Configure(EPathTracingDenoiser::kNone, true, false);
        Require(none.Status() == EDenoiserStatus::kDisabled, "None must bypass");
        none.Configure(EPathTracingDenoiser::kOidnGpu, true, false);
        Require(none.Status() == EDenoiserStatus::kUnsupported || none.Status() == EDenoiserStatus::kInitializing,
                "GPU backend must either initialize lazily or report Unsupported");
        const u64 before = none.Generation();
        none.Configure(EPathTracingDenoiser::kOidnGpu, false, false);
        Require(none.Generation() > before, "Mode change must reset generation");
        PathTracingDenoiserHost host(CreateSynthetic);
        host.Configure(EPathTracingDenoiser::kOidnGpu, true, false);
        PathTracingDenoiserInput input;
        input._generation = host.Generation();
        input._backend_instance = host.BackendInstance();
        input._width = input._height = 16u;
        input._sample_count = 4u;
        input._valid_radiance = true;
        RDG::RenderGraph graph;
        // RG reserves id zero as invalid; renderer normally creates its frame targets first.
        graph.CreateResource(TextureDesc(1, 1, ERenderTargetFormat::kRGBAFloat), "Reserved Handle");
        input._radiance = graph.CreateResource(TextureDesc(16, 16, ERenderTargetFormat::kRGBAFloat), "History");
        auto output = host.Record(graph, input);
        if (!PathTracingDenoiserHost::CanDisplay(input, output))
            std::cerr << "Synthetic output: valid=" << output._valid << " handle=" << output._display_texture._id
                      << " generation=" << output._generation << '/' << input._generation
                      << " instance=" << output._backend_instance << '/' << input._backend_instance << '\n';
        Require(PathTracingDenoiserHost::CanDisplay(input, output), "Current synthetic output must display");
        graph.AddPass(
                "Debug GI synthetic consumer", RDG::PassDesc(),
                [output](RDG::RenderGraphBuilder &builder) { builder.Read(output._display_texture); },
                [](RDG::RenderGraph &, CommandBuffer *, const RenderingData &) {});
        Require(graph.Compile(), "Synthetic producer/consumer graph must compile");
        host.Reset();
        input._generation = host.Generation();
        Require(!PathTracingDenoiserHost::CanDisplay(input, output), "Old generation must be rejected");
        input._generation = output._generation;
        ++input._backend_instance;
        Require(!PathTracingDenoiserHost::CanDisplay(input, output), "Old backend instance must be rejected");
        input._backend_instance = output._backend_instance;
        ++input._width;
        Require(!PathTracingDenoiserHost::CanDisplay(input, output), "Old dimensions must be rejected");
        input._width = output._width;
        input._sample_count = 1u;
        Require(!PathTracingDenoiserHost::CanDisplay(input, output), "Future SPP must be rejected");
        input._sample_count = 4u;
        input._valid_radiance = false;
        Require(!host.Record(graph, input)._valid, "Invalid radiance must bypass the backend");
        host.Configure(EPathTracingDenoiser::kNone, true, false);
        Require(!host.Record(graph, input)._valid, "Disabled backend must discard output");
        std::cout << "PathTracingDenoiserSmoke passed (contract/RG compile only; no visual acceptance)\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
