#include "Render/AssetPreviewGenerator.h"
#include "Render/Mesh.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"

using namespace Ailu::Render;
namespace Ailu
{
    namespace Editor
    {
        void AssetPreviewGenerator::GeneratorMeshSnapshot(u16 w, u16 h, Render::Mesh *mesh,Ref<Render::RenderTexture> &target)
        {
            if (!mesh)
            {
                LOG_ERROR("Invalid mesh for snapshot generation.");
                return;
            }
            if (target == nullptr)
                target = RenderTexture::Create(w, h, std::format("{}_preview", mesh->Name()));
            auto cmd = CommandBufferPool::Get("GeneratorMeshSnapshot");
            auto depth = cmd->GetTempRT(
                    target->Width(), target->Height(),
                    std::format("{}_temp_depth", target->Name()),
                    ERenderTargetFormat::kDepth, false, false, false);

            cmd->SetRenderTargetLoadAction(depth, ELoadStoreAction::kClear);
            cmd->SetRenderTarget(target.get(), g_pRenderTexturePool->Get(depth));

            const auto &aabb = mesh->BoundBox()[0];
            Vector3f center = (aabb._min + aabb._max) * 0.5f;
            Vector3f extents = (aabb._max - aabb._min) * 0.5f;
            f32 radius = Magnitude(extents);

            f32 fov = 90.0f * k2Radius;
            f32 aspect = f32(target->Width()) / f32(target->Height());
            f32 nearPlane = 0.01f;
            f32 farPlane = 1000.0f;

            f32 distance = radius * tanf(fov * 0.5f) * 1.2f;// 稍留边距
            Vector3f viewDir = Normalize(Vector3f(-1, -1, -1));
            Vector3f cameraPos = center - viewDir * distance;
            Vector3f up(0, 1, 0);
            Matrix4x4f view, proj;
            BuildViewMatrixLookToLH(view, cameraPos, viewDir, up);
            BuildPerspectiveFovLHMatrix(proj, fov, aspect, nearPlane, farPlane);
            CBufferPerCameraData data;
            data._MatrixV = view;
            data._MatrixP = proj;
            data._MatrixVP = view * proj;
            data._CameraPos = Vector4f(cameraPos, 1.0f);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &data, sizeof(data));
            for (u16 i = 0; i < mesh->SubmeshCount(); i++)
                cmd->DrawMesh(mesh, Material::s_standard_forward_lit.lock().get(), BuildIdentityMatrix(), i);
            GraphicsContext::Get().ExecuteCommandBuffer(cmd);
            cmd->ReleaseTempRT(depth);
            CommandBufferPool::Release(cmd);
        }

    }// namespace Editor
}// namespace Ailu
