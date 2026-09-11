#include "Render/AssetPreviewGenerator.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/AssetPreviewMaterial.h"
#include "Render/Mesh.h"
#include "Render/2D/Sprite.h"
#include "Render/2D/SpriteBatcher.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/ResourcePool.h"
#include "Render/Shader.h"

using namespace Ailu::Render;
namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            Scope<SpriteBatcher> s_sprite_batcher;

            void GenerateMeshSnapshotImpl(u16 w, u16 h, Render::Mesh *mesh, Render::Material *material,
                                          const String &preview_name, Ref<Render::RenderTexture> &target)
            {
                if (mesh == nullptr || material == nullptr)
                {
                    LOG_ERROR("Invalid mesh or material for snapshot generation.");
                    return;
                }
                if (mesh->BoundBox().empty())
                {
                    LOG_ERROR("Mesh has no bounds for snapshot generation: {}", mesh->Name());
                    return;
                }

                if (target == nullptr)
                    target = RenderTexture::Create(w, h, std::format("{}_preview", preview_name));
                if (target == nullptr || target->Width() == 0u || target->Height() == 0u)
                {
                    LOG_ERROR("Failed to create mesh snapshot target: {}", preview_name);
                    return;
                }

                auto cmd = CommandBufferPool::Get("GeneratorMeshSnapshot");
                auto depth = cmd->GetTempRT(
                        target->Width(), target->Height(),
                        std::format("{}_temp_depth", target->Name()),
                        ERenderTargetFormat::kDepth, false, false, false);

                cmd->SetRenderTargetLoadAction(depth, ELoadStoreAction::kClear);
                cmd->SetRenderTargetLoadAction(target.get(), ELoadStoreAction::kClear);
                cmd->SetRenderTarget(target.get(), g_pRenderTexturePool->Get(depth));

                const auto &aabb = mesh->BoundBox()[0];
                Vector3f center = (aabb._min + aabb._max) * 0.5f;
                Vector3f extents = (aabb._max - aabb._min) * 0.5f;
                f32 radius = std::max(Magnitude(extents), 0.001f);

                f32 fov = 90.0f * k2Radius;
                f32 aspect = f32(target->Width()) / f32(target->Height());
                f32 near_plane = 0.01f;
                f32 far_plane = 1000.0f;

                f32 distance = radius / tanf(fov * 0.5f) * 1.2f;
                Vector3f view_dir = Normalize(Vector3f(-1, -1, -1));
                Vector3f camera_pos = -view_dir * distance;
                Vector3f up(0, 1, 0);
                Matrix4x4f view, proj;
                BuildViewMatrixLookToLH(view, camera_pos, view_dir, up);
                BuildPerspectiveFovLHMatrix(proj, fov, aspect, near_plane, far_plane);
                CBufferPerCameraData data{};
                data._MatrixV = view;
                data._MatrixP = proj;
                data._MatrixVP = view * proj;
                data._MatrixVP_NoJitter = data._MatrixVP;
                data._CameraPos = Vector4f(camera_pos, 1.0f);
                data._ScreenParams = Vector4f(1.0f / f32(target->Width()), 1.0f / f32(target->Height()),
                                              f32(target->Width()), f32(target->Height()));
                CBufferPerSceneData scene_data{};
                scene_data._DirectionalLights[0]._LightDir = Normalize(Vector3f(-0.45f, -1.0f, -0.65f));
                scene_data._DirectionalLights[0]._LightColor = Vector3f(1.0f, 1.0f, 1.0f);
                scene_data._DirectionalLights[0]._shadowmap_index = -1;
                scene_data._ActiveLightCount.x = 1.0f;
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerScene, &scene_data, sizeof(scene_data));
                cmd->SetGlobalTexture("_OcclusionTex", Texture::s_p_default_white);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &data, sizeof(data));
                const Matrix4x4f world_matrix = MatrixTranslation(Vector3f(-center.x, -center.y, -center.z));
                for (u16 i = 0; i < mesh->SubmeshCount(); i++)
                    cmd->DrawMesh(mesh, material, world_matrix, i);

                // The generated texture may be replaced immediately by AssetBrowser.  Submit
                // synchronously so the old render target cannot be destroyed before execution.
                GraphicsContext::Get().ExecuteCommandBufferSync(cmd);
                cmd->ReleaseTempRT(depth);
                CommandBufferPool::Release(cmd);
            }
        }

        void AssetPreviewGenerator::GeneratorMeshSnapshot(u16 w, u16 h, Render::Mesh *mesh,
                                                          Ref<Render::RenderTexture> &target)
        {
            auto standard_material = Material::s_standard_forward_lit.lock();
            auto preview_material = CreatePerObjectPreviewMaterial(standard_material.get());
            String preview_name = "mesh";
            if (mesh != nullptr)
                preview_name = mesh->Name();
            GenerateMeshSnapshotImpl(w, h, mesh, preview_material.get(), preview_name, target);
        }

        void AssetPreviewGenerator::GeneratorMaterialSnapshot(u16 w, u16 h, Render::Material *material,
                                                               Ref<Render::RenderTexture> &target)
        {
            if (material == nullptr)
            {
                LOG_ERROR("Invalid material for snapshot generation.");
                return;
            }
            auto sphere = Mesh::s_sphere.lock();
            if (sphere == nullptr)
            {
                LOG_ERROR("Sphere mesh is unavailable for material snapshot generation.");
                return;
            }

            auto preview_material = CreatePerObjectPreviewMaterial(material);
            GenerateMeshSnapshotImpl(w, h, sphere.get(), preview_material.get(), material->Name(), target);
        }
        void AssetPreviewGenerator::GeneratorSpriteSnapshot(u16 w, u16 h, Render::Sprite *sprite, Ref<Render::RenderTexture> &target)
        {
            if (!sprite || !sprite->_texture)
            {
                LOG_ERROR("Invalid sprite or sprite texture for snapshot generation.");
                return;
            }

            if (target == nullptr)
                target = RenderTexture::Create(w, h, std::format("{}_preview", sprite->Name()));

            if (s_sprite_batcher == nullptr)
            {
                s_sprite_batcher = MakeScope<SpriteBatcher>();
                s_sprite_batcher->Initialize();
            }

            // Build sprite render data matching the runtime rendering path
            SpriteRenderData render_data;
            render_data._local_to_world = BuildIdentityMatrix();
            render_data._uv_rect = sprite->_uv_rect;
            render_data._color = Colors::kWhite;
            render_data._size = sprite->GetRenderSize();
            render_data._pivot = sprite->_pivot;
            render_data._texture = sprite->_texture.get();
            render_data._material = nullptr;       // use default sprite material
            render_data._blend_mode = ESpriteBlendMode::kAlpha;

            // Orthographic camera: positioned behind the sprite (-Z), looking toward +Z.
            // In left-hand coordinates, the camera looks along +Z, so objects in front
            // have positive view-space Z — this is what BuildOrthographicMatrix expects.
            f32 max_dim = std::max(render_data._size.x, render_data._size.y);
            f32 margin = 1.1f;
            f32 half_extent = max_dim * 0.5f * margin;
            f32 aspect = f32(target->Width()) / f32(target->Height());
            f32 near_plane = 0.0f;
            f32 far_plane = 3.0f;

            // Adjust ortho bounds for aspect ratio
            f32 ortho_half_w = half_extent;
            f32 ortho_half_h = half_extent / aspect;

            // Camera at Z=-2, looking toward +Z (the sprite at origin)
            Vector3f camera_pos(0.0f, 0.0f, -2.0f);
            Vector3f view_dir(0.0f, 0.0f, 1.0f);
            Vector3f up(0.0f, 1.0f, 0.0f);

            Matrix4x4f view, proj;
            BuildViewMatrixLookToLH(view, camera_pos, view_dir, up);
            BuildOrthographicMatrix(proj, -ortho_half_w, ortho_half_w, ortho_half_h, -ortho_half_h, near_plane, far_plane);

            CBufferPerCameraData data;
            data._MatrixV = view;
            data._MatrixP = proj;
            data._MatrixVP = view * proj;
            data._CameraPos = Vector4f(camera_pos, 1.0f);

            s_sprite_batcher->Build({render_data});

            auto cmd = CommandBufferPool::Get("GeneratorSpriteSnapshot");
            auto depth = cmd->GetTempRT(
                    target->Width(), target->Height(),
                    std::format("{}_temp_depth", target->Name()),
                    ERenderTargetFormat::kDepth, false, false, false);

            // Clear both color and depth before rendering
            cmd->SetRenderTargetLoadAction(target.get(), ELoadStoreAction::kClear);
            cmd->SetRenderTargetLoadAction(depth, ELoadStoreAction::kClear);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &data, sizeof(data));
            s_sprite_batcher->Render(cmd.get(), target.get(), g_pRenderTexturePool->Get(depth));

            // The generated texture may be replaced immediately by AssetBrowser.  Submit
            // synchronously so the old render target cannot be destroyed before execution.
            GraphicsContext::Get().ExecuteCommandBufferSync(cmd);
            cmd->ReleaseTempRT(depth);
            CommandBufferPool::Release(cmd);
        }

        void AssetPreviewGenerator::Shutdown()
        {
            s_sprite_batcher.reset();
        }

    }// namespace Editor
}// namespace Ailu
