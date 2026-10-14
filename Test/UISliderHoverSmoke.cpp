#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/CrashHandler.h"
#include "Framework/Common/Log.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/Style/UITheme.h"
#include "UI/UIRenderer.h"
#include "UI/Widget.h"
#include "Render/GraphicsContext.h"
#include "Render/CommandBuffer.h"
#include "Render/Texture.h"
#include "Render/RenderPipeline.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"
#include <iostream>
#include <stdexcept>
#include <Windows.h>

using namespace Ailu;

class PreviewSmokePipeline final : public Render::RenderPipeline
{
public:
    size_t CameraCount() const { return _cameras.size(); }
};

class SmokeApplication final : public Application
{
public:
    void RenderUI()
    {
        BeginFrame();
        UpdateResources(1.0f / 60.0f);
        UpdateScenes(1.0f / 60.0f);
        RenderFrame();
        RenderEditor();
        PresentFrame();
        EndFrame();
    }
};

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void CheckHiddenLayout()
    {
        UI::Canvas root;
        auto *container = root.AddChild<UI::Canvas>();
        container->GetSlotAs<UI::CanvasSlot>().Position({20.0f, 30.0f}).Size({300.0f, 200.0f});
        auto *child = container->AddChild<UI::Border>();
        child->GetSlotAs<UI::CanvasSlot>().Position({0.0f, 0.0f}).Size({40.0f, 20.0f});
        child->SetVisible(false);
        root.Arrange(0.0f, 0.0f, 500.0f, 400.0f);
        root.Update(0.0f);
        child->GetSlotAs<UI::CanvasSlot>().Position({180.0f, 140.0f});
        child->SetVisible(true);
        root.Update(0.0f);
        auto rect = child->GetArrangeRect();
        Require(rect.x == 200.0f && rect.y == 170.0f, "first show must use the new slot position");
        container->SetVisible(false);
        root.Update(0.0f);
        child->GetSlotAs<UI::CanvasSlot>().Position({100.0f, 80.0f}).Size({60.0f, 35.0f});
        root.Update(0.0f);
        container->SetVisible(true);
        root.Update(0.0f);
        rect = child->GetArrangeRect();
        Require(rect.x == 120.0f && rect.y == 110.0f && rect.z == 60.0f && rect.w == 35.0f,
                "nested hidden subtree must use updated position and size on first show");
        child->SetVisible(false);
        root.Update(0.0f);
        container->GetSlotAs<UI::CanvasSlot>().Position({50.0f, 60.0f}).Size({350.0f, 250.0f});
        root.Arrange(0.0f, 0.0f, 700.0f, 500.0f);
        root.Update(0.0f);
        child->SetVisible(true);
        root.Update(0.0f);
        rect = child->GetArrangeRect();
        Require(rect.x == 150.0f && rect.y == 140.0f, "reshown child must use resized parent's current transform");
        std::cout << "Hidden layout first show, nested mutation and parent resize PASS\n" << std::flush;
    }

    void CheckSlider()
    {
        UI::Slider slider;
        slider.Arrange(0.0f, 0.0f, 200.0f, 24.0f);
        slider.Update(0.0f);
        auto send_mouse = [&](UI::UIEvent::EType type, Vector2f position)
        {
            UI::UIEvent event{};
            event._type = type;
            event._target = event._current_target = &slider;
            event._mouse_position = position;
            event._key_code = EKey::kLBUTTON;
            slider.OnEvent(event);
        };
        Require(slider.HitTest({170.0f, 12.0f}) == &slider, "track must remain clickable");
        send_mouse(UI::UIEvent::EType::kMouseEnter, {170.0f, 12.0f});
        Require(slider.IsHovered() && !slider.IsThumbHovered(), "track hover must not highlight the thumb");
        send_mouse(UI::UIEvent::EType::kMouseMove, {7.0f, 12.0f});
        Require(slider.IsThumbHovered(), "moving from track to thumb must highlight it");
        send_mouse(UI::UIEvent::EType::kMouseMove, {170.0f, 12.0f});
        Require(!slider.IsThumbHovered(), "moving from thumb to track must clear thumb hover");
        send_mouse(UI::UIEvent::EType::kMouseDown, {170.0f, 12.0f});
        slider.Update(0.0f);
        Require(slider.GetValue() > 0.7f, "clicking track must still change the value");
        send_mouse(UI::UIEvent::EType::kMouseUp, {170.0f, 12.0f});
        send_mouse(UI::UIEvent::EType::kMouseExit, {220.0f, 12.0f});
        Require(!slider.IsThumbHovered(), "mouse exit must clear thumb hover");

        slider.SetValue(0.0f, false);
        slider.Translate({40.0f, 30.0f});
        slider.Update(0.0f);
        send_mouse(UI::UIEvent::EType::kMouseEnter, {47.0f, 42.0f});
        Require(slider.IsThumbHovered(), "thumb hover must respect the element transform");
        slider.SetInteractiveEnabled(false);
        slider.Update(0.0f);
        Require(!slider.IsThumbHovered(), "disabled thumb must not remain hovered");
    }

    void CheckImageCache(SmokeApplication &app)
    {
        using namespace Render;
        auto texture = Texture2D::Create(4u, 4u, ETextureFormat::kRGBA8UNorm);
        Vector<u8> pixels(4u * 4u * 4u, 255u);
        texture->SetPixelData(pixels.data(), 0u);
        texture->ApplySync();
        auto widget = MakeRef<UI::Widget>();
        auto image = MakeRef<UI::Image>(texture.get());
        widget->SetSize({100.0f, 60.0f});
        widget->AddToWidget(image);
        widget->SetTextureCacheMode(UI::EWidgetTextureCacheMode::kTexture);
        UI::UIManager::Get()->RegisterWidget(widget);
        auto *renderer = UI::UIRenderer::Get();
        for (u32 frame = 0u; frame < 10u; ++frame)
            app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_resident_bytes == 0u,
                "untracked image dependencies must fall back to Geometry");
        widget->SetTextureCacheImageRevision(0u);
        bool has_hit = false;
        for (u32 frame = 0u; frame < 80u && !has_hit; ++frame)
        {
            app.RenderUI();
            has_hit = renderer->GetFrameStats()._ui_texture_cache_hit_count == 1u;
        }
        Require(has_hit, "audited image widget must reach a steady texture cache hit");
        widget->SetTextureCacheImageRevision(1u);
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_dirty_count == 1u, "owner revision must repaint the cache");
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_hit_count == 1u, "owner repaint must settle to a cache hit");
        const u64 old_revision = texture->UploadRevision();
        pixels[0] = 0u;
        texture->SetPixelData(pixels.data(), 0u);
        texture->ApplySync();
        GraphicsContext::Get().WaitForGpu();
        Require(texture->UploadRevision() > old_revision, "texture reupload must advance its revision");
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_dirty_count == 1u, "texture reupload must repaint cached pixels");
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_hit_count == 1u, "reuploaded image must settle to a cache hit");
        auto preview = RenderTexture::Create(16u, 16u, "UIStaticPreview", ERenderTargetFormat::kDefault);
        auto clear_preview = [&]()
        {
            auto cmd = CommandBufferPool::Get("UIStaticPreview");
            cmd->SetRenderTarget(preview.get());
            cmd->ClearRenderTarget(Colors::kWhite);
            GraphicsContext::Get().ExecuteCommandBufferSync(cmd);
            CommandBufferPool::Release(cmd);
        };
        clear_preview();
        image->SetTexture(preview.get());
        widget->SetTextureCacheImageRevision(2u);
        app.RenderUI();
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_hit_count == 1u, "published static preview RT must cache");
        clear_preview();
        widget->SetTextureCacheImageRevision(3u);
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_dirty_count == 1u, "preview publication must repaint cached pixels");
        app.RenderUI();
        Require(renderer->GetFrameStats()._ui_texture_cache_hit_count == 1u, "preview publication must settle to a cache hit");
        UI::UIManager::Get()->UnRegisterWidget(widget.get());
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
        return 2;
    LogMgr::Init();
    InstallCrashHandler();
    Application::SetEngineConfigPath(argv[2]);
    SmokeApplication app;
    ApplicationInitContext init;
    init._project_file_path = argv[1];
    init._enable_audio = false;
    ApplicationDesc desc;
    desc._window_width = desc._gameview_width = 320;
    desc._window_height = desc._gameview_height = 180;
    if (app.Initialize(desc, init) != 0)
        return 3;
    ShowWindow(static_cast<HWND>(app.GetWindow().GetNativeWindowPtr()), SW_HIDE);
    int result = 0;
    UI::UITheme theme = UI::UITheme::DefaultDark();
    UI::UIManager::Get()->SetTheme(&theme);
    Render::Camera camera(16.0f / 9.0f, 0.1f, 100.0f);
    camera.OutputSize(320u, 180u);
    Render::Camera::sCurrent = Render::Camera::sScene = Render::Camera::sMain = &camera;
    auto pipeline = MakeScope<PreviewSmokePipeline>();
    Render::RenderPipeline::Register(pipeline.get());
    auto scene = MakeRef<SceneManagement::Scene>("UIInteractionSmoke");
    SceneManagement::SceneMgr::Get().OpenTemporaryScene(scene);
    Render::Camera::sCurrent = &camera;
    for (auto *feature: pipeline->GetRenderer()->GetFeatures())
        feature->SetActive(false);
    try
    {
        CheckHiddenLayout();
        CheckSlider();
        std::cout << "Slider track/thumb hover, click, exit, transform and disabled checks PASS\n" << std::flush;
        CheckImageCache(app);
        Render::Camera preview_camera;
        preview_camera.OutputSize(64u, 36u);
        auto preview_target = Render::RenderTexture::Create(64u, 36u, "OnDemandPreviewSmoke");
        pipeline->SetPreviewCamera(&preview_camera, preview_target.get());
        app.RenderUI();
        Require(pipeline->CameraCount() == 2u, "selecting camera must render the preview once");
        pipeline->SetPreviewCamera(&preview_camera, preview_target.get());
        app.RenderUI();
        Require(pipeline->CameraCount() == 1u, "unchanged preview must skip rendering");
        scene->MarkEdited();
        app.RenderUI();
        Require(pipeline->CameraCount() == 2u, "scene edit must refresh preview");
        scene->MarkEdited();
        app.RenderUI();
        Require(pipeline->CameraCount() == 2u, "another unsaved edit must refresh preview again");
        app.RenderUI();
        Require(pipeline->CameraCount() == 1u, "preview must return to cached state");
        pipeline->SetPreviewCamera(nullptr, nullptr);
        pipeline->SetPreviewCamera(&preview_camera, preview_target.get());
        app.RenderUI();
        Require(pipeline->CameraCount() == 2u, "reselecting camera must refresh preview");
        auto resized_target = Render::RenderTexture::Create(80u, 45u, "ResizedPreviewSmoke");
        pipeline->SetPreviewCamera(&preview_camera, resized_target.get());
        app.RenderUI();
        Require(pipeline->CameraCount() == 2u, "replacing preview target must refresh preview");
        pipeline->SetPreviewCamera(nullptr, nullptr);
        std::cout << "On-demand preview selection, idle, repeated edits, reselection and target replacement PASS\n";
        std::cout << "GPU image cache fallback, hit, owner revision, texture reupload and static preview RT checks PASS\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    Render::Camera::sCurrent = Render::Camera::sScene = Render::Camera::sMain = nullptr;
    scene.reset();
    pipeline.reset();
    Render::RenderPipeline::Register(nullptr);
    UI::UIManager::Get()->SetTheme(nullptr);
    app.Finalize();
    return result;
}
