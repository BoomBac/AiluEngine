//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_UIRENDERER_H
#define AILU_UIRENDERER_H
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Render/Buffer.h"
#include "Render/Material.h"
#include "Render/Shader.h"

namespace Ailu
{
    using Render::RTHandle;
    using Render::RenderTexture;
    using Render::Material;
    using Render::VertexBuffer;
    using Render::IndexBuffer;
    using Render::ConstantBuffer;
    using Render::CommandBuffer;

    namespace UI
    {
        class AILU_API UIRenderer
        {
        public:
            static void Init();
            static void Shutdown();
            static UIRenderer* Get();
            static void DrawQuad(Vector2f position, Vector2f size,f32 depth,Color color = Colors::kWhite);
        public:
            UIRenderer();
            ~UIRenderer();
            void Render(RTHandle color, RTHandle depth);
            void Render(RTHandle color, RTHandle depth, CommandBuffer *cmd);
            void Render(RenderTexture *color, RenderTexture* depth);
            void Render(RenderTexture *color, RenderTexture *depth, CommandBuffer *cmd);
        private:
            struct DrawerBlock
            {
            public:
                DISALLOW_COPY_AND_ASSIGN(DrawerBlock)
                DrawerBlock(DrawerBlock && other) noexcept ;
                DrawerBlock & operator=(DrawerBlock && other) noexcept ;
                explicit DrawerBlock(u32 vert_num = 1000u);
                ~DrawerBlock();
            public:
                Ref<Material> _mat;
                VertexBuffer* _vbuf;
                IndexBuffer* _ibuf;
                Vector<Vector3f> _pos_buf;
                Vector<Vector2f> _uv_buf;
                Vector<u32> _index_buf;
                u32 _pos_offset = 0u;
                u32 _uv_offset = 0u;
                u32 _index_offset = 0u;
            };
            u16 _frame_index = 0u;
            Ref<ConstantBuffer> _obj_cb;
            using FrameDrawerBlock = Map<Color, DrawerBlock*>;
            Array<FrameDrawerBlock, Render::RenderConstants::kFrameCount> _drawer_blocks;
        };

    }// namespace UI
}// namespace Aliu

#endif//AILU_UIRENDERER_H
