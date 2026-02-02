#include "Render/Features/MiscPasses.h"
#include "Render/CommandBuffer.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
    namespace Render
    {
        VolumeTexturePreviewPass::VolumeTexturePreviewPass()
        {
            _slice_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/texture3d_drawer.alasset"), "Runtime/VolumeSliceView");
            _slice_mat->EnableKeyword("_DrawMode_Slice");
        }
        VolumeTexturePreviewPass::~VolumeTexturePreviewPass()
        {
        }
        void VolumeTexturePreviewPass::OnPropertyChanged(const PropertyInfo& prop)
        {
            if (prop.Name() == "_is_slice_mode")
            {
                if (_is_slice_mode)
                {
                    _slice_mat->EnableKeyword("_DrawMode_Slice");
                    _slice_mat->DisableKeyword("_DrawMode_Volume");
                }
                else
                {
                    _slice_mat->EnableKeyword("_DrawMode_Volume");
                    _slice_mat->DisableKeyword("_DrawMode_Slice");
                }
            }
        }
        
        void Ailu::Render::VolumeTexturePreviewPass::EnsureTarget()
        {
            static u32 id = 0u;
            if (_color_buffer == nullptr)
            {
                TextureDesc desc = TextureDesc(_view_size.x, _view_size.y, ERenderTargetFormat::kDefault);
                desc._load = ELoadStoreAction::kNotCare;
                desc._store = ELoadStoreAction::kNotCare;
                desc._clear_color = kBackgroundColor;
                desc._is_color_target = true;
                _color_buffer = RenderTexture::Create(desc, std::format("VolumeTexturePreview_ColorBuffer_{}", id));
                desc = TextureDesc(_view_size.x, _view_size.y, ERenderTargetFormat::kDepth);
                desc._is_depth_target = true;
                _depth_buffer = RenderTexture::Create(desc, std::format("VolumeTexturePreview_DepthBuffer_{}", id));
                ++id;
            }
            else if (_color_buffer->Width() != _view_size.x || _color_buffer->Height() != _view_size.y)
            {
                _color_buffer.reset();
                _depth_buffer.reset();
                TextureDesc desc = TextureDesc(_view_size.x, _view_size.y, ERenderTargetFormat::kDefault);
                desc._load = ELoadStoreAction::kNotCare;
                desc._store = ELoadStoreAction::kNotCare;
                desc._clear_color = kBackgroundColor;
                desc._is_color_target = true;
                _color_buffer = RenderTexture::Create(desc, std::format("VolumeTexturePreview_ColorBuffer_{}", id));
                desc = TextureDesc(_view_size.x, _view_size.y, ERenderTargetFormat::kDepth);
                desc._is_depth_target = true;
                _depth_buffer = RenderTexture::Create(desc, std::format("VolumeTexturePreview_DepthBuffer_{}", id));
                ++id;
            }
            if (_on_target_ready)
            {
                _on_target_ready(_color_buffer.get());
            }
        }

#define NEW_MATRIX
        template<typename T, int Da, int Db, int Dc>
        void MatrixMultipyNew(
            Matrix<T, Da, Dc>& out,
            const Matrix<T, Da, Db>& a,
            const Matrix<T, Db, Dc>& b)
        {
            for (int i = 0; i < Da; ++i)
            {
                for (int j = 0; j < Dc; ++j)
                {
                    T sum = T(0);
                    for (int k = 0; k < Db; ++k)
                    {
                        sum += a[i][k] * b[k][j];
                    }
                    out[i][j] = sum;
                }
            }
        }


        Matrix4x4f MatrixMultiplyNew(const Matrix4x4f &m1, const Matrix4x4f &m2)
        {
            Matrix4x4f ret{};
            MatrixMultipyNew(ret, m2, m1);
            return ret;
        }

        void VolumeTexturePreviewPass::Execute(GraphicsContext *context, RenderingData &rendering_data)
        {
            auto cmd = CommandBufferPool::Get("VolumeTexturePreviewPass");
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &_per_camera_cbuf, sizeof(CBufferPerCameraData));
            auto slice_to_world = [](f32 slice01) -> f32
            {
                // map [0,1] -> [-1,1] to match the cube extents used by the shader
                return Lerp(-1.0f, 1.0f, std::clamp(slice01, 0.0f, 1.0f));
            };
            EnsureTarget();
            Matrix4x4f world_mat = MatrixTranslation(Vector3f::kZero);
            {
                GpuProfileBlock profile(cmd.get(), _name);
                cmd->SetRenderTarget(_color_buffer.get(), _depth_buffer.get());
                cmd->ClearRenderTarget(kBackgroundColor);
                if (_is_slice_mode)
                {
                    #if defined(NEW_MATRIX)
                        // Y slice (base plane assumed to be XZ, normal +Y)
                        _slice_mat->SetFloat("_Slice", _slice_y);
                        _slice_mat->SetFloat("_SliceAxis", 1.0f);
                        world_mat = MatrixTranslation(Vector3f{0.0f, slice_to_world(1.0f - _slice_y), 0.0f});
                        cmd->DrawMesh(Mesh::s_plane.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);

                        // Z slice
                        _slice_mat->SetFloat("_Slice", _slice_z);
                        _slice_mat->SetFloat("_SliceAxis", 2.0f);
                        world_mat = MatrixMultiplyNew(MatrixTranslation(Vector3f{0.0f, 0.0f, slice_to_world(1.0f - _slice_z)}),MatrixRotationX(k2Radius * 90.0f));
                        cmd->DrawMesh(Mesh::s_plane.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);

                        // X slice
                        _slice_mat->SetFloat("_Slice", _slice_x);
                        _slice_mat->SetFloat("_SliceAxis", 0.0f);
                        world_mat = MatrixMultiplyNew(MatrixTranslation(Vector3f{slice_to_world(1.0f - _slice_x), 0.0f, 0.0f}), MatrixRotationZ(k2Radius * 90.0f));
                        cmd->DrawMesh(Mesh::s_plane.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);
                    #else
                        // Y slice (base plane assumed to be XZ, normal +Y)
                        _slice_mat->SetFloat("_Slice", _slice_y);
                        _slice_mat->SetFloat("_SliceAxis", 1.0f);
                        world_mat = MatrixTranslation(Vector3f{0.0f, slice_to_world(_slice_y), 0.0f});
                        cmd->DrawMesh(Mesh::s_plane.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);

                        // Z slice
                        _slice_mat->SetFloat("_Slice", _slice_z);
                        _slice_mat->SetFloat("_SliceAxis", 2.0f);
                        world_mat = MatrixRotationX(k2Radius * 90.0f) * MatrixTranslation(Vector3f{0.0f, 0.0f, slice_to_world(_slice_z)});
                        cmd->DrawMesh(Mesh::s_plane.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);

                        // X slice
                        _slice_mat->SetFloat("_Slice", _slice_x);
                        _slice_mat->SetFloat("_SliceAxis", 0.0f);
                        world_mat = MatrixRotationZ(k2Radius * 90.0f) * MatrixTranslation(Vector3f{slice_to_world(_slice_x), 0.0f, 0.0f});
                        cmd->DrawMesh(Mesh::s_plane.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);
                    #endif
                }
                else
                {
                    cmd->DrawMesh(Mesh::s_cube.lock().get(), _slice_mat.get(), world_mat, 0, 0, 1);
                }
            }
            context->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
        }
        void VolumeTexturePreviewPass::BeginPass(GraphicsContext *context)
        {
            Vector3f target = {0.0f, 0.0f, 0.0f};

            // forward
            Vector3f forward = Normalize(target - _camera_pos);

            // 防止 forward 与 worldUp 共线
            Vector3f worldUp = {0.0f, 1.0f, 0.0f};
            if (fabs(DotProduct(forward, worldUp)) > 0.999f)
            {
                // 接近极点，换一个 up
                worldUp = {0.0f, 0.0f, 1.0f};
            }

            // right / up
            Vector3f right = Normalize(CrossProduct(worldUp, forward));
            Vector3f up = CrossProduct(forward, right);

            // 构建视图矩阵
            BuildViewMatrixLookToLH(
                    _per_camera_cbuf._MatrixV,
                    _camera_pos,
                    forward,
                    up);
            BuildPerspectiveFovLHMatrix(_per_camera_cbuf._MatrixP,60.0f * k2Radius , (f32) _view_size.x / (f32) _view_size.y, 0.1f, 100.0f);
            _per_camera_cbuf._MatrixVP_NoJitter = _per_camera_cbuf._MatrixV * _per_camera_cbuf._MatrixP;
           _per_camera_cbuf._MatrixVP = _per_camera_cbuf._MatrixVP_NoJitter;
           _slice_mat->SetVector("_camera_pos", _camera_pos);
        }
    }// namespace Render
    
} // namespace Ailu