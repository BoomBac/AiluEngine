#include "UI/DrawerBlock.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
    namespace UI
    {
        using namespace Render;
        DrawerBlock::DrawerBlock(Ref<Render::Material> mat, u32 vert_num) : _max_vert_num(vert_num), _mat(mat)
        {
            Vector<VertexBufferLayoutDesc> desc_list;
            desc_list.emplace_back(RenderConstants::kSemanticPosition, EShaderDateType::kFloat3, 0);
            desc_list.emplace_back(RenderConstants::kSemanticTexcoord, EShaderDateType::kFloat2, 1);
            desc_list.emplace_back(RenderConstants::kSemanticColor, EShaderDateType::kFloat4, 2);
            _vbuf = VertexBuffer::Create(desc_list, std::format("block({})_vbuf", s_id_gen));
            _ibuf = IndexBuffer::Create(nullptr, vert_num, std::format("block({})_ibuf", s_id_gen), true);
            _obj_cb = ConstantBuffer::Create(RenderConstants::kPerObjectDataSize);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector3f), 0, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector2f), 1, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector4f), 2, true);
            //_ibuf->SetData((u8*)indices,6);
            _pos_buf.resize(vert_num);
            _uv_buf.resize(vert_num);
            _color_buf.resize(vert_num);
            _index_buf.resize(vert_num);
            GraphicsContext::Get().CreateResource(_vbuf);
            GraphicsContext::Get().CreateResource(_ibuf);
            ++s_id_gen;
        }
        DrawerBlock::~DrawerBlock()
        {
            --s_id_gen;
            DESTORY_PTR(_vbuf);
            DESTORY_PTR(_ibuf);
        }

        void DrawerBlock::SubmitVertexData()
        {
            _vbuf->SetData((u8 *)_pos_buf.data(), _cur_vert_num * sizeof(Vector3f), 0u, 0u);
            _vbuf->SetData((u8 *)_uv_buf.data(), _cur_vert_num * sizeof(Vector2f), 1u, 0u);
            _vbuf->SetData((u8 *)_color_buf.data(), _cur_vert_num * sizeof(Vector4f), 2u, 0u);
            _ibuf->SetData((u8 *)_index_buf.data(), _cur_index_num * sizeof(u32));
        }

        DrawerBlock::DrawerBlock(DrawerBlock &&other) noexcept
        {
            _vbuf = other._vbuf;
            _ibuf = other._ibuf;
            _mat = std::move(other._mat);
            other._vbuf = nullptr;
            other._ibuf = nullptr;
        }
        DrawerBlock &DrawerBlock::operator=(DrawerBlock &&other) noexcept
        {
            _vbuf = other._vbuf;
            _ibuf = other._ibuf;
            _mat = std::move(other._mat);
            other._vbuf = nullptr;
            other._ibuf = nullptr;
            return *this;
        }
    }
}