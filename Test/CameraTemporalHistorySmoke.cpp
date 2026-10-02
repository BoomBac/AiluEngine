#include "Render/Camera.h"

#include <iostream>
#include <stdexcept>

using namespace Ailu;
using namespace Ailu::Render;

namespace
{
    bool Equal(const Matrix4x4f &left, const Matrix4x4f &right)
    {
        for (u32 row = 0; row < 4u; ++row)
            for (u32 column = 0; column < 4u; ++column)
                if (left[row][column] != right[row][column])
                    return false;
        return true;
    }

    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void CheckHistory(ECameraType type)
    {
        Camera camera(16.0f / 9.0f, 0.1f, 100.0f, type);
        camera.PrepareRenderFrame(10u);
        const Matrix4x4f first = camera.GetViewProjNoJitter();
        Require(Equal(camera.GetPrevViewProj(), first), "first rendered frame must have zero camera motion");

        camera.Position(Vector3f(1.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        camera.Position(Vector3f(2.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        Require(Equal(camera.GetPrevViewProj(), first), "controller rebuild must not advance rendered history");
        camera.PrepareRenderFrame(12u);
        const Matrix4x4f second = camera.GetViewProjNoJitter();
        Require(!Equal(first, second), "test camera must actually move");
        Require(Equal(camera.GetPrevViewProj(), first), "skipped frame must retain last rendered VP");
        camera.PrepareRenderFrame(12u);
        Require(Equal(camera.GetPrevViewProj(), first), "duplicate preparation must not replace previous VP");

        camera.Position(Vector3f(3.0f, 0.0f, 0.0f));
        camera.LookTo(Vector3f(0.2f, 0.0f, 1.0f), Vector3f::kUp);
        camera.PrepareRenderFrame(13u);
        Require(Equal(camera.GetPrevViewProj(), second), "LookTo must retain the previous rendered VP");

        camera.MarkTemporalHistoryReset();
        camera.Position(Vector3f(30.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        camera.PrepareRenderFrame(14u);
        const Matrix4x4f reset = camera.GetViewProjNoJitter();
        Require(Equal(camera.GetPrevViewProj(), reset), "reset must align previous VP on the next rendered view");
        camera.Position(Vector3f(31.0f, 0.0f, 0.0f));
        camera.RecalculateMatrix(true);
        camera.PrepareRenderFrame(15u);
        Require(Equal(camera.GetPrevViewProj(), reset), "motion must resume after reset");

        Matrix4x4f custom_view = camera.GetView();
        const Matrix4x4f custom_projection = camera.GetProjNoJitter();
        const Matrix4x4f before_custom = camera.GetViewProjNoJitter();
        custom_view[3][0] += 1.0f;
        camera.SetViewAndProj(custom_view, custom_projection);
        camera.PrepareRenderFrame(16u);
        Require(Equal(camera.GetPrevViewProj(), before_custom), "custom VP must retain previous rendered state");
        Require(Equal(camera.GetViewProjJittered(), camera.GetViewProjNoJitter()), "custom VP must remain jitter-free");
    }
}

int main()
{
    try
    {
        CheckHistory(ECameraType::kPerspective);
        CheckHistory(ECameraType::kOrthographic);
        std::cout << "CameraTemporalHistorySmoke PASS: first frame, rebuild, skipped/duplicate, reset, custom VP\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "CameraTemporalHistorySmoke FAIL: " << error.what() << '\n';
        return 1;
    }
}
